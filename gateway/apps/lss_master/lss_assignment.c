#include "lss_assignment.h"

#include <string.h>

#include "CO_storageLinux.h"

#ifndef LSS_ASSIGNMENT_FILE
#define LSS_ASSIGNMENT_FILE "/etc/mcu-update/lss-assignments.bin"
#endif

#define LSS_ASSIGNMENT_CAPACITY 127u

typedef struct
{
    CO_LSS_address_t identity;
    uint8_t node_id;
} LssAssignment;

typedef struct
{
    uint8_t count;
    LssAssignment entries[LSS_ASSIGNMENT_CAPACITY];
} LssAssignmentTable;

static int assignment_open(LssAssignmentTable *table, CO_storageLinux_t *storage,
                           CO_storage_entry_t *entry)
{
    uint32_t errors = 0u;

    memset(table, 0, sizeof(*table));
    memset(entry, 0, sizeof(*entry));
    entry->addr = table;
    entry->len = sizeof(*table);
    (void)strncpy(entry->filename, LSS_ASSIGNMENT_FILE, sizeof(entry->filename) - 1u);
    if (CO_storageLinux_init(storage, entry, 1u, &errors) != CO_ERROR_NO ||
        errors != 0u || table->count > LSS_ASSIGNMENT_CAPACITY)
    {
        return -1;
    }
    return 0;
}

static uint32_t assignment_close(CO_storageLinux_t *storage)
{
    return CO_storageLinux_auto_process(storage, true);
}

int lss_assignment_find(const CO_LSS_address_t *identity, uint8_t *node_id_out)
{
    LssAssignmentTable table;
    CO_storageLinux_t storage;
    CO_storage_entry_t entry;
    size_t index;
    int result = -1;

    if (identity == NULL || node_id_out == NULL || assignment_open(&table, &storage, &entry) != 0)
    {
        return -1;
    }
    for (index = 0u; index < table.count; ++index)
    {
        if (CO_LSS_ADDRESS_EQUAL(table.entries[index].identity, *identity) &&
            table.entries[index].node_id >= 1u &&
            table.entries[index].node_id <= LSS_ASSIGNMENT_CAPACITY)
        {
            *node_id_out = table.entries[index].node_id;
            result = 0;
            break;
        }
    }
    if (assignment_close(&storage) != 0u)
    {
        result = -1;
    }
    return result;
}

int lss_assignment_store(const CO_LSS_address_t *identity, uint8_t node_id)
{
    LssAssignmentTable table;
    CO_storageLinux_t storage;
    CO_storage_entry_t entry;
    size_t index;
    size_t matching = LSS_ASSIGNMENT_CAPACITY;

    if (identity == NULL || node_id < 1u || node_id > LSS_ASSIGNMENT_CAPACITY ||
        assignment_open(&table, &storage, &entry) != 0)
    {
        return -1;
    }
    for (index = 0u; index < table.count; ++index)
    {
        if (CO_LSS_ADDRESS_EQUAL(table.entries[index].identity, *identity))
        {
            matching = index;
        }
        else if (table.entries[index].node_id == node_id)
        {
            (void)assignment_close(&storage);
            return -1;
        }
    }
    if (matching < table.count)
    {
        table.entries[matching].node_id = node_id;
        return assignment_close(&storage) == 0u ? 0 : -1;
    }
    if (table.count == LSS_ASSIGNMENT_CAPACITY)
    {
        (void)assignment_close(&storage);
        return -1;
    }
    table.entries[table.count].identity = *identity;
    table.entries[table.count].node_id = node_id;
    ++table.count;
    return CO_storageLinux_auto_process(&storage, true) == 0u ? 0 : -1;
}
