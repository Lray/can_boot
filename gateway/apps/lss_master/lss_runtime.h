#ifndef LSS_RUNTIME_H
#define LSS_RUNTIME_H

#include <stdint.h>

#include "301/CO_driver.h"
#include "305/CO_LSS.h"
#include "305/CO_LSSmaster.h"

typedef CO_LSSmaster_return_t (*LssRuntimeOperation)(CO_LSSmaster_t *master,
                                                      uint32_t elapsed_us,
                                                      void *context);

CO_LSSmaster_return_t lss_run_operation(CO_LSSmaster_t *master,
                                         CO_CANmodule_t *module, int epoll_fd,
                                         LssRuntimeOperation operation,
                                         void *context);

int lss_resolve_node_id(const char *ifname, const CO_LSS_address_t *identity,
                        uint8_t *node_id_out);

#endif
