#include "remote_handler.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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
                           uint8_t *body, size_t body_cap, size_t *body_len_out)
{
    zmq_msg_t command_msg;
    zmq_msg_t body_msg;
    int more = 0;
    size_t more_size = sizeof(more);

    if (handler == NULL || handler->socket == NULL || command == NULL || body == NULL ||
        body_len_out == NULL || command_cap == 0u || body_cap == 0u ||
        zmq_msg_init(&command_msg) != 0)
    {
        return -1;
    }
    if (zmq_msg_recv(&command_msg, handler->socket, 0) < 0 ||
        zmq_getsockopt(handler->socket, ZMQ_RCVMORE, &more, &more_size) != 0 || more == 0 ||
        zmq_msg_size(&command_msg) >= command_cap || zmq_msg_init(&body_msg) != 0)
    {
        (void)zmq_msg_close(&command_msg);
        return -1;
    }
    memcpy(command, zmq_msg_data(&command_msg), zmq_msg_size(&command_msg));
    command[zmq_msg_size(&command_msg)] = '\0';
    (void)zmq_msg_close(&command_msg);
    more = 0;
    more_size = sizeof(more);
    if (zmq_msg_recv(&body_msg, handler->socket, 0) < 0 ||
        zmq_getsockopt(handler->socket, ZMQ_RCVMORE, &more, &more_size) != 0 || more != 0 ||
        zmq_msg_size(&body_msg) > body_cap)
    {
        (void)zmq_msg_close(&body_msg);
        return -1;
    }
    memcpy(body, zmq_msg_data(&body_msg), zmq_msg_size(&body_msg));
    *body_len_out = zmq_msg_size(&body_msg);
    (void)zmq_msg_close(&body_msg);
    return 0;
}

int remote_handler_reply(RemoteHandler_t *handler, const char *reply)
{
    if (handler == NULL || handler->socket == NULL || reply == NULL)
    {
        return -1;
    }
    return zmq_send(handler->socket, reply, strlen(reply), 0) >= 0 ? 0 : -1;
}
