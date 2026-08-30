#ifndef CAN_DRIVER_H
#define CAN_DRIVER_H

#include <stdbool.h>
#include <stdint.h>

#include "can_frame.h"

/* CAN error status bitfield. Mirrors CANopenNode CO_CAN_ERR_status_t
 * (CANopenNode 301/CO_driver.h, Apache-2.0) with the project CAN_ prefix. */
#define CAN_ERRTX_WARNING    0x0001U
#define CAN_ERRTX_PASSIVE    0x0002U
#define CAN_ERRTX_BUS_OFF    0x0004U
#define CAN_ERRTX_OVERFLOW   0x0008U
#define CAN_ERRRX_WARNING    0x0100U
#define CAN_ERRRX_PASSIVE    0x0200U
#define CAN_ERRRX_OVERFLOW   0x0800U
#define CAN_ERR_WARN_PASSIVE 0x0303U

/* Driver return values. Mirrors CANopenNode CO_ReturnError_t
 * (CANopenNode 301/CO_driver.h, Apache-2.0) with the project CAN_ prefix;
 * numeric values are kept identical for review traceability. */
typedef enum
{
    CAN_ERROR_NO = 0,
    CAN_ERROR_ILLEGAL_ARGUMENT = -1,
    CAN_ERROR_TX_OVERFLOW = -9,
    CAN_ERROR_TX_UNCONFIGURED = -11,
    CAN_ERROR_TX_BUSY = -15,
    CAN_ERROR_INVALID_STATE = -18
} CAN_ReturnError_t;

/**
 * Configures the receive filter and starts the CAN controller.
 *
 * @param receive_id Standard CAN identifier accepted by the hardware filter.
 * @return true on success; false for an invalid identifier or platform failure.
 * @pre MX_FDCAN1_Init() completed successfully.
 */
bool CAN_Start(uint32_t receive_id);

/**
 * Queues one classic CAN frame for transmission.
 *
 * @param frame Frame with a standard identifier and DLC no greater than 8.
 * @return CAN_ERROR_NO on success, otherwise one of CAN_ReturnError_t.
 * @pre CAN_Start() completed successfully.
 */
CAN_ReturnError_t CAN_SendFrame(const can_frame_t *frame);

/**
 * Atomically removes the oldest queued receive frame.
 *
 * @param frame Destination for the queued frame.
 * @return true when a frame was copied; false for NULL or an empty queue.
 * @pre FDCAN receive interrupts may be active.
 */
bool CAN_TakeRxFrame(can_frame_t *frame);

/**
 * Returns the receive count.
 *
 * @return Monotonic count since reset.
 * @pre None.
 */
uint32_t CAN_GetRxCount(void);

/**
 * Returns the transmit count.
 *
 * @return Monotonic count since reset.
 * @pre None.
 */
uint32_t CAN_GetTxCount(void);

/**
 * Returns the driver error event count.
 *
 * @return Monotonic count of detected error events since reset.
 * @pre None.
 */
uint32_t CAN_GetErrorCount(void);

/**
 * Returns the classified CAN error status bitfield.
 *
 * @return Bitwise OR of the CAN_ERR_*_WARNING/PASSIVE/BUS_OFF/OVERFLOW
 *         masks. Warning/passive/bus-off bits reflect the current controller
 *         state; overflow bits are latched until cleared by the caller.
 * @pre None.
 */
uint16_t CAN_GetErrorStatus(void);

/**
 * Clears the latched CAN error status bits.
 *
 * @param mask Bitwise OR of the CAN_ERR*_OVERFLOW bits to clear. Other bits
 *             are not affected.
 * @return None.
 * @pre None.
 */
void CAN_ClearErrorStatus(uint16_t mask);

/**
 * Periodically verifies the CAN error state from the controller PSR register.
 *
 * Mirrors CANopenNode CO_CANmodule_process(). Bus-off recovery is performed by
 * the FDCAN controller automatically, so this function only reports the state.
 *
 * @return None.
 * @pre CAN_Start() completed successfully.
 */
void CAN_module_process(void);

#endif /* CAN_DRIVER_H */
