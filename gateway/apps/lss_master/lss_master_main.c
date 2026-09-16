#include "305/CO_LSSmaster.h"
#include "mcu_registry.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <time.h>
#include <unistd.h>

typedef enum {
    OP_FASTSCAN,
    OP_SELECT,
    OP_INQUIRE_NODE_ID,
    OP_CONFIGURE_NODE_ID,
    OP_STORE,
} operation_kind_t;

typedef struct {
    operation_kind_t kind;
    CO_LSSmaster_fastscan_t* fastscan;
    CO_LSS_address_t* address;
    uint32_t* value;
    uint8_t nodeId;
} operation_t;

static uint32_t
elapsedUs(const struct timespec* before, const struct timespec* after) {
    uint64_t elapsed = (uint64_t)(after->tv_sec - before->tv_sec) * UINT64_C(1000000);
    long nanoseconds = after->tv_nsec - before->tv_nsec;
    if (nanoseconds < 0) {
        elapsed -= UINT64_C(1000000);
        nanoseconds += 1000000000L;
    }
    elapsed += (uint64_t)nanoseconds / UINT64_C(1000);
    return elapsed > UINT32_MAX ? UINT32_MAX : (uint32_t)elapsed;
}

static CO_LSSmaster_return_t
runOperation(CO_LSSmaster_t* master, CO_CANmodule_t* module, int epollFd, operation_t* operation) {
    struct timespec previous;
    if (clock_gettime(CLOCK_MONOTONIC, &previous) != 0) {
        return CO_LSSmaster_SCAN_FAILED;
    }

    uint32_t elapsed = 0;
    for (;;) {
        CO_LSSmaster_return_t result;
        switch (operation->kind) {
            case OP_FASTSCAN: result = CO_LSSmaster_IdentifyFastscan(master, elapsed, operation->fastscan); break;
            case OP_SELECT: result = CO_LSSmaster_swStateSelect(master, elapsed, operation->address); break;
            case OP_INQUIRE_NODE_ID:
                result = CO_LSSmaster_Inquire(master, elapsed, CO_LSS_INQUIRE_NODE_ID, operation->value); break;
            case OP_CONFIGURE_NODE_ID: result = CO_LSSmaster_configureNodeId(master, elapsed, operation->nodeId); break;
            case OP_STORE: result = CO_LSSmaster_configureStore(master, elapsed); break;
            default: return CO_LSSmaster_ILLEGAL_ARGUMENT;
        }
        if (module->CANerrorStatus != 0) return CO_LSSmaster_SCAN_FAILED;
        if (result != CO_LSSmaster_WAIT_SLAVE) return result;
        struct epoll_event events[4];
        int count = epoll_wait(epollFd, events, 4, 10);
        if (count < 0 && errno != EINTR) {
            return CO_LSSmaster_SCAN_FAILED;
        }
        for (int i = 0; i < count; i++) {
            if ((events[i].events & (EPOLLERR | EPOLLHUP)) != 0) return CO_LSSmaster_SCAN_FAILED;
            (void)CO_CANrxFromEpoll(module, &events[i], NULL, NULL);
        }
        CO_CANmodule_process(module);

        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
            return CO_LSSmaster_SCAN_FAILED;
        }
        elapsed = elapsedUs(&previous, &now);
        previous = now;
    }
}

static bool
parseUnsigned(const char* text, uint32_t maximum, uint32_t* value) {
    char* end;
    errno = 0;
    unsigned long parsed = strtoul(text, &end, 0);
    if (errno != 0 || *text == '\0' || *end != '\0' || parsed > maximum) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

static void
printUsage(const char* program) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s fastscan <can-iface> <new-node-id>\n", program);
    fprintf(stderr, "  %s select <can-iface> <vendor-id> <product-code> <revision> <serial> <new-node-id>\n",
            program);
}

int
main(int argc, char* argv[]) {
    bool fastscanMode = argc == 4 && strcmp(argv[1], "fastscan") == 0;
    bool selectMode = argc == 8 && strcmp(argv[1], "select") == 0;
    if (!fastscanMode && !selectMode) {
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }

    const char* interfaceName = argv[2];
    uint32_t nodeId;
    CO_LSS_address_t address = {0};
    if (!parseUnsigned(argv[argc - 1], 127, &nodeId) || nodeId == 0) {
        fprintf(stderr, "new-node-id must be in range 1..127\n");
        return EXIT_FAILURE;
    }
    if (selectMode
        && (!parseUnsigned(argv[3], UINT32_MAX, &address.identity.vendorID)
            || !parseUnsigned(argv[4], UINT32_MAX, &address.identity.productCode)
            || !parseUnsigned(argv[5], UINT32_MAX, &address.identity.revisionNumber)
            || !parseUnsigned(argv[6], UINT32_MAX, &address.identity.serialNumber))) {
        fprintf(stderr, "LSS identity fields must be unsigned 32-bit integers\n");
        return EXIT_FAILURE;
    }

    unsigned int interfaceIndex = if_nametoindex(interfaceName);
    if (interfaceIndex == 0) {
        fprintf(stderr, "Unknown CAN interface '%s'\n", interfaceName);
        return EXIT_FAILURE;
    }

    McuRegistry registry;
    int registryResult = mcu_registry_open(&registry, interfaceName, true);
    if (registryResult != 0) {
        fprintf(stderr, "Cannot open registry or acquire bus lock: %d\n", registryResult);
        return EXIT_FAILURE;
    }
    int epollFd = epoll_create1(EPOLL_CLOEXEC);
    if (epollFd < 0) {
        perror("epoll_create1");
        mcu_registry_close(&registry);
        return EXIT_FAILURE;
    }

    CO_CANmodule_t module;
    CO_CANrx_t rx[1];
    CO_CANtx_t tx[1];
    CO_LSSmaster_t master;
    CO_CANptrSocketCan_t socketCan = {.can_ifindex = (int)interfaceIndex, .epoll_fd = epollFd};
    CO_ReturnError_t driverError = CO_CANmodule_init(&module, &socketCan, rx, 1, tx, 1, 0);
    if (driverError != CO_ERROR_NO) {
        fprintf(stderr, "CO_CANmodule_init failed: %d\n", driverError);
        close(epollFd);
        mcu_registry_close(&registry);
        return EXIT_FAILURE;
    }

    driverError = CO_LSSmaster_init(&master, CO_LSSmaster_DEFAULT_TIMEOUT, &module, 0, CO_CAN_ID_LSS_SLV, &module, 0,
                                    CO_CAN_ID_LSS_MST);
    if (driverError != CO_ERROR_NO) {
        fprintf(stderr, "CO_LSSmaster_init failed: %d\n", driverError);
        CO_CANmodule_disable(&module);
        close(epollFd);
        mcu_registry_close(&registry);
        return EXIT_FAILURE;
    }
    CO_CANsetNormalMode(&module);
    if (!module.CANnormal) {
        fprintf(stderr, "Unable to enable SocketCAN reception\n");
        CO_CANmodule_disable(&module);
        close(epollFd);
        mcu_registry_close(&registry);
        return EXIT_FAILURE;
    }

    CO_LSSmaster_fastscan_t scan = {0};
    operation_t operation = {0};
    if (fastscanMode) {
        for (size_t i = 0; i < 4; i++) {
            scan.scan[i] = CO_LSSmaster_FS_SCAN;
        }
        operation.kind = OP_FASTSCAN;
        operation.fastscan = &scan;
    } else {
        operation.kind = OP_SELECT;
        operation.address = &address;
    }

    CO_LSSmaster_return_t lssResult = runOperation(&master, &module, epollFd, &operation);
    if (lssResult != CO_LSSmaster_OK && lssResult != CO_LSSmaster_SCAN_FINISHED) {
        fprintf(stderr, "LSS selection failed: %d\n", lssResult);
        CO_CANmodule_disable(&module);
        close(epollFd);
        mcu_registry_close(&registry);
        return EXIT_FAILURE;
    }

    if (fastscanMode) {
        address = scan.found;
        printf("Selected %08" PRIX32 ":%08" PRIX32 ":%08" PRIX32 ":%08" PRIX32 "\n",
               address.identity.vendorID, address.identity.productCode, address.identity.revisionNumber,
               address.identity.serialNumber);
    }

    uint32_t previousNodeId = 0;
    registryResult = mcu_registry_check_assignment(&registry, &address, (uint8_t)nodeId);
    if (registryResult != 0) {
        fprintf(stderr, "Identity/node-ID registry conflict: %d\n", registryResult);
        (void)CO_LSSmaster_swStateDeselect(&master);
        CO_CANmodule_disable(&module);
        close(epollFd);
        mcu_registry_close(&registry);
        return EXIT_FAILURE;
    }
    operation = (operation_t){.kind = OP_INQUIRE_NODE_ID, .value = &previousNodeId};
    lssResult = runOperation(&master, &module, epollFd, &operation);
    if (lssResult == CO_LSSmaster_OK) {
        printf("Current node-ID: %" PRIu32 "\n", previousNodeId);
        if (previousNodeId != CO_LSS_NODE_ID_ASSIGNMENT && previousNodeId != nodeId) {
            fprintf(stderr, "Configured node-ID differs; reset/recommission explicitly before registration\n");
            lssResult = CO_LSSmaster_ILLEGAL_ARGUMENT;
        } else if (previousNodeId == CO_LSS_NODE_ID_ASSIGNMENT) {
            operation = (operation_t){.kind = OP_CONFIGURE_NODE_ID, .nodeId = (uint8_t)nodeId};
            lssResult = runOperation(&master, &module, epollFd, &operation);
        }
    }
    if (lssResult == CO_LSSmaster_OK) {
        operation = (operation_t){.kind = OP_STORE};
        lssResult = runOperation(&master, &module, epollFd, &operation);
    }

    CO_LSSmaster_return_t deselectResult = CO_LSSmaster_swStateDeselect(&master);
    if (lssResult != CO_LSSmaster_OK || deselectResult != CO_LSSmaster_OK || module.CANerrorStatus != 0) {
        fprintf(stderr, "LSS configuration failed: operation=%d deselect=%d\n", lssResult, deselectResult);
        CO_CANmodule_disable(&module);
        close(epollFd);
        mcu_registry_close(&registry);
        return EXIT_FAILURE;
    }

    /* An unconfigured slave activates its first assignment on deselect.
     * Wait for its communication reset before checking the active assignment. */
    struct timespec settle = {.tv_sec = 0, .tv_nsec = 100000000L};
    while (nanosleep(&settle, &settle) != 0 && errno == EINTR) {}
    operation = (operation_t){.kind = OP_SELECT, .address = &address};
    lssResult = runOperation(&master, &module, epollFd, &operation);
    uint32_t activeNodeId = 0;
    if (lssResult == CO_LSSmaster_OK) {
        operation = (operation_t){.kind = OP_INQUIRE_NODE_ID, .value = &activeNodeId};
        lssResult = runOperation(&master, &module, epollFd, &operation);
    }
    deselectResult = CO_LSSmaster_swStateDeselect(&master);
    int result = EXIT_FAILURE;
    if (lssResult == CO_LSSmaster_OK && deselectResult == CO_LSSmaster_OK &&
        module.CANerrorStatus == 0 && activeNodeId == nodeId &&
        mcu_registry_register(&registry, &address, (uint8_t)nodeId) == 0) {
        printf("Configured, stored and registered node-ID %" PRIu32 "\n", nodeId);
        result = EXIT_SUCCESS;
    } else {
        fprintf(stderr, "Active assignment verification or registry persistence failed\n");
    }
    CO_CANmodule_disable(&module);
    close(epollFd);
    mcu_registry_close(&registry);
    return result;
}
