#include "305/CO_LSSmaster.h"
#include "lss_runtime.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
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

static CO_LSSmaster_return_t
callOperation(CO_LSSmaster_t* master, uint32_t elapsedUs, void* context) {
    operation_t* operation = context;
    switch (operation->kind) {
        case OP_FASTSCAN: return CO_LSSmaster_IdentifyFastscan(master, elapsedUs, operation->fastscan);
        case OP_SELECT: return CO_LSSmaster_swStateSelect(master, elapsedUs, operation->address);
        case OP_INQUIRE_NODE_ID:
            return CO_LSSmaster_Inquire(master, elapsedUs, CO_LSS_INQUIRE_NODE_ID, operation->value);
        case OP_CONFIGURE_NODE_ID: return CO_LSSmaster_configureNodeId(master, elapsedUs, operation->nodeId);
        case OP_STORE: return CO_LSSmaster_configureStore(master, elapsedUs);
    }
    return CO_LSSmaster_ILLEGAL_ARGUMENT;
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

    int epollFd = epoll_create1(EPOLL_CLOEXEC);
    if (epollFd < 0) {
        perror("epoll_create1");
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
        return EXIT_FAILURE;
    }

    driverError = CO_LSSmaster_init(&master, CO_LSSmaster_DEFAULT_TIMEOUT, &module, 0, CO_CAN_ID_LSS_SLV, &module, 0,
                                    CO_CAN_ID_LSS_MST);
    if (driverError != CO_ERROR_NO) {
        fprintf(stderr, "CO_LSSmaster_init failed: %d\n", driverError);
        CO_CANmodule_disable(&module);
        close(epollFd);
        return EXIT_FAILURE;
    }
    CO_CANsetNormalMode(&module);
    if (!module.CANnormal) {
        fprintf(stderr, "Unable to enable SocketCAN reception\n");
        CO_CANmodule_disable(&module);
        close(epollFd);
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

    CO_LSSmaster_return_t lssResult = lss_run_operation(&master, &module, epollFd, callOperation, &operation);
    if (lssResult != CO_LSSmaster_OK && lssResult != CO_LSSmaster_SCAN_FINISHED) {
        fprintf(stderr, "LSS selection failed: %d\n", lssResult);
        CO_CANmodule_disable(&module);
        close(epollFd);
        return EXIT_FAILURE;
    }

    if (fastscanMode) {
        address = scan.found;
        printf("Selected %08" PRIX32 ":%08" PRIX32 ":%08" PRIX32 ":%08" PRIX32 "\n",
               address.identity.vendorID, address.identity.productCode, address.identity.revisionNumber,
               address.identity.serialNumber);
    }

    uint32_t previousNodeId = 0;
    operation = (operation_t){.kind = OP_INQUIRE_NODE_ID, .value = &previousNodeId};
    lssResult = lss_run_operation(&master, &module, epollFd, callOperation, &operation);
    if (lssResult == CO_LSSmaster_OK) {
        printf("Current node-ID: %" PRIu32 "\n", previousNodeId);
        operation = (operation_t){.kind = OP_CONFIGURE_NODE_ID, .nodeId = (uint8_t)nodeId};
        lssResult = lss_run_operation(&master, &module, epollFd, callOperation, &operation);
    }
    if (lssResult == CO_LSSmaster_OK) {
        operation = (operation_t){.kind = OP_STORE};
        lssResult = lss_run_operation(&master, &module, epollFd, callOperation, &operation);
    }

    CO_LSSmaster_return_t deselectResult = CO_LSSmaster_swStateDeselect(&master);
    if (lssResult != CO_LSSmaster_OK || deselectResult != CO_LSSmaster_OK) {
        fprintf(stderr, "LSS configuration failed: operation=%d deselect=%d\n", lssResult, deselectResult);
        CO_CANmodule_disable(&module);
        close(epollFd);
        return EXIT_FAILURE;
    }

    printf("Configured and stored node-ID %" PRIu32 "\n", nodeId);
    CO_CANmodule_disable(&module);
    close(epollFd);
    return EXIT_SUCCESS;
}
