#include "lss_runtime.h"

#include "301/CO_driver.h"
#include "305/CO_LSSmaster.h"

#include <errno.h>
#include <limits.h>
#include <net/if.h>
#include <sys/epoll.h>
#include <time.h>
#include <unistd.h>

typedef enum {
    LSS_RUNTIME_SELECT,
    LSS_RUNTIME_INQUIRE_NODE_ID,
} LssRuntimeOperationKind;

typedef struct {
    LssRuntimeOperationKind kind;
    CO_LSS_address_t *identity;
    uint32_t *value;
} LssRuntimeRequest;

static CO_LSSmaster_return_t
resolve_operation(CO_LSSmaster_t *master, uint32_t elapsed_us, void *context)
{
    LssRuntimeRequest *operation = context;

    if (operation->kind == LSS_RUNTIME_SELECT)
    {
        return CO_LSSmaster_swStateSelect(master, elapsed_us, operation->identity);
    }
    return CO_LSSmaster_Inquire(master, elapsed_us, CO_LSS_INQUIRE_NODE_ID,
                                operation->value);
}

static uint32_t elapsed_us(const struct timespec *before, const struct timespec *after)
{
    uint64_t elapsed = (uint64_t)(after->tv_sec - before->tv_sec) * UINT64_C(1000000);
    long nanoseconds = after->tv_nsec - before->tv_nsec;

    if (nanoseconds < 0)
    {
        elapsed -= UINT64_C(1000000);
        nanoseconds += 1000000000L;
    }
    elapsed += (uint64_t)nanoseconds / UINT64_C(1000);
    return elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
}

CO_LSSmaster_return_t lss_run_operation(CO_LSSmaster_t *master,
                                         CO_CANmodule_t *module, int epoll_fd,
                                         LssRuntimeOperation operation,
                                         void *context)
{
    struct timespec previous;
    CO_LSSmaster_return_t result;

    if (master == NULL || module == NULL || epoll_fd < 0 || operation == NULL)
    {
        return CO_LSSmaster_ILLEGAL_ARGUMENT;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &previous) != 0)
    {
        return CO_LSSmaster_SCAN_FAILED;
    }
    result = operation(master, 0u, context);
    while (result == CO_LSSmaster_WAIT_SLAVE)
    {
        struct epoll_event events[4];
        int count = epoll_wait(epoll_fd, events, 4, 10);
        struct timespec now;

        if (count < 0 && errno != EINTR)
        {
            return CO_LSSmaster_SCAN_FAILED;
        }
        for (int i = 0; i < count; ++i)
        {
            (void)CO_CANrxFromEpoll(module, &events[i], NULL, NULL);
        }
        CO_CANmodule_process(module);
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        {
            return CO_LSSmaster_SCAN_FAILED;
        }
        result = operation(master, elapsed_us(&previous, &now), context);
        previous = now;
    }
    return result;
}

int lss_resolve_node_id(const char *ifname, const CO_LSS_address_t *identity,
                        uint8_t *node_id_out)
{
    CO_CANmodule_t module = {0};
    CO_CANrx_t rx[1] = {{0}};
    CO_CANtx_t tx[1] = {{0}};
    CO_LSSmaster_t master;
    CO_CANptrSocketCan_t socket_can;
    CO_LSS_address_t selected_identity;
    LssRuntimeRequest operation;
    uint32_t node_id = 0u;
    int epoll_fd;
    int result = -1;

    if (ifname == NULL || identity == NULL || node_id_out == NULL)
    {
        return -1;
    }
    unsigned int interface_index = if_nametoindex(ifname);
    if (interface_index == 0u)
    {
        return -1;
    }
    epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd < 0)
    {
        return -1;
    }
    socket_can = (CO_CANptrSocketCan_t){.can_ifindex = (int)interface_index,
                                        .epoll_fd = epoll_fd};
    if (CO_CANmodule_init(&module, &socket_can, rx, 1u, tx, 1u, 0u) != CO_ERROR_NO ||
        CO_LSSmaster_init(&master, CO_LSSmaster_DEFAULT_TIMEOUT, &module, 0u,
                          CO_CAN_ID_LSS_SLV, &module, 0u,
                          CO_CAN_ID_LSS_MST) != CO_ERROR_NO)
    {
        CO_CANmodule_disable(&module);
        (void)close(epoll_fd);
        return -1;
    }
    CO_CANsetNormalMode(&module);
    if (!module.CANnormal)
    {
        CO_CANmodule_disable(&module);
        (void)close(epoll_fd);
        return -1;
    }

    selected_identity = *identity;
    operation = (LssRuntimeRequest){.kind = LSS_RUNTIME_SELECT,
                                    .identity = &selected_identity};
    if (lss_run_operation(&master, &module, epoll_fd, resolve_operation,
                          &operation) == CO_LSSmaster_OK)
    {
        CO_LSSmaster_return_t inquire_result;
        CO_LSSmaster_return_t deselect_result;

        operation = (LssRuntimeRequest){.kind = LSS_RUNTIME_INQUIRE_NODE_ID,
                                        .value = &node_id};
        inquire_result = lss_run_operation(&master, &module, epoll_fd,
                                            resolve_operation, &operation);
        deselect_result = CO_LSSmaster_swStateDeselect(&master);
        if (inquire_result == CO_LSSmaster_OK &&
            node_id >= 1u && node_id <= 127u &&
            deselect_result == CO_LSSmaster_OK)
        {
            *node_id_out = (uint8_t)node_id;
            result = 0;
        }
    }
    CO_CANmodule_disable(&module);
    (void)close(epoll_fd);
    return result;
}
