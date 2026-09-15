#ifndef CO_DRIVER_H
#define CO_DRIVER_H

/* CANopenNode LSS-to-project driver ABI adapter. */
#include "can_driver.h"

#define CO_CONFIG_FLAG_CALLBACK_PRE                    0x1000U
#define CO_CONFIG_LSS_SLAVE                            0x01U
#define CO_CONFIG_LSS_SLAVE_FASTSCAN_DIRECT_RESPOND    0x02U
#define CO_CONFIG_LSS_MASTER                           0x10U
#define CO_CONFIG_GLOBAL_FLAG_CALLBACK_PRE             0U
#define CO_CONFIG_LSS                                  CO_CONFIG_LSS_SLAVE

#define CO_LITTLE_ENDIAN
#define CO_SWAP_16(x) SWAP_16(x)
#define CO_SWAP_32(x) SWAP_32(x)
#define CO_SWAP_64(x) SWAP_64(x)

#define CO_FLAG_READ(flag) FLAG_READ(flag)
#define CO_FLAG_SET(flag) FLAG_SET(flag)
#define CO_FLAG_CLEAR(flag) FLAG_CLEAR(flag)

typedef can_rx_t CO_CANrx_t;
typedef can_tx_t CO_CANtx_t;
typedef can_module_t CO_CANmodule_t;
typedef can_return_error_t CO_ReturnError_t;

#define CO_ERROR_NO ERROR_NO
#define CO_ERROR_ILLEGAL_ARGUMENT ERROR_ILLEGAL_ARGUMENT
#define CO_ERROR_DATA_CORRUPT ERROR_DATA_CORRUPT

#define CO_CANrxMsg_readIdent can_rx_msg_read_ident
#define CO_CANrxMsg_readDLC can_rx_msg_read_dlc
#define CO_CANrxMsg_readData can_rx_msg_read_data
#define CO_CANrxBufferInit can_rx_buffer_init
#define CO_CANtxBufferInit can_tx_buffer_init
#define CO_CANsend can_send

#define CO_CAN_ID_LSS_SLV CAN_ID_LSS_SLV
#define CO_CAN_ID_LSS_MST CAN_ID_LSS_MST

#endif /* CO_DRIVER_H */
