#include "remote_handler.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <zmq.h>

#define REMOTE_MESSAGE_FRAMES 2u
#define REMOTE_FRAME_COMMAND 0u
#define REMOTE_FRAME_BODY 1u

struct RHmsg
{
    zmq_msg_t frame[REMOTE_MESSAGE_FRAMES];
};

static void remote_message_close(struct RHmsg *message)
{
    size_t index;

    for (index = 0u; index < REMOTE_MESSAGE_FRAMES; ++index)
    {
        (void)zmq_msg_close(&message->frame[index]);
    }
}

static int remote_message_receive(void *socket, struct RHmsg *message)
{
    size_t index;

    for (index = 0u; index < REMOTE_MESSAGE_FRAMES; ++index)
    {
        if (zmq_msg_init(&message->frame[index]) != 0)
        {
            while (index > 0u)
            {
                --index;
                (void)zmq_msg_close(&message->frame[index]);
            }
            return -1;
        }
    }
    for (index = 0u; index < REMOTE_MESSAGE_FRAMES; ++index)
    {
        if (zmq_msg_recv(&message->frame[index], socket, 0) < 0 ||
            (index + 1u < REMOTE_MESSAGE_FRAMES) !=
                (zmq_msg_more(&message->frame[index]) != 0))
        {
            remote_message_close(message);
            return -1;
        }
    }
    return 0;
}

static int prepare_endpoint(const char *endpoint, char *path_out, size_t path_cap,
                            int *lock_fd_out)
{
    const char *path = endpoint + strlen("ipc://");
    char lock_path[512];
    struct stat endpoint_stat;
    int lock_fd;
    int length;

    if (strncmp(endpoint, "ipc://", 6u) != 0 || path[0] != '/')
    {
        return -1;
    }
    length = snprintf(path_out, path_cap, "%s", path);
    if (length < 0 || (size_t)length >= path_cap ||
        snprintf(lock_path, sizeof(lock_path), "%s.lock", path) < 0 ||
        strlen(path) + strlen(".lock") >= sizeof(lock_path))
    {
        return -1;
    }
    lock_fd = open(lock_path, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (lock_fd < 0 || flock(lock_fd, LOCK_EX | LOCK_NB) != 0)
    {
        if (lock_fd >= 0)
        {
            (void)close(lock_fd);
        }
        return -1;
    }
    if (lstat(path, &endpoint_stat) == 0)
    {
        if (!S_ISSOCK(endpoint_stat.st_mode) || endpoint_stat.st_uid != geteuid() ||
            (endpoint_stat.st_mode & 0777u) != 0660u || unlink(path) != 0)
        {
            (void)close(lock_fd);
            return -1;
        }
    }
    else if (errno != ENOENT)
    {
        (void)close(lock_fd);
        return -1;
    }
    *lock_fd_out = lock_fd;
    return 0;
}

int remote_handler_init(RemoteHandler_t *handler, const char *endpoint, int receive_timeout_ms)
{
    int linger = 0;

    if (handler == NULL || endpoint == NULL || receive_timeout_ms <= 0)
    {
        return -1;
    }
    memset(handler, 0, sizeof(*handler));
    handler->lock_fd = -1;
    if (prepare_endpoint(endpoint, handler->endpoint_path, sizeof(handler->endpoint_path),
                         &handler->lock_fd) != 0)
    {
        return -1;
    }
    handler->context = zmq_ctx_new();
    handler->socket = handler->context != NULL ? zmq_socket(handler->context, ZMQ_REP) : NULL;
    if (handler->socket == NULL ||
        zmq_setsockopt(handler->socket, ZMQ_LINGER, &linger, sizeof(linger)) != 0 ||
        zmq_setsockopt(handler->socket, ZMQ_RCVTIMEO, &receive_timeout_ms,
                       sizeof(receive_timeout_ms)) != 0 ||
        zmq_bind(handler->socket, endpoint) != 0 ||
        chmod(handler->endpoint_path, 0660u) != 0)
    {
        remote_handler_close(handler);
        return -1;
    }
    return 0;
}

void remote_handler_close(RemoteHandler_t *handler)
{
    if (handler == NULL)
    {
        return;
    }
    if (handler->socket != NULL)
    {
        (void)zmq_close(handler->socket);
    }
    if (handler->context != NULL)
    {
        (void)zmq_ctx_destroy(handler->context);
    }
    if (handler->lock_fd >= 0)
    {
        (void)unlink(handler->endpoint_path);
        (void)close(handler->lock_fd);
    }
    memset(handler, 0, sizeof(*handler));
    handler->lock_fd = -1;
}

int remote_handler_receive(RemoteHandler_t *handler, char *command, size_t command_cap,
                           uint8_t *body, size_t body_cap, size_t *command_len_out,
                           size_t *body_len_out)
{
    struct RHmsg message;
    size_t command_len;
    size_t body_len;

    if (handler == NULL || handler->socket == NULL || command == NULL || body == NULL ||
        command_len_out == NULL || body_len_out == NULL || command_cap == 0u ||
        body_cap == 0u)
    {
        return -1;
    }
    if (remote_message_receive(handler->socket, &message) != 0)
    {
        return -1;
    }
    command_len = zmq_msg_size(&message.frame[REMOTE_FRAME_COMMAND]);
    body_len = zmq_msg_size(&message.frame[REMOTE_FRAME_BODY]);
    if (command_len >= command_cap || body_len > body_cap)
    {
        remote_message_close(&message);
        return -1;
    }
    memcpy(command, zmq_msg_data(&message.frame[REMOTE_FRAME_COMMAND]), command_len);
    command[command_len] = '\0';
    if (body_len > 0u)
    {
        memcpy(body, zmq_msg_data(&message.frame[REMOTE_FRAME_BODY]), body_len);
    }
    *command_len_out = command_len;
    *body_len_out = body_len;
    remote_message_close(&message);
    return 0;
}

int RHmsg_send_ack(RemoteHandler_t *handler, const char *reply)
{
    if (handler == NULL || handler->socket == NULL || reply == NULL)
    {
        return -1;
    }
    return zmq_send(handler->socket, reply, strlen(reply), 0) >= 0 ? 0 : -1;
}
