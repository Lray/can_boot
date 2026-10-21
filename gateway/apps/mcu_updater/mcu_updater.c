#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "byte_order.h"
#include "LSS_gateway.h"
#include "package_metadata.h"
#include "profile.h"
#include "remote_handler.h"
#include "security_token_tee.h"
#include "update_job.h"

#define MCU_UPDATER_CAN_IFNAME "awlink0"
#define MCU_UPDATER_MAX_DATA (64u * 1024u)

static int harden_process(void)
{
    struct rlimit no_core = {0u, 0u};

    (void)umask(0077u);
    (void)signal(SIGPIPE, SIG_IGN);
    return setrlimit(RLIMIT_CORE, &no_core) == 0 && prctl(PR_SET_DUMPABLE, 0) == 0 ? 0 : -1;
}

static int parse_init_size(const char *command, size_t command_len,
                           uint64_t *size_out)
{
    uint64_t value = 0u;
    size_t index;

    if (command == NULL || size_out == NULL || command_len <= 5u ||
        memcmp(command, "INIT:", 5u) != 0 ||
        (command_len > 6u && command[5] == '0'))
    {
        return -1;
    }
    for (index = 5u; index < command_len; ++index)
    {
        char digit = command[index];

        if (digit < '0' || digit > '9' ||
            value > (UINT64_MAX - (uint64_t)(digit - '0')) / 10u)
        {
            return -1;
        }
        value = (value * 10u) + (uint64_t)(digit - '0');
    }
    if (value == 0u || value > OTA_PACKAGE_MAX_SIZE)
    {
        return -1;
    }
    *size_out = value;
    return 0;
}

static int write_all(int fd, const uint8_t *buffer, size_t length)
{
    size_t offset = 0u;

    while (offset < length)
    {
        ssize_t count = write(fd, buffer + offset, length - offset);

        if (count < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return -1;
        }
        if (count == 0)
        {
            return -1;
        }
        offset += (size_t)count;
    }
    return 0;
}

/* Blocking reply accumulation follows CANopenLinux cocomm/printReply,
 * commit f1348d4072cdabea4c3435a13c721ac29ab4cc91 (Copyright 2020 Janez Paternoster).
 * SPDX-License-Identifier: Apache-2.0
 * Retain the official 100-byte response tail while consuming LF-only scan reports.
 */
static int read_gateway_reply(int fd, unsigned sequence, uint8_t *node_id_out)
{
    enum { READ_SIZE = 1000, REPLY_LAG = 100 };
    char reply[READ_SIZE + REPLY_LAG + 1];
    char prefix[16];
    size_t count = 0u;
    int prefix_length = snprintf(prefix, sizeof(prefix), "[%u] ", sequence);

    if (prefix_length < 0 || (size_t)prefix_length >= sizeof(prefix))
    {
        return -1;
    }
    for (;;)
    {
        ssize_t received = read(fd, reply + count, READ_SIZE);
        char *end;
        char *line;
        char *value;

        if (received < 0 && errno == EINTR)
        {
            continue;
        }
        if (received <= 0)
        {
            return -1;
        }
        count += (size_t)received;
        reply[count] = '\0';
        end = strstr(reply, "\r\n");
        if (end == NULL)
        {
            if (count > REPLY_LAG)
            {
                memmove(reply, reply + count - REPLY_LAG, REPLY_LAG);
                count = REPLY_LAG;
            }
            continue;
        }
        *end = '\0';
        line = strrchr(reply, '\n');
        line = line == NULL ? reply : line + 1;
        if (strncmp(line, prefix, (size_t)prefix_length) != 0)
        {
            return -1;
        }
        value = line + prefix_length;
        if (node_id_out == NULL)
        {
            return strcmp(value, "OK") == 0 ? 0 : -1;
        }
        errno = 0;
        unsigned long node_id = strtoul(value, &end, 0);
        if (errno != 0 || *end != '\0' ||
            node_id < CAN_NODE_ID_MIN || node_id > CAN_NODE_ID_MAX)
        {
            return -1;
        }
        *node_id_out = (uint8_t)node_id;
        return 0;
    }
}

/* One connection owns the official allnodes/select/inquire/deselect sequence. */
static int request_target_node(int image_fd, uint8_t *node_id_out)
{
    uint8_t identity[OTA_ENVELOPE_IDENTITY_SIZE];
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    char select_command[96];
    const char *commands[] = {
        "[1] lss_allnodes 100 2 1\r\n", select_command,
        "[3] lss_get_node\r\n", "[4] lss_switch_glob 0\r\n",
    };
    uint8_t node_id = 0u;
    int result = -1;
    int socket_fd = -1;
    int length;

    if (node_id_out == NULL ||
        pread(image_fd, identity, sizeof(identity), 0) != (ssize_t)sizeof(identity))
    {
        goto out;
    }
    length = snprintf(select_command, sizeof(select_command),
                      "[2] lss_switch_sel 0x%08" PRIX32 " 0x%08" PRIX32
                      " 0x%08" PRIX32 " 0x%08" PRIX32 "\r\n",
                      byte_order_get_u32_be(identity),
                      byte_order_get_u32_be(identity + 4u),
                      byte_order_get_u32_be(identity + 8u),
                      byte_order_get_u32_be(identity + 12u));
    if (length < 0 || (size_t)length >= sizeof(select_command))
    {
        goto out;
    }
    memcpy(address.sun_path, LSS_GATEWAY_SOCKET, sizeof(LSS_GATEWAY_SOCKET));
    socket_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (socket_fd < 0 ||
        connect(socket_fd, (struct sockaddr *)&address, sizeof(address)) != 0)
    {
        goto out;
    }
    for (size_t step = 0u; step < sizeof(commands) / sizeof(commands[0]); ++step)
    {
        if (write_all(socket_fd, (const uint8_t *)commands[step], strlen(commands[step])) != 0 ||
            read_gateway_reply(socket_fd, (unsigned)step + 1u, step == 2u ? &node_id : NULL) != 0)
        {
            goto out;
        }
    }
    *node_id_out = node_id;
    result = 0;

out:
    if (socket_fd >= 0)
    {
        (void)close(socket_fd);
    }
    if (result != 0)
    {
        fprintf(stderr, "mcu-updater: gateway node-ID request failed\n");
    }
    return result;
}

static int install_remote_image(RemoteHandler_t *handler,
                                SecurityTokenTee_t *token_tee)
{
    char command[REMOTE_HANDLER_CMD_MAX];
    uint8_t body[MCU_UPDATER_MAX_DATA];
    uint64_t expected_size = 0u;
    uint64_t received_size = 0u;
    size_t command_len = 0u;
    size_t body_len = 0u;
    int image_fd = -1;
    int rc;

    if (remote_handler_receive(handler, command, sizeof(command), body, sizeof(body),
                               &command_len, &body_len) != 0)
    {
        fprintf(stderr, "mcu-updater: remote receive failure\n");
        return -1;
    }
    if (body_len != 0u ||
        parse_init_size(command, command_len, &expected_size) != 0)
    {
        return RHmsg_send_ack(handler, "NACK");
    }
    image_fd = memfd_create("mcu-update-image", MFD_CLOEXEC);
    if (image_fd < 0)
    {
        return RHmsg_send_ack(handler, "NACK");
    }
    {
        char reply[32];
        int reply_len = snprintf(reply, sizeof(reply), "ACK:%u", MCU_UPDATE_REMOTE_WAIT_MS);

        rc = reply_len > 0 && (size_t)reply_len < sizeof(reply)
                 ? RHmsg_send_ack(handler, reply)
                 : -1;
    }

    while (rc == 0 && received_size < expected_size)
    {
        if (remote_handler_receive(handler, command, sizeof(command), body, sizeof(body),
                                   &command_len, &body_len) != 0)
        {
            rc = -1;
            break;
        }
        if (command_len != 4u || memcmp(command, "DATA", 4u) != 0 ||
            body_len == 0u || body_len > expected_size - received_size ||
            write_all(image_fd, body, body_len) != 0)
        {
            rc = RHmsg_send_ack(handler, "NACK");
            break;
        }
        received_size += body_len;
        if (received_size < expected_size)
        {
            rc = RHmsg_send_ack(handler, "ACK");
            continue;
        }
        {
            McuUpdateConfig_t config = {
                .can_ifname = MCU_UPDATER_CAN_IFNAME,
                .token_tee = token_tee,
            };
            McuUpdateResult_t result = {0};
            int update_rc;

            fprintf(stderr, "mcu-updater: image received bytes=%llu\n",
                    (unsigned long long)received_size);
            update_rc = request_target_node(image_fd, &config.node_id) == 0
                            ? mcu_update_run_image(image_fd, &config, &result)
                            : MCU_UPDATE_EXIT_TRANSPORT;
            mcu_update_log_result("mcu-updater", update_rc, &result);
            rc = RHmsg_send_ack(handler, update_rc == 0 ? "ACK" : "NACK");
        }
    }
    (void)close(image_fd);
    return rc;
}

int main(void)
{
    RemoteHandler_t handler = {0};
    SecurityTokenTee_t *token_tee = NULL;
    int handler_ready = 0;

    if (harden_process() != 0)
    {
        fprintf(stderr, "mcu-updater: process hardening failed\n");
        return 2;
    }
    if (security_token_tee_open(&token_tee) != 0)
    {
        fprintf(stderr, "mcu-updater: cannot open security-token TA\n");
        goto out;
    }
    if (remote_handler_init(&handler, REMOTE_HANDLER_ENDPOINT, INT_MAX) != 0)
    {
        fprintf(stderr, "mcu-updater: cannot bind remote handler endpoint\n");
        goto out;
    }
    handler_ready = 1;
    fprintf(stderr, "mcu-updater: READY\n");

    for (;;)
    {
        if (install_remote_image(&handler, token_tee) != 0)
            break;
    }

out:
    security_token_tee_close(token_tee);
    if (handler_ready)
    {
        remote_handler_close(&handler);
    }
    return 1;
}
