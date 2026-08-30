#ifndef REMOTE_HANDLER_H
#define REMOTE_HANDLER_H

#include <stddef.h>
#include <stdint.h>
#include <zmq.h>

#define REMOTE_HANDLER_DEFAULT_ENDPOINT "ipc:///run/ecu-ota/remote-handler/ecu-v1"
#define REMOTE_HANDLER_CMD_MAX 96u

typedef struct
{
    void *context;
    void *socket;
    int lock_fd;
    char endpoint_path[512];
} RemoteHandler_t;

int remote_handler_init(RemoteHandler_t *handler, const char *endpoint, int receive_timeout_ms);
void remote_handler_close(RemoteHandler_t *handler);
int remote_handler_receive(RemoteHandler_t *handler, char *command, size_t command_cap,
                           uint8_t *body, size_t body_cap, size_t *body_len_out);
int remote_handler_reply(RemoteHandler_t *handler, const char *reply);

#endif
