/*
 * Standalone Linux file persistence adapted from CANopenLinux.
 * Copyright 2021 Janez Paternoster
 * SPDX-License-Identifier: Apache-2.0
 * Project adaptation: fail-closed loading and durable, atomic standalone writes.
 */

#include "CO_storageLinux.h"
#include "301/crc16-ccitt.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static uint32_t
entryErrorBit(size_t index) {
    return UINT32_C(1) << (index < 31 ? index : 31);
}

static CO_ReturnError_t
openEntry(CO_storage_entry_t* entry) {
    int fd = open(entry->filename, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        return CO_ERROR_SYSCALL;
    }
    struct stat status;
    if (fstat(fd, &status) != 0 || !S_ISREG(status.st_mode) || status.st_nlink != 1 ||
        status.st_uid != geteuid() || (status.st_mode & 0022) != 0) {
        close(fd);
        errno = EINVAL;
        return CO_ERROR_SYSCALL;
    }
    entry->fp = fdopen(fd, "rb");
    if (entry->fp == NULL) {
        int saved = errno;
        close(fd);
        errno = saved;
        return CO_ERROR_SYSCALL;
    }
    return CO_ERROR_NO;
}

static CO_ReturnError_t
writeEntry(CO_storage_entry_t* entry, uint16_t crc) {
    char temporary[CO_STORAGE_PATH_MAX + sizeof(".XXXXXX")];
    char parent[CO_STORAGE_PATH_MAX];
    memcpy(parent, entry->filename, strlen(entry->filename) + 1);
    char* slash = strrchr(parent, '/');
    if (slash == NULL) {
        strcpy(parent, ".");
    } else if (slash == parent) {
        slash[1] = '\0';
    } else {
        *slash = '\0';
    }
    int directory = open(parent, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (directory < 0) {
        return CO_ERROR_SYSCALL;
    }
    int length = snprintf(temporary, sizeof(temporary), "%s.XXXXXX", entry->filename);
    if (length < 0 || (size_t)length >= sizeof(temporary)) {
        close(directory);
        return CO_ERROR_ILLEGAL_ARGUMENT;
    }
    int fd = mkstemp(temporary);
    if (fd < 0) {
        close(directory);
        return CO_ERROR_SYSCALL;
    }
    FILE* fp = fdopen(fd, "wb");
    if (fp == NULL) {
        close(fd);
        unlink(temporary);
        close(directory);
        return CO_ERROR_SYSCALL;
    }
    const uint8_t encodedCrc[2] = {(uint8_t)crc, (uint8_t)(crc >> 8)};
    bool complete = fwrite(entry->addr, 1, entry->len, fp) == entry->len
                    && fwrite(encodedCrc, 1, sizeof(encodedCrc), fp) == sizeof(encodedCrc)
                    && fflush(fp) == 0 && fsync(fileno(fp)) == 0;
    if (fclose(fp) != 0) {
        complete = false;
    }
    if (!complete || rename(temporary, entry->filename) != 0) {
        unlink(temporary);
        close(directory);
        return CO_ERROR_SYSCALL;
    }
    int syncResult = fsync(directory);
    close(directory);
    if (entry->fp != NULL) {
        fclose(entry->fp);
        entry->fp = NULL;
    }
    CO_ReturnError_t error = openEntry(entry);
    if (error == CO_ERROR_NO) {
        entry->crc = crc;
    }
    return syncResult == 0 ? error : CO_ERROR_SYSCALL;
}

static CO_ReturnError_t
loadEntry(CO_storage_entry_t* entry) {
    uint8_t* buffer = malloc(entry->len);
    uint8_t encodedCrc[2];
    if (buffer == NULL) {
        return CO_ERROR_OUT_OF_MEMORY;
    }
    size_t dataRead = fread(buffer, 1, entry->len, entry->fp);
    size_t crcRead = fread(encodedCrc, 1, sizeof(encodedCrc), entry->fp);
    int extra = fgetc(entry->fp);
    CO_ReturnError_t error = CO_ERROR_DATA_CORRUPT;
    if (ferror(entry->fp)) {
        error = CO_ERROR_SYSCALL;
    } else if (dataRead == entry->len && crcRead == sizeof(encodedCrc) && extra == EOF) {
        uint16_t storedCrc = (uint16_t)((uint16_t)encodedCrc[0] | ((uint16_t)encodedCrc[1] << 8));
        if (crc16_ccitt(buffer, entry->len, 0) == storedCrc) {
            memcpy(entry->addr, buffer, entry->len);
            entry->crc = storedCrc;
            error = CO_ERROR_NO;
        }
    }
    free(buffer);
    return error;
}

/* CRC alone is not a reliable dirty flag: distinct registry contents can share a CRC16. */
static bool
entryChanged(CO_storage_entry_t* entry, uint16_t crc) {
    if (entry->fp == NULL || fseek(entry->fp, 0, SEEK_SET) != 0) {
        return true;
    }
    const uint8_t* data = entry->addr;
    uint8_t buffer[256];
    for (size_t offset = 0; offset < entry->len;) {
        size_t count = entry->len - offset;
        if (count > sizeof(buffer)) {
            count = sizeof(buffer);
        }
        if (fread(buffer, 1, count, entry->fp) != count || memcmp(data + offset, buffer, count) != 0) {
            return true;
        }
        offset += count;
    }
    uint8_t encodedCrc[2];
    size_t crcRead = fread(encodedCrc, 1, sizeof(encodedCrc), entry->fp);
    int extra = fgetc(entry->fp);
    return crcRead != sizeof(encodedCrc) || encodedCrc[0] != (uint8_t)crc
           || encodedCrc[1] != (uint8_t)(crc >> 8) || extra != EOF || ferror(entry->fp);
}

CO_ReturnError_t
CO_storageLinux_init(CO_storageLinux_t* storage, CO_storage_entry_t* entries, size_t entriesCount,
                     uint32_t* storageInitError, bool_t allowMissing) {
    if (storage == NULL || entries == NULL || entriesCount == 0 || storageInitError == NULL) {
        return CO_ERROR_ILLEGAL_ARGUMENT;
    }
    *storage = (CO_storageLinux_t){0};
    *storageInitError = 0;
    for (size_t i = 0; i < entriesCount; i++) {
        entries[i].fp = NULL;
    }
    for (size_t i = 0; i < entriesCount; i++) {
        CO_storage_entry_t* entry = &entries[i];
        CO_ReturnError_t error = CO_ERROR_ILLEGAL_ARGUMENT;
        if (entry->addr != NULL && entry->len != 0
            && memchr(entry->filename, '\0', sizeof(entry->filename)) != NULL && entry->filename[0] != '\0') {
            error = openEntry(entry);
            if (error == CO_ERROR_NO) {
                error = loadEntry(entry);
            } else if (allowMissing && errno == ENOENT) {
                error = CO_ERROR_NO;
            }
        }
        if (error != CO_ERROR_NO) {
            int saved = errno;
            *storageInitError |= entryErrorBit(i);
            for (size_t j = 0; j <= i; j++) {
                if (entries[j].fp != NULL) {
                    fclose(entries[j].fp);
                    entries[j].fp = NULL;
                }
            }
            errno = saved;
            return error;
        }
    }
    storage->entries = entries;
    storage->entriesCount = entriesCount;
    return CO_ERROR_NO;
}

uint32_t
CO_storageLinux_auto_process(CO_storageLinux_t* storage, bool_t closeFiles) {
    if (storage == NULL || storage->entries == NULL) {
        return UINT32_MAX;
    }
    uint32_t storageError = 0;
    for (size_t i = 0; i < storage->entriesCount; i++) {
        CO_storage_entry_t* entry = &storage->entries[i];
        uint16_t crc = crc16_ccitt(entry->addr, entry->len, 0);
        if ((crc != entry->crc || entryChanged(entry, crc)) && writeEntry(entry, crc) != CO_ERROR_NO) {
            storageError |= entryErrorBit(i);
        }
        if (closeFiles && entry->fp != NULL) {
            if (fclose(entry->fp) != 0) {
                storageError |= entryErrorBit(i);
            }
            entry->fp = NULL;
        }
    }
    return storageError;
}
