/*
 * Linux SocketCAN interface for CANopenNode.
 *
 * Adapted from CANopenLinux for a single-interface LSS master. See UPSTREAM.md.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "301/CO_driver.h"

#include <errno.h>
#include <linux/can/raw.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static CO_ReturnError_t
disableRx(CO_CANmodule_t* CANmodule) {
    if (setsockopt(CANmodule->CANinterfaces[0].fd, SOL_CAN_RAW, CAN_RAW_FILTER, NULL, 0) < 0) {
        return CO_ERROR_SYSCALL;
    }
    return CO_ERROR_NO;
}

static CO_ReturnError_t
setRxFilters(CO_CANmodule_t* CANmodule) {
    struct can_filter filters[CANmodule->rxSize];
    size_t count = 0;

    for (size_t i = 0; i < CANmodule->rxSize; i++) {
        if (CANmodule->rxFilter[i].can_id != 0 || CANmodule->rxFilter[i].can_mask != 0) {
            filters[count++] = CANmodule->rxFilter[i];
        }
    }
    if (count == 0) {
        return disableRx(CANmodule);
    }
    if (setsockopt(CANmodule->CANinterfaces[0].fd, SOL_CAN_RAW, CAN_RAW_FILTER, filters,
                   count * sizeof(filters[0])) < 0) {
        return CO_ERROR_SYSCALL;
    }
    return CO_ERROR_NO;
}

static CO_ReturnError_t
addInterface(CO_CANmodule_t* CANmodule, int can_ifindex) {
    struct sockaddr_can address = {0};
    struct epoll_event event = {0};
    CO_CANinterface_t* interface = calloc(1, sizeof(*interface));

    if (interface == NULL) {
        return CO_ERROR_OUT_OF_MEMORY;
    }
    interface->fd = -1;
    interface->can_ifindex = can_ifindex;
    if (if_indextoname(can_ifindex, interface->ifName) == NULL) {
        free(interface);
        return CO_ERROR_ILLEGAL_ARGUMENT;
    }

    interface->fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (interface->fd < 0) {
        free(interface);
        return CO_ERROR_SYSCALL;
    }

    address.can_family = AF_CAN;
    address.can_ifindex = can_ifindex;
    if (bind(interface->fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        close(interface->fd);
        free(interface);
        return CO_ERROR_SYSCALL;
    }

    event.events = EPOLLIN;
    event.data.fd = interface->fd;
    if (epoll_ctl(CANmodule->epoll_fd, EPOLL_CTL_ADD, interface->fd, &event) < 0) {
        close(interface->fd);
        free(interface);
        return CO_ERROR_SYSCALL;
    }

    CANmodule->CANinterfaces = interface;
    CANmodule->CANinterfaceCount = 1;
    return disableRx(CANmodule);
}

void
CO_CANsetConfigurationMode(void* CANptr) {
    (void)CANptr;
}

void
CO_CANsetNormalMode(CO_CANmodule_t* CANmodule) {
    if (CANmodule != NULL && CANmodule->CANinterfaceCount == 1 && setRxFilters(CANmodule) == CO_ERROR_NO) {
        CANmodule->CANnormal = true;
    }
}

CO_ReturnError_t
CO_CANmodule_init(CO_CANmodule_t* CANmodule, void* CANptr, CO_CANrx_t rxArray[], uint16_t rxSize, CO_CANtx_t txArray[],
                  uint16_t txSize, uint16_t CANbitRate) {
    CO_CANptrSocketCan_t* socketCan = (CO_CANptrSocketCan_t*)CANptr;
    (void)CANbitRate;

    if (CANmodule == NULL || socketCan == NULL || rxArray == NULL || rxSize == 0 || txArray == NULL || txSize == 0
        || socketCan->can_ifindex == 0 || socketCan->epoll_fd < 0) {
        return CO_ERROR_ILLEGAL_ARGUMENT;
    }

    memset(CANmodule, 0, sizeof(*CANmodule));
    CANmodule->epoll_fd = socketCan->epoll_fd;
    CANmodule->rxArray = rxArray;
    CANmodule->rxSize = rxSize;
    CANmodule->txArray = txArray;
    CANmodule->txSize = txSize;
    CANmodule->rxFilter = calloc(rxSize, sizeof(*CANmodule->rxFilter));
    if (CANmodule->rxFilter == NULL) {
        return CO_ERROR_OUT_OF_MEMORY;
    }

    for (uint16_t i = 0; i < rxSize; i++) {
        rxArray[i].mask = UINT32_MAX;
    }
    CO_ReturnError_t error = addInterface(CANmodule, socketCan->can_ifindex);
    if (error != CO_ERROR_NO) {
        CO_CANmodule_disable(CANmodule);
    }
    return error;
}

void
CO_CANmodule_disable(CO_CANmodule_t* CANmodule) {
    if (CANmodule == NULL) {
        return;
    }
    CANmodule->CANnormal = false;
    if (CANmodule->CANinterfaceCount == 1 && CANmodule->CANinterfaces != NULL) {
        epoll_ctl(CANmodule->epoll_fd, EPOLL_CTL_DEL, CANmodule->CANinterfaces[0].fd, NULL);
        close(CANmodule->CANinterfaces[0].fd);
    }
    free(CANmodule->CANinterfaces);
    free(CANmodule->rxFilter);
    CANmodule->CANinterfaces = NULL;
    CANmodule->rxFilter = NULL;
    CANmodule->CANinterfaceCount = 0;
}

CO_ReturnError_t
CO_CANrxBufferInit(CO_CANmodule_t* CANmodule, uint16_t index, uint16_t ident, uint16_t mask, bool_t rtr, void* object,
                   void (*CANrx_callback)(void* object, void* message)) {
    if (CANmodule == NULL || index >= CANmodule->rxSize) {
        return CO_ERROR_ILLEGAL_ARGUMENT;
    }

    CO_CANrx_t* buffer = &CANmodule->rxArray[index];
    buffer->object = object;
    buffer->CANrx_callback = CANrx_callback;
    buffer->ident = ident & CAN_SFF_MASK;
    if (rtr) {
        buffer->ident |= CAN_RTR_FLAG;
    }
    buffer->mask = (mask & CAN_SFF_MASK) | CAN_EFF_FLAG | CAN_RTR_FLAG;
    CANmodule->rxFilter[index].can_id = buffer->ident;
    CANmodule->rxFilter[index].can_mask = buffer->mask;
    return CANmodule->CANnormal ? setRxFilters(CANmodule) : CO_ERROR_NO;
}

CO_CANtx_t*
CO_CANtxBufferInit(CO_CANmodule_t* CANmodule, uint16_t index, uint16_t ident, bool_t rtr, uint8_t noOfBytes,
                   bool_t syncFlag) {
    if (CANmodule == NULL || index >= CANmodule->txSize || noOfBytes > CAN_MAX_DLEN) {
        return NULL;
    }

    CO_CANtx_t* buffer = &CANmodule->txArray[index];
    buffer->ident = ident & CAN_SFF_MASK;
    if (rtr) {
        buffer->ident |= CAN_RTR_FLAG;
    }
    buffer->DLC = noOfBytes;
    buffer->bufferFull = false;
    buffer->syncFlag = syncFlag;
    return buffer;
}

CO_ReturnError_t
CO_CANsend(CO_CANmodule_t* CANmodule, CO_CANtx_t* buffer) {
    if (CANmodule == NULL || buffer == NULL || CANmodule->CANinterfaceCount != 1 || !CANmodule->CANnormal) {
        return CO_ERROR_ILLEGAL_ARGUMENT;
    }

    CO_ReturnError_t error = buffer->bufferFull ? CO_ERROR_TX_OVERFLOW : CO_ERROR_NO;
    ssize_t written;
    do {
        written = send(CANmodule->CANinterfaces[0].fd, buffer, CAN_MTU, MSG_DONTWAIT);
    } while (written < 0 && errno == EINTR);

    if (written == CAN_MTU) {
        if (buffer->bufferFull) {
            buffer->bufferFull = false;
            CANmodule->CANtxCount--;
        }
        return error;
    }
    if (errno == EAGAIN || errno == ENOBUFS) {
        if (!buffer->bufferFull) {
            buffer->bufferFull = true;
            CANmodule->CANtxCount++;
        }
        return CO_ERROR_TX_BUSY;
    }
    return CO_ERROR_SYSCALL;
}

void
CO_CANclearPendingSyncPDOs(CO_CANmodule_t* CANmodule) {
    (void)CANmodule;
}

void
CO_CANmodule_process(CO_CANmodule_t* CANmodule) {
    if (CANmodule == NULL || CANmodule->CANtxCount == 0) {
        return;
    }
    for (uint16_t i = 0; i < CANmodule->txSize; i++) {
        CO_CANtx_t* buffer = &CANmodule->txArray[i];
        if (buffer->bufferFull) {
            buffer->bufferFull = false;
            CANmodule->CANtxCount--;
            (void)CO_CANsend(CANmodule, buffer);
            return;
        }
    }
    CANmodule->CANtxCount = 0;
}

static int32_t
dispatch(CO_CANmodule_t* CANmodule, struct can_frame* frame, CO_CANrxMsg_t* copy) {
    CO_CANrxMsg_t* message = (CO_CANrxMsg_t*)frame;

    for (uint16_t i = 0; i < CANmodule->rxSize; i++) {
        CO_CANrx_t* buffer = &CANmodule->rxArray[i];
        if (((message->ident ^ buffer->ident) & buffer->mask) == 0) {
            if (buffer->CANrx_callback != NULL) {
                buffer->CANrx_callback(buffer->object, message);
            }
            if (copy != NULL) {
                memcpy(copy, message, sizeof(*copy));
            }
            return (int32_t)i;
        }
    }
    return -1;
}

bool_t
CO_CANrxFromEpoll(CO_CANmodule_t* CANmodule, struct epoll_event* event, CO_CANrxMsg_t* buffer, int32_t* msgIndex) {
    if (CANmodule == NULL || event == NULL || CANmodule->CANinterfaceCount != 1
        || event->data.fd != CANmodule->CANinterfaces[0].fd) {
        return false;
    }

    if ((event->events & EPOLLIN) != 0) {
        struct can_frame frame;
        ssize_t received = recv(event->data.fd, &frame, sizeof(frame), MSG_DONTWAIT);
        if (received == CAN_MTU && CANmodule->CANnormal && (frame.can_id & CAN_ERR_FLAG) == 0) {
            int32_t index = dispatch(CANmodule, &frame, buffer);
            if (msgIndex != NULL) {
                *msgIndex = index;
            }
        }
    }
    return true;
}
