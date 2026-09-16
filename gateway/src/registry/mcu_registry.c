#include "mcu_registry.h"

#include <errno.h>
#include <fcntl.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static uint32_t read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | bytes[3];
}

static void write_be32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static bool identity_equal(const CO_LSS_address_t *left, const CO_LSS_address_t *right) {
    return CO_LSS_ADDRESS_EQUAL((*left), (*right));
}

int mcu_registry_lookup(const McuRegistry *registry, const CO_LSS_address_t *identity, uint8_t *node_id) {
    if (registry == NULL || registry->lock_fd < 0 || identity == NULL || node_id == NULL) {
        return -EINVAL;
    }
    for (size_t i = 0; i < registry->count; i++) {
        if (identity_equal(&registry->devices[i].identity, identity)) {
            *node_id = registry->devices[i].node_id;
            return 0;
        }
    }
    return -ENOENT;
}

int mcu_registry_check_assignment(const McuRegistry *registry, const CO_LSS_address_t *identity, uint8_t node_id) {
    if (registry == NULL || registry->lock_fd < 0 || identity == NULL || node_id == 0 || node_id > 127) {
        return -EINVAL;
    }
    bool present = false;
    for (size_t i = 0; i < registry->count; i++) {
        bool same_identity = identity_equal(&registry->devices[i].identity, identity);
        bool same_node = registry->devices[i].node_id == node_id;
        if (same_identity != same_node) {
            return -EEXIST;
        }
        present |= same_identity;
    }
    return !present && registry->count == MCU_REGISTRY_CAPACITY ? -ENOSPC : 0;
}

static int decode_registry(McuRegistry *registry) {
    if (memcmp(registry->encoded, "MCRG\0\1", 6) != 0 || registry->encoded[6] != 0
        || registry->encoded[7] > MCU_REGISTRY_CAPACITY) {
        return -EBADMSG;
    }
    uint16_t count = registry->encoded[7];
    for (size_t i = 0; i < MCU_REGISTRY_CAPACITY; i++) {
        const uint8_t *record = &registry->encoded[8 + 20 * i];
        if (i >= count) {
            for (size_t j = 0; j < 20; j++) {
                if (record[j] != 0) {
                    return -EBADMSG;
                }
            }
            continue;
        }
        CO_LSS_address_t identity = {.identity = {
            .vendorID = read_be32(record),
            .productCode = read_be32(record + 4),
            .revisionNumber = read_be32(record + 8),
            .serialNumber = read_be32(record + 12),
        }};
        uint8_t node_id = record[16];
        uint8_t existing;
        if (record[17] != 0 || record[18] != 0 || record[19] != 0
            || mcu_registry_check_assignment(registry, &identity, node_id) != 0
            || mcu_registry_lookup(registry, &identity, &existing) == 0) {
            return -EBADMSG;
        }
        registry->devices[registry->count++] = (McuRegistryEntry){.identity = identity, .node_id = node_id};
    }
    return 0;
}

int mcu_registry_open(McuRegistry *registry, const char *interface_name, bool allow_missing) {
    if (registry == NULL) {
        return -EINVAL;
    }
    *registry = (McuRegistry){.lock_fd = -1};
    if (interface_name == NULL || interface_name[0] == '\0' || strlen(interface_name) >= IFNAMSIZ
        || strcmp(interface_name, ".") == 0 || strcmp(interface_name, "..") == 0
        || strspn(interface_name, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")
               != strlen(interface_name)) {
        return -EINVAL;
    }
    int directory = open(MCU_REGISTRY_LOCK_DIRECTORY, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (directory < 0) {
        return -errno;
    }
    char lock_name[IFNAMSIZ + sizeof(".lock")];
    snprintf(lock_name, sizeof(lock_name), "%s.lock", interface_name);
    registry->lock_fd = openat(directory, lock_name, O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    int saved = errno;
    close(directory);
    if (registry->lock_fd < 0) {
        return -saved;
    }
    struct stat status;
    int result = 0;
    if (fstat(registry->lock_fd, &status) != 0) {
        result = -errno;
    } else if (!S_ISREG(status.st_mode) || status.st_nlink != 1 || status.st_uid != geteuid()
               || (status.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        result = -EPERM;
    } else if (flock(registry->lock_fd, LOCK_EX | LOCK_NB) != 0) {
        result = errno == EWOULDBLOCK ? -EBUSY : -errno;
    }
    if (result != 0) {
        mcu_registry_close(registry);
        return result;
    }
    directory = open(MCU_REGISTRY_DIRECTORY, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (directory < 0) {
        result = -errno;
        mcu_registry_close(registry);
        return result;
    }
    if (fstat(directory, &status) != 0) {
        result = -errno;
    } else if (status.st_uid != geteuid() || (status.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        result = -EPERM;
    }
    close(directory);
    if (result != 0) {
        mcu_registry_close(registry);
        return result;
    }
    memcpy(registry->encoded, "MCRG\0\1", 6);
    registry->entry.addr = registry->encoded;
    registry->entry.len = sizeof(registry->encoded);
    int count = snprintf(registry->entry.filename, sizeof(registry->entry.filename), "%s/%s.bin",
                         MCU_REGISTRY_DIRECTORY, interface_name);
    if (count < 0 || (size_t)count >= sizeof(registry->entry.filename)) {
        mcu_registry_close(registry);
        return -ENAMETOOLONG;
    }
    uint32_t errors = 0;
    CO_ReturnError_t storage_result = CO_storageLinux_init(&registry->storage, &registry->entry, 1, &errors,
                                                          allow_missing);
    if (storage_result != CO_ERROR_NO) {
        result = storage_result == CO_ERROR_DATA_CORRUPT ? -EBADMSG
                 : storage_result == CO_ERROR_OUT_OF_MEMORY ? -ENOMEM : -(errno != 0 ? errno : EIO);
    } else {
        result = decode_registry(registry);
    }
    if (result != 0) {
        mcu_registry_close(registry);
    }
    return result;
}

int mcu_registry_register(McuRegistry *registry, const CO_LSS_address_t *identity, uint8_t node_id) {
    int result = mcu_registry_check_assignment(registry, identity, node_id);
    if (result != 0) {
        return result;
    }
    uint8_t existing;
    if (mcu_registry_lookup(registry, identity, &existing) == 0) {
        return 0;
    }
    uint8_t *record = &registry->encoded[8 + 20 * registry->count];
    write_be32(record, identity->identity.vendorID);
    write_be32(record + 4, identity->identity.productCode);
    write_be32(record + 8, identity->identity.revisionNumber);
    write_be32(record + 12, identity->identity.serialNumber);
    record[16] = node_id;
    registry->devices[registry->count++] = (McuRegistryEntry){.identity = *identity, .node_id = node_id};
    registry->encoded[7] = (uint8_t)registry->count;
    if (CO_storageLinux_auto_process(&registry->storage, false) != 0) {
        /* Persistence may have reached rename but failed at directory fsync. Do not report success. */
        return -EIO;
    }
    return 0;
}

void mcu_registry_close(McuRegistry *registry) {
    if (registry == NULL) {
        return;
    }
    if (registry->entry.fp != NULL) {
        fclose(registry->entry.fp);
        registry->entry.fp = NULL;
    }
    if (registry->lock_fd >= 0) {
        close(registry->lock_fd);
        registry->lock_fd = -1;
    }
    registry->storage = (CO_storageLinux_t){0};
}
