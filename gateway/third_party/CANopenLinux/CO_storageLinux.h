/**
 * Standalone Linux file persistence adapted from CANopenLinux.
 *
 * This variant intentionally has no Object Dictionary or CO_storage dependency.
 * See UPSTREAM.md.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CO_STORAGE_LINUX_H
#define CO_STORAGE_LINUX_H

#include "301/CO_driver.h"

typedef struct {
    CO_storage_entry_t* entries;
    size_t entriesCount;
} CO_storageLinux_t;

CO_ReturnError_t CO_storageLinux_init(CO_storageLinux_t* storage, CO_storage_entry_t* entries, size_t entriesCount,
                                      uint32_t* storageInitError);
uint32_t CO_storageLinux_auto_process(CO_storageLinux_t* storage, bool_t closeFiles);

#endif
