#ifndef LSS_ASSIGNMENT_H
#define LSS_ASSIGNMENT_H

#include <stdint.h>

#include "305/CO_LSS.h"

int lss_assignment_find(const CO_LSS_address_t *identity, uint8_t *node_id_out);
int lss_assignment_store(const CO_LSS_address_t *identity, uint8_t node_id);

#endif
