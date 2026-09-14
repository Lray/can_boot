#include "mcu_updater_args.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "remote_handler.h"
#include "util.h"

static int is_positive_identity(const char *text)
{
    unsigned long value = 0u;

    return util_parse_identity(text, &value) == 0;
}

static int is_signer_timeout(const char *text)
{
    char *end = NULL;
    unsigned long value;

    if (text == NULL || text[0] == '\0')
    {
        return 0;
    }
    errno = 0;
    value = strtoul(text, &end, 10);
    return errno == 0 && end != text && *end == '\0' && value > 0u && value <= 1000u;
}

static int is_safe_absolute_root(const char *path)
{
    size_t length;

    if (path == NULL || path[0] != '/')
    {
        return 0;
    }
    length = strlen(path);
    return length > 1u && path[length - 1u] != '/' && strstr(path, "//") == NULL &&
           strstr(path, "/./") == NULL && strstr(path, "/../") == NULL &&
           (length < 2u || strcmp(path + length - 2u, "/.") != 0) &&
           (length < 3u || strcmp(path + length - 3u, "/..") != 0);
}

int mcu_updater_args_parse(int argc, char **argv, McuUpdaterOptions_t *options)
{
    enum
    {
        SEEN_WORK_ROOT = 1u << 0,
        SEEN_SIGNER_ENDPOINT = 1u << 1,
        SEEN_SIGNER_UID = 1u << 2,
        SEEN_SIGNER_GID = 1u << 3,
        SEEN_SIGNER_SOCKET_GID = 1u << 4,
        SEEN_SIGNER_TIMEOUT = 1u << 5,
        SEEN_CAN_IFNAME = 1u << 6,
        SEEN_ENDPOINT = 1u << 7,
    };
    unsigned int seen = 0u;
    int index;

    if (options == NULL)
    {
        return -1;
    }
    memset(options, 0, sizeof(*options));
    options->can_ifname = MCU_UPDATER_DEFAULT_CAN_IFNAME;
    options->endpoint = REMOTE_HANDLER_DEFAULT_ENDPOINT;
    for (index = 1; index + 1 < argc; index += 2)
    {
        const char *name = argv[index];
        const char *value = argv[index + 1];

        if (strcmp(name, "--work-root") == 0)
        {
            if ((seen & SEEN_WORK_ROOT) != 0u) return -1;
            seen |= SEEN_WORK_ROOT;
            options->work_root = value;
        }
        else if (strcmp(name, "--ifname") == 0)
        {
            if ((seen & SEEN_CAN_IFNAME) != 0u) return -1;
            seen |= SEEN_CAN_IFNAME;
            options->can_ifname = value;
        }
        else if (strcmp(name, "--endpoint") == 0)
        {
            if ((seen & SEEN_ENDPOINT) != 0u) return -1;
            seen |= SEEN_ENDPOINT;
            options->endpoint = value;
        }
        else if (strcmp(name, "--signer-endpoint") == 0)
        {
            if ((seen & SEEN_SIGNER_ENDPOINT) != 0u) return -1;
            seen |= SEEN_SIGNER_ENDPOINT;
            options->signer_endpoint = value;
        }
        else if (strcmp(name, "--signer-uid") == 0)
        {
            if ((seen & SEEN_SIGNER_UID) != 0u) return -1;
            seen |= SEEN_SIGNER_UID;
            options->signer_uid = value;
        }
        else if (strcmp(name, "--signer-gid") == 0)
        {
            if ((seen & SEEN_SIGNER_GID) != 0u) return -1;
            seen |= SEEN_SIGNER_GID;
            options->signer_gid = value;
        }
        else if (strcmp(name, "--signer-socket-gid") == 0)
        {
            if ((seen & SEEN_SIGNER_SOCKET_GID) != 0u) return -1;
            seen |= SEEN_SIGNER_SOCKET_GID;
            options->signer_socket_gid = value;
        }
        else if (strcmp(name, "--signer-timeout-ms") == 0)
        {
            if ((seen & SEEN_SIGNER_TIMEOUT) != 0u) return -1;
            seen |= SEEN_SIGNER_TIMEOUT;
            options->signer_timeout_ms = value;
        }
        else
        {
            return -1;
        }
    }
    return index == argc ? 0 : -1;
}

int mcu_updater_args_validate(const McuUpdaterOptions_t *options)
{
    if (options == NULL || options->work_root == NULL || options->signer_endpoint == NULL ||
        options->signer_uid == NULL || options->signer_gid == NULL ||
        options->signer_socket_gid == NULL || options->signer_timeout_ms == NULL)
    {
        return -1;
    }
    if (!is_safe_absolute_root(options->work_root))
    {
        return -5;
    }
    if (!is_safe_absolute_root(options->signer_endpoint) ||
        !is_positive_identity(options->signer_uid) || !is_positive_identity(options->signer_gid) ||
        !is_positive_identity(options->signer_socket_gid) ||
        !is_signer_timeout(options->signer_timeout_ms))
    {
        return -11;
    }
    if (!valid_ifname(options->can_ifname))
    {
        return -6;
    }
    return 0;
}

void mcu_updater_args_print_usage(const char *program)
{
    fprintf(stderr,
            "usage: %s --work-root /abs/path "
            "--signer-endpoint /abs/v1.sock --signer-uid UID --signer-gid GID "
            "--signer-socket-gid GID --signer-timeout-ms N "
            "[--ifname awlink0] [--endpoint " REMOTE_HANDLER_DEFAULT_ENDPOINT "]\n",
            program);
}
