#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "305/CO_LSSslave.h"

static CO_CANrx_t s_rx[1];
static CO_CANtx_t s_tx[1];
static CO_CANmodule_t s_can = {
    .rxArray = s_rx,
    .rxSize = 1U,
    .txArray = s_tx,
    .txSize = 1U,
};
static uint8_t s_response[8];
static unsigned int s_response_count;
static unsigned int s_store_count;
static uint8_t s_stored_node_id;
static uint16_t s_stored_bit_rate;
static bool s_store_result = true;

CO_ReturnError_t CO_CANrxBufferInit(
    CO_CANmodule_t *module,
    uint16_t index,
    uint16_t ident,
    uint16_t mask,
    bool_t rtr,
    void *object,
    void (*callback)(void *object, void *message))
{
    (void)rtr;
    if ((module == NULL) || (index >= module->rxSize) ||
        (callback == NULL))
    {
        return CO_ERROR_ILLEGAL_ARGUMENT;
    }

    module->rxArray[index].ident = ident;
    module->rxArray[index].mask = mask;
    module->rxArray[index].object = object;
    module->rxArray[index].CANrx_callback = callback;
    return CO_ERROR_NO;
}

CO_CANtx_t *CO_CANtxBufferInit(CO_CANmodule_t *module,
                               uint16_t index,
                               uint16_t ident,
                               bool_t rtr,
                               uint8_t length,
                               bool_t sync)
{
    (void)rtr;
    if ((module == NULL) || (index >= module->txSize))
    {
        return NULL;
    }

    module->txArray[index].ident = ident;
    module->txArray[index].DLC = length;
    module->txArray[index].syncFlag = sync;
    module->txArray[index].bufferFull = false;
    return &module->txArray[index];
}

CO_ReturnError_t CO_CANsend(CO_CANmodule_t *module, CO_CANtx_t *buffer)
{
    (void)module;
    (void)memcpy(s_response, buffer->data, sizeof(s_response));
    s_response_count++;
    return CO_ERROR_NO;
}

static bool_t StoreConfiguration(void *object,
                                 uint8_t node_id,
                                 uint16_t bit_rate)
{
    (void)object;
    s_store_count++;
    s_stored_node_id = node_id;
    s_stored_bit_rate = bit_rate;
    return s_store_result;
}

static bool InitLss(CO_LSSslave_t *slave,
                    CO_LSS_address_t *address,
                    uint8_t *pending_node_id,
                    uint16_t *pending_bit_rate)
{
    if (CO_LSSslave_init(slave,
                         address,
                         pending_bit_rate,
                         pending_node_id,
                         &s_can,
                         0U,
                         CO_CAN_ID_LSS_MST,
                         &s_can,
                         0U,
                         CO_CAN_ID_LSS_SLV) != CO_ERROR_NO)
    {
        return false;
    }

    CO_LSSslave_initCfgStoreCall(slave, NULL, StoreConfiguration);
    return true;
}

static bool SendAndProcess(CO_LSSslave_t *slave, const uint8_t data[8])
{
    CO_CANrxMsg_t message = {CO_CAN_ID_LSS_MST, 8U, {0}};

    (void)memcpy(message.data, data, sizeof(message.data));
    s_rx[0].CANrx_callback(s_rx[0].object, &message);
    return CO_LSSslave_process(slave);
}

static void ExpectResponse(uint8_t service, uint8_t status)
{
    assert(s_response_count > 0U);
    assert(s_response[0] == service);
    assert(s_response[1] == status);
}

int main(void)
{
    CO_LSSslave_t slave;
    CO_LSS_address_t address = {
        .identity = {
            .vendorID = 0x11223344U,
            .productCode = 0x55667788U,
            .revisionNumber = 0x01020304U,
            .serialNumber = 0xAABBCCDDU,
        },
    };
    uint8_t pending_node_id = 0xFFU;
    uint16_t pending_bit_rate = 500U;
    unsigned int response_count = 0U;
    uint8_t switch_configuration[8] = {CO_LSS_SWITCH_STATE_GLOBAL,
                                       CO_LSS_STATE_CONFIGURATION};
    uint8_t configure_node[8] = {CO_LSS_CFG_NODE_ID, 42U};
    uint8_t configure_bit_rate[8] = {CO_LSS_CFG_BIT_TIMING,
                                     0U,
                                     CO_LSS_BIT_TIMING_250};
    uint8_t store[8] = {CO_LSS_CFG_STORE};
    uint8_t switch_waiting[8] = {CO_LSS_SWITCH_STATE_GLOBAL,
                                 CO_LSS_STATE_WAITING};

    assert(InitLss(&slave,
                   &address,
                   &pending_node_id,
                   &pending_bit_rate));
    assert(slave.activeNodeID == 0xFFU);

    assert(!SendAndProcess(&slave, switch_configuration));
    assert(!SendAndProcess(&slave, configure_node));
    ExpectResponse(CO_LSS_CFG_NODE_ID, CO_LSS_CFG_NODE_ID_OK);
    assert(pending_node_id == 42U);

    response_count = s_response_count;
    assert(!SendAndProcess(&slave, configure_bit_rate));
    assert(s_response_count == response_count);
    assert(pending_bit_rate == 500U);

    assert(!SendAndProcess(&slave, store));
    ExpectResponse(CO_LSS_CFG_STORE, CO_LSS_CFG_STORE_OK);
    assert(s_store_count == 1U);
    assert(s_stored_node_id == 42U);
    assert(s_stored_bit_rate == 500U);

    s_store_result = false;
    assert(!SendAndProcess(&slave, store));
    ExpectResponse(CO_LSS_CFG_STORE, CO_LSS_CFG_STORE_FAILED);

    assert(SendAndProcess(&slave, switch_waiting));
    assert(InitLss(&slave,
                   &address,
                   &pending_node_id,
                   &pending_bit_rate));
    assert(slave.activeNodeID == 42U);
    return 0;
}
