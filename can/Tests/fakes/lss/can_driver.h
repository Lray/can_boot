#ifndef CAN_DRIVER_H
#define CAN_DRIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LITTLE_ENDIAN
#define SWAP_16(value) (value)
#define SWAP_32(value) (value)
#define SWAP_64(value) (value)

typedef uint_fast8_t bool_t;

typedef struct
{
    uint16_t ident;
    uint8_t dlc;
    uint8_t data[8];
} can_rx_msg_t;

#define can_rx_msg_read_ident(message) \
    (((can_rx_msg_t *)(message))->ident)
#define can_rx_msg_read_dlc(message) \
    (((can_rx_msg_t *)(message))->dlc)
#define can_rx_msg_read_data(message) \
    (((can_rx_msg_t *)(message))->data)

typedef struct
{
    uint16_t ident;
    uint16_t mask;
    void *object;
    void (*CANrx_callback)(void *object, void *message);
} can_rx_t;

typedef struct
{
    uint32_t ident;
    uint8_t DLC;
    uint8_t data[8];
    volatile bool_t bufferFull;
    volatile bool_t syncFlag;
} can_tx_t;

typedef struct
{
    can_rx_t *rxArray;
    uint16_t rxSize;
    can_tx_t *txArray;
    uint16_t txSize;
} can_module_t;

typedef enum
{
    ERROR_NO = 0,
    ERROR_ILLEGAL_ARGUMENT = -1,
    ERROR_DATA_CORRUPT = -13,
    ERROR_TX_OVERFLOW = -9
} can_return_error_t;

#define FLAG_READ(flag) ((flag) != NULL)
#define FLAG_SET(flag) ((flag) = (void *)1L)
#define FLAG_CLEAR(flag) ((flag) = NULL)

#define CAN_ID_LSS_SLV 0x7E4U
#define CAN_ID_LSS_MST 0x7E5U

can_return_error_t can_rx_buffer_init(
    can_module_t *module,
    uint16_t index,
    uint16_t ident,
    uint16_t mask,
    bool_t rtr,
    void *object,
    void (*callback)(void *object, void *message));
can_tx_t *can_tx_buffer_init(can_module_t *module,
                             uint16_t index,
                             uint16_t ident,
                             bool_t rtr,
                             uint8_t length,
                             bool_t sync);
can_return_error_t can_send(can_module_t *module, can_tx_t *buffer);

#endif /* CAN_DRIVER_H */
