#ifndef GATEWAY_MCU_REGISTRY_H
#define GATEWAY_MCU_REGISTRY_H

#include "305/CO_LSS.h"
#include "CO_storageLinux.h"

#include <stdbool.h>

#define MCU_REGISTRY_DIRECTORY "/var/lib/mcu-update/devices"
#define MCU_REGISTRY_LOCK_DIRECTORY "/run/mcu-update"
#define MCU_REGISTRY_CAPACITY 127U
#define MCU_REGISTRY_ENCODED_SIZE (8U + 20U * MCU_REGISTRY_CAPACITY)

typedef struct {
    CO_LSS_address_t identity;
    uint8_t node_id;
} McuRegistryEntry;

typedef struct {
    int lock_fd;
    uint16_t count;
    McuRegistryEntry devices[MCU_REGISTRY_CAPACITY];
    uint8_t encoded[MCU_REGISTRY_ENCODED_SIZE];
    CO_storage_entry_t entry;
    CO_storageLinux_t storage;
} McuRegistry;

/* Return 0 or negative errno. The exclusive per-interface lock is held until close.
 * Only commissioning may allow a missing registry; open never creates its data file. */
int mcu_registry_open(McuRegistry *registry, const char *interface_name, bool allow_missing);
int mcu_registry_lookup(const McuRegistry *registry, const CO_LSS_address_t *identity, uint8_t *node_id);
int mcu_registry_check_assignment(const McuRegistry *registry, const CO_LSS_address_t *identity, uint8_t node_id);
/* Call only after confirming the node's active identity and node ID on the bus. */
int mcu_registry_register(McuRegistry *registry, const CO_LSS_address_t *identity, uint8_t node_id);
void mcu_registry_close(McuRegistry *registry);

#endif
