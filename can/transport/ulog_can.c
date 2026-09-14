#include "ulog_can.h"

#include "can_driver.h"
#include "ulog_can_wire.h"

#include <rtthread.h>
#include <ipc/ringblk_buf.h>
#include <string.h>
#include <ulog.h>

#define ULOG_CAN_QUEUE_CAPACITY 16U
#define ULOG_CAN_RECORD_MAX_LOG_SIZE \
    ((ULOG_LINE_BUF_SIZE < MCU_LOG_CAN_MAX_LOG_SIZE) \
         ? ULOG_LINE_BUF_SIZE \
         : MCU_LOG_CAN_MAX_LOG_SIZE)

#define ULOG_CAN_QUEUE_STORAGE_WORDS \
    ((ULOG_CAN_QUEUE_CAPACITY * ULOG_CAN_RECORD_MAX_LOG_SIZE + sizeof(rt_ubase_t) - 1U) \
     / sizeof(rt_ubase_t))

static struct ulog_backend s_ulog_can;
static bool s_ulog_can_registered;
static struct rt_rbb s_record_queue;
static struct rt_rbb_blk s_record_blocks[ULOG_CAN_QUEUE_CAPACITY];
static rt_ubase_t s_record_storage[ULOG_CAN_QUEUE_STORAGE_WORDS];
static rt_rbb_blk_t s_active_record;
static uint8_t s_active_fragment;
static can_module_t *s_can_module;
static can_tx_t *s_tx_buffer;

static void ULogCan_Enqueue(
    struct ulog_backend *backend,
    rt_uint32_t level,
    const char *tag,
    rt_bool_t is_raw,
    const char *log,
    rt_size_t length)
{
    rt_rbb_blk_t block = RT_NULL;

    (void)backend;
    (void)level;
    (void)tag;
    (void)is_raw;

    if ((log == RT_NULL) || (length > ULOG_CAN_RECORD_MAX_LOG_SIZE))
    {
        return;
    }

    block = rt_rbb_blk_alloc(&s_record_queue, length);
    if (block == RT_NULL)
    {
        return;
    }

    (void)memcpy(rt_rbb_blk_buf(block), log, length);
    rt_rbb_blk_put(block);
}

bool ULogCan_Init(can_module_t *CANmodule, can_tx_t *tx_buffer)
{
    if ((CANmodule == NULL) || (tx_buffer == NULL))
    {
        return false;
    }

    if (s_ulog_can_registered)
    {
        return true;
    }

    s_can_module = CANmodule;
    s_tx_buffer = tx_buffer;

    rt_rbb_init(&s_record_queue,
                (rt_uint8_t *)s_record_storage,
                sizeof(s_record_storage),
                s_record_blocks,
                ULOG_CAN_QUEUE_CAPACITY);
    s_ulog_can.output = ULogCan_Enqueue;
    if (ulog_backend_register(&s_ulog_can, "canulog", RT_FALSE)
        != RT_EOK)
    {
        return false;
    }

    s_ulog_can_registered = true;
    return true;
}

bool ULogCan_Poll(void)
{
    if (!s_ulog_can_registered || s_tx_buffer->bufferFull)
    {
        return false;
    }

    if (s_active_record == RT_NULL)
    {
        s_active_record = rt_rbb_blk_get(&s_record_queue);
        if (s_active_record == RT_NULL)
        {
            return false;
        }
        s_active_fragment = 0U;
    }

    if (!ULogCanWire_BuildFrame((const char *)rt_rbb_blk_buf(s_active_record),
                                (uint16_t)rt_rbb_blk_size(s_active_record),
                                s_active_fragment,
                                s_tx_buffer))
    {
        rt_rbb_blk_free(&s_record_queue, s_active_record);
        s_active_record = RT_NULL;
        return false;
    }

    /* A full Tx FIFO leaves the active fragment untouched for the next poll. */
    if (can_send(s_can_module, s_tx_buffer) != ERROR_NO)
    {
        return false;
    }

    s_active_fragment++;
    if ((s_tx_buffer->data[0] & MCU_LOG_CAN_END) != 0U)
    {
        rt_rbb_blk_free(&s_record_queue, s_active_record);
        s_active_record = RT_NULL;
    }
    return true;
}
