#ifndef GATEWAY_LOG_H
#define GATEWAY_LOG_H

/*
 * GW_LOG_* is the application-level name for records sent to the Gateway.
 * Define LOG_TAG and LOG_LVL before including this header.
 */
#include <ulog.h>

#define GW_LOG_E(...) ulog_e(LOG_TAG, __VA_ARGS__)
#define GW_LOG_W(...) ulog_w(LOG_TAG, __VA_ARGS__)
#define GW_LOG_I(...) ulog_i(LOG_TAG, __VA_ARGS__)
#define GW_LOG_D(...) ulog_d(LOG_TAG, __VA_ARGS__)

#endif /* GATEWAY_LOG_H */
