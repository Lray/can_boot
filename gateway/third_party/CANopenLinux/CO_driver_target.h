/**
 * Minimal Linux SocketCAN target definitions for CANopenNode.
 *
 * Adapted from CANopenLinux for the Gateway LSS master. See UPSTREAM.md.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CO_DRIVER_TARGET_H
#define CO_DRIVER_TARGET_H

#include <endian.h>
#include <linux/can.h>
#include <net/if.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/epoll.h>

#define CO_CONFIG_LSS CO_CONFIG_LSS_MASTER
#define CO_CONFIG_CRC16 CO_CONFIG_CRC16_ENABLE

#define CO_DRIVER_MULTI_INTERFACE 0
#define CO_DRIVER_ERROR_REPORTING 0

#if __BYTE_ORDER == __LITTLE_ENDIAN
#define CO_LITTLE_ENDIAN
#define CO_SWAP_16(x) (x)
#define CO_SWAP_32(x) (x)
#define CO_SWAP_64(x) (x)
#else
#define CO_BIG_ENDIAN
#include <byteswap.h>
#define CO_SWAP_16(x) bswap_16(x)
#define CO_SWAP_32(x) bswap_32(x)
#define CO_SWAP_64(x) bswap_64(x)
#endif

typedef uint_fast8_t bool_t;
typedef float float32_t;
typedef double float64_t;

typedef struct {
    uint32_t ident;
    uint8_t DLC;
    uint8_t padding[3];
    uint8_t data[8];
} CO_CANrxMsg_t;

static inline uint16_t
CO_CANrxMsg_readIdent(void* rxMsg) {
    const CO_CANrxMsg_t* msg = (const CO_CANrxMsg_t*)rxMsg;
    return (uint16_t)(msg->ident & CAN_SFF_MASK);
}

static inline uint8_t
CO_CANrxMsg_readDLC(void* rxMsg) {
    const CO_CANrxMsg_t* msg = (const CO_CANrxMsg_t*)rxMsg;
    return msg->DLC;
}

static inline const uint8_t*
CO_CANrxMsg_readData(void* rxMsg) {
    const CO_CANrxMsg_t* msg = (const CO_CANrxMsg_t*)rxMsg;
    return msg->data;
}

typedef struct {
    uint32_t ident;
    uint32_t mask;
    void* object;
    void (*CANrx_callback)(void* object, void* message);
} CO_CANrx_t;

/* The first CAN_MTU bytes are binary-compatible with struct can_frame. */
typedef struct {
    uint32_t ident;
    uint8_t DLC;
    uint8_t padding[3];
    uint8_t data[8];
    volatile bool_t bufferFull;
    volatile bool_t syncFlag;
} CO_CANtx_t;

typedef struct {
    int can_ifindex;
    int epoll_fd;
} CO_CANptrSocketCan_t;

typedef struct {
    int can_ifindex;
    char ifName[IFNAMSIZ];
    int fd;
} CO_CANinterface_t;

typedef struct {
    CO_CANinterface_t* CANinterfaces;
    uint32_t CANinterfaceCount;
    CO_CANrx_t* rxArray;
    uint16_t rxSize;
    struct can_filter* rxFilter;
    CO_CANtx_t* txArray;
    uint16_t txSize;
    uint16_t CANerrorStatus;
    volatile bool_t CANnormal;
    volatile uint16_t CANtxCount;
    int epoll_fd;
} CO_CANmodule_t;

#ifndef CO_STORAGE_PATH_MAX
#define CO_STORAGE_PATH_MAX 255
#endif

typedef struct {
    void* addr;
    size_t len;
    char filename[CO_STORAGE_PATH_MAX];
    uint16_t crc;
    FILE* fp;
} CO_storage_entry_t;

#define CO_LOCK_CAN_SEND(CAN_MODULE) ((void)(CAN_MODULE))
#define CO_UNLOCK_CAN_SEND(CAN_MODULE) ((void)(CAN_MODULE))
#define CO_LOCK_EMCY(CAN_MODULE) ((void)(CAN_MODULE))
#define CO_UNLOCK_EMCY(CAN_MODULE) ((void)(CAN_MODULE))
#define CO_LOCK_OD(CAN_MODULE) ((void)(CAN_MODULE))
#define CO_UNLOCK_OD(CAN_MODULE) ((void)(CAN_MODULE))

#define CO_FLAG_READ(rxNew) ((rxNew) != NULL)
#define CO_FLAG_SET(rxNew)                                                                                             \
    do {                                                                                                               \
        __sync_synchronize();                                                                                          \
        (rxNew) = (void*)1L;                                                                                           \
    } while (0)
#define CO_FLAG_CLEAR(rxNew)                                                                                           \
    do {                                                                                                               \
        __sync_synchronize();                                                                                          \
        (rxNew) = NULL;                                                                                                \
    } while (0)

bool_t CO_CANrxFromEpoll(CO_CANmodule_t* CANmodule, struct epoll_event* event, CO_CANrxMsg_t* buffer,
                         int32_t* msgIndex);

#endif
