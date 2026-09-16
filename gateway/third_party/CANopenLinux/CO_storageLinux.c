/*
 * Standalone Linux file persistence adapted from CANopenLinux.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "CO_storageLinux.h"
#include "301/crc16-ccitt.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint32_t
entryErrorBit(size_t index) {
    return UINT32_C(1) << (index < 31 ? index : 31);
}

static CO_ReturnError_t
openEntry(CO_storage_entry_t* entry) {
    entry->fp = fopen(entry->filename, "rb");
    return entry->fp == NULL ? CO_ERROR_SYSCALL : CO_ERROR_NO;
}

static CO_ReturnError_t
writeEntry(CO_storage_entry_t* entry, uint16_t crc) {
    char temporary[CO_STORAGE_PATH_MAX + sizeof(".tmp")];
    int length = snprintf(temporary, sizeof(temporary), "%s.tmp", entry->filename);
    if (length < 0 || (size_t)length >= sizeof(temporary)) {
        return CO_ERROR_ILLEGAL_ARGUMENT;
    }

    FILE* fp = fopen(temporary, "wb");
    if (fp == NULL) {
        return CO_ERROR_SYSCALL;
    }

    bool complete = fwrite(entry->addr, 1, entry->len, fp) == entry->len
                    && fwrite(&crc, 1, sizeof(crc), fp) == sizeof(crc) && fflush(fp) == 0 && fsync(fileno(fp)) == 0;
    if (fclose(fp) != 0) {
        complete = false;
    }
    if (!complete || rename(temporary, entry->filename) != 0) {
        (void)remove(temporary);
        return CO_ERROR_SYSCALL;
    }

    if (entry->fp != NULL) {
        fclose(entry->fp);
        entry->fp = NULL;
    }
    CO_ReturnError_t error = openEntry(entry);
    if (error == CO_ERROR_NO) {
        entry->crc = crc;
    }
    return error;
}

static CO_ReturnError_t
loadEntry(CO_storage_entry_t* entry) {
    uint8_t* buffer = malloc(entry->len);
    uint16_t storedCrc;
    uint8_t extra;
    if (buffer == NULL) {
        return CO_ERROR_OUT_OF_MEMORY;
    }

    size_t dataRead = fread(buffer, 1, entry->len, entry->fp);
    size_t crcRead = fread(&storedCrc, 1, sizeof(storedCrc), entry->fp);
    bool hasExtra = fread(&extra, 1, 1, entry->fp) != 0;
    uint16_t computedCrc = crc16_ccitt(buffer, dataRead, 0);
    CO_ReturnError_t error = CO_ERROR_DATA_CORRUPT;
    if (dataRead == entry->len && crcRead == sizeof(storedCrc) && !hasExtra && computedCrc == storedCrc) {
        memcpy(entry->addr, buffer, entry->len);
        entry->crc = storedCrc;
        error = CO_ERROR_NO;
    }
    free(buffer);
    return error;
}

CO_ReturnError_t
CO_storageLinux_init(CO_storageLinux_t* storage, CO_storage_entry_t* entries, size_t entriesCount,
                     uint32_t* storageInitError) {
    if (storage == NULL || entries == NULL || entriesCount == 0 || storageInitError == NULL) {
        return CO_ERROR_ILLEGAL_ARGUMENT;
    }

    storage->entries = entries;
    storage->entriesCount = entriesCount;
    *storageInitError = 0;
    CO_ReturnError_t result = CO_ERROR_NO;

    for (size_t i = 0; i < entriesCount; i++) {
        entries[i].fp = NULL;
    }

    for (size_t i = 0; i < entriesCount; i++) {
        CO_storage_entry_t* entry = &entries[i];
        if (entry->addr == NULL || entry->len == 0
            || memchr(entry->filename, '\0', sizeof(entry->filename)) == NULL
            || entry->filename[0] == '\0') {
            *storageInitError |= entryErrorBit(i);
            return CO_ERROR_ILLEGAL_ARGUMENT;
        }
        CO_ReturnError_t error = openEntry(entry);
        if (error == CO_ERROR_NO) {
            error = loadEntry(entry);
        } else if (errno == ENOENT) {
            error = writeEntry(entry, crc16_ccitt(entry->addr, entry->len, 0));
        }

        if (error == CO_ERROR_DATA_CORRUPT) {
            *storageInitError |= entryErrorBit(i);
            result = CO_ERROR_DATA_CORRUPT;
            error = writeEntry(entry, crc16_ccitt(entry->addr, entry->len, 0));
        }
        if (error != CO_ERROR_NO) {
            *storageInitError |= entryErrorBit(i);
            CO_storageLinux_auto_process(storage, true);
            storage->entries = NULL;
            storage->entriesCount = 0;
            return error;
        }
    }
    return result;
}

uint32_t
CO_storageLinux_auto_process(CO_storageLinux_t* storage, bool_t closeFiles) {
    if (storage == NULL || storage->entries == NULL) {
        return UINT32_MAX;
    }

    uint32_t storageError = 0;
    for (size_t i = 0; i < storage->entriesCount; i++) {
        CO_storage_entry_t* entry = &storage->entries[i];
        if (entry->fp == NULL) {
            continue;
        }

        uint16_t crc = crc16_ccitt(entry->addr, entry->len, 0);
        if (crc != entry->crc && writeEntry(entry, crc) != CO_ERROR_NO) {
            storageError |= entryErrorBit(i);
        }
        if (closeFiles && entry->fp != NULL) {
            fclose(entry->fp);
            entry->fp = NULL;
        }
    }
    return storageError;
}
