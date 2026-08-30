#include "token_signer_client.h"

#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include "token_signer_codec.h"
#include "util.h"

static int validate_socket_path(const TokenSignerClient_t *client)
{
    struct stat socket_stat;

    if (lstat(client->endpoint, &socket_stat) != 0 || !S_ISSOCK(socket_stat.st_mode) ||
        socket_stat.st_nlink != 1 || socket_stat.st_uid != client->expected_socket_uid ||
        socket_stat.st_gid != client->expected_socket_gid ||
        (socket_stat.st_mode & 0777u) != client->expected_socket_mode)
    {
        return TOKEN_SIGNER_ERR_SOCKET_POLICY;
    }
    return 0;
}

static int connect_signer(const TokenSignerClient_t *client)
{
    struct sockaddr_un address;
    struct timeval timeout;
    struct ucred peer;
    socklen_t peer_len = sizeof(peer);
    int fd;

    if (validate_socket_path(client) != 0)
    {
        return TOKEN_SIGNER_ERR_SOCKET_POLICY;
    }
    fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0)
    {
        return TOKEN_SIGNER_ERR_CONNECT;
    }
    timeout.tv_sec = (time_t)(client->timeout_ms / 1000u);
    timeout.tv_usec = (suseconds_t)((client->timeout_ms % 1000u) * 1000u);
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) != 0 ||
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) != 0)
    {
        (void)close(fd);
        return TOKEN_SIGNER_ERR_CONNECT;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    if (strlen(client->endpoint) >= sizeof(address.sun_path))
    {
        (void)close(fd);
        return TOKEN_SIGNER_ERR_INVALID_ARG;
    }
    memcpy(address.sun_path, client->endpoint, strlen(client->endpoint) + 1u);
    if (connect(fd, (const struct sockaddr *)&address, sizeof(address)) != 0)
    {
        (void)close(fd);
        return TOKEN_SIGNER_ERR_CONNECT;
    }
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &peer_len) != 0 ||
        peer_len != sizeof(peer) || peer.uid != client->expected_peer_uid ||
        peer.gid != client->expected_peer_gid)
    {
        (void)close(fd);
        return TOKEN_SIGNER_ERR_PEER;
    }
    return fd;
}

int token_signer_client_issue(const TokenSignerClient_t *client,
                              const uint8_t *seed, size_t seed_len,
                              uint8_t *token_out, size_t token_cap, size_t *token_len_out)
{
    uint8_t request_packet[TOKEN_SIGNER_MAX_REQUEST_SIZE];
    uint8_t response_packet[TOKEN_SIGNER_MAX_RESPONSE_SIZE];
    uint8_t request_id[16];
    size_t request_len = 0u;
    ssize_t response_len;
    int fd = -1;
    int rc = TOKEN_SIGNER_ERR_INVALID_ARG;

    if (client == NULL || client->endpoint == NULL || client->endpoint[0] != '/' ||
        client->timeout_ms == 0u ||
        client->timeout_ms > TOKEN_SIGNER_DEFAULT_TIMEOUT_MS ||
        client->expected_socket_mode != 0660u || seed == NULL || seed_len == 0u ||
        token_out == NULL || token_len_out == NULL || token_cap == 0u)
    {
        return rc;
    }
    *token_len_out = 0u;
    memset(request_packet, 0, sizeof(request_packet));
    memset(response_packet, 0, sizeof(response_packet));
    memset(request_id, 0, sizeof(request_id));
    rc = token_signer_codec_encode_request(
        seed, seed_len, request_id, request_packet, sizeof(request_packet), &request_len);
    if (rc != 0)
    {
        goto out;
    }
    fd = connect_signer(client);
    if (fd < 0)
    {
        rc = fd;
        goto out;
    }
    if (send(fd, request_packet, request_len, MSG_NOSIGNAL) != (ssize_t)request_len)
    {
        rc = TOKEN_SIGNER_ERR_IO;
        goto out;
    }
    response_len = recv(fd, response_packet, sizeof(response_packet), MSG_TRUNC);
    if (response_len <= 0 || (size_t)response_len > sizeof(response_packet))
    {
        rc = TOKEN_SIGNER_ERR_IO;
        goto out;
    }
    rc = token_signer_codec_decode_response(response_packet, (size_t)response_len, request_id,
                                            token_out, token_cap, token_len_out);

out:
    if (fd >= 0)
    {
        (void)close(fd);
    }
    secure_zero(request_packet, sizeof(request_packet));
    secure_zero(response_packet, sizeof(response_packet));
    secure_zero(request_id, sizeof(request_id));
    return rc;
}
