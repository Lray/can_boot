#ifndef CAN_TRANSPORT_H
#define CAN_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>

#include "can_driver.h"
#include "can_frame.h"

typedef CAN_ReturnError_t (*can_send_fn_t)(const can_frame_t *frame);
typedef bool (*can_rx_handler_t)(const can_frame_t *frame);
typedef bool (*can_rx_source_fn_t)(can_frame_t *frame);

/** Initializes the CAN transport callbacks. */
void CAN_Transport_Init(can_send_fn_t send_fn);

/** Installs the lower-layer source used by CAN_Transport_Poll(). */
void CAN_Transport_SetRxSource(can_rx_source_fn_t rx_source);

/** Installs the callback used to deliver one received CAN frame. */
void CAN_Transport_SetRxHandler(can_rx_handler_t rx_handler);

/** Drains queued CAN frames into the configured receive handler. */
void CAN_Transport_Poll(void);

/** Sends one CAN frame through the configured lower-layer sender. */
CAN_ReturnError_t CAN_Transport_Send(const can_frame_t *frame);

/** Delivers one CAN frame to the configured receive handler. */
bool CAN_Transport_OnRxFrame(const can_frame_t *frame);

#endif /* CAN_TRANSPORT_H */
