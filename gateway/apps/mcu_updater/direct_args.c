#include "direct_args.h"

#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include "token_signer_client.h"
#include "util.h"

static int parse_uid(const char *text, uid_t *value_out)
{
    unsigned long value = 0u;

    if (value_out == NULL || util_parse_identity(text, &value) != 0)
    {
        return -1;
    }
    *value_out = (uid_t)value;
    return 0;
}

int mcu_updater_direct_args_parse(int argc, char **argv, McuUpdaterDirectOptions_t *options)
{
    enum
    {
        SEEN_JOB_DIR = 1u << 0,
        SEEN_JOB_ID = 1u << 1,
        SEEN_IFNAME = 1u << 2,
        SEEN_SIGNER_ENDPOINT = 1u << 3,
        SEEN_SIGNER_UID = 1u << 4,
        SEEN_SIGNER_GID = 1u << 5,
        SEEN_SIGNER_SOCKET_GID = 1u << 6,
        SEEN_SIGNER_TIMEOUT = 1u << 7,
        SEEN_TARGET = 1u << 8,
    };
    unsigned int seen = 0u;
    const char *basename;
    int index;

    if (options == NULL)
    {
        return -1;
    }
    memset(options, 0, sizeof(*options));
    for (index = 1; index + 1 < argc; index += 2)
    {
        if (strcmp(argv[index], "--job-dir") == 0)
        {
            if ((seen & SEEN_JOB_DIR) != 0u) return -1;
            seen |= SEEN_JOB_DIR;
            options->job_dir = argv[index + 1];
        }
        else if (strcmp(argv[index], "--job-id") == 0)
        {
            if ((seen & SEEN_JOB_ID) != 0u) return -1;
            seen |= SEEN_JOB_ID;
            options->job_id = argv[index + 1];
        }
        else if (strcmp(argv[index], "--ifname") == 0)
        {
            if ((seen & SEEN_IFNAME) != 0u) return -1;
            seen |= SEEN_IFNAME;
            options->ifname = argv[index + 1];
        }
        else if (strcmp(argv[index], "--signer-endpoint") == 0)
        {
            if ((seen & SEEN_SIGNER_ENDPOINT) != 0u) return -1;
            seen |= SEEN_SIGNER_ENDPOINT;
            options->signer_endpoint = argv[index + 1];
        }
        else if (strcmp(argv[index], "--signer-uid") == 0)
        {
            if ((seen & SEEN_SIGNER_UID) != 0u) return -1;
            seen |= SEEN_SIGNER_UID;
            if (parse_uid(argv[index + 1], &options->signer_uid) != 0)
            {
                return -1;
            }
        }
        else if (strcmp(argv[index], "--signer-gid") == 0)
        {
            if ((seen & SEEN_SIGNER_GID) != 0u) return -1;
            seen |= SEEN_SIGNER_GID;
            if (parse_uid(argv[index + 1], &options->signer_gid) != 0)
            {
                return -1;
            }
        }
        else if (strcmp(argv[index], "--signer-socket-gid") == 0)
        {
            if ((seen & SEEN_SIGNER_SOCKET_GID) != 0u) return -1;
            seen |= SEEN_SIGNER_SOCKET_GID;
            if (parse_uid(argv[index + 1], &options->signer_socket_gid) != 0)
            {
                return -1;
            }
        }
        else if (strcmp(argv[index], "--target-identity") == 0)
        {
            const char *text = argv[index + 1];
            char hex[33];
            if ((seen & SEEN_TARGET) != 0u || strlen(text) != 35u) return -1;
            seen |= SEEN_TARGET;
            for (size_t field = 0; field < 4; ++field)
            {
                if (field < 3 && text[field * 9 + 8] != ':') return -1;
                for (size_t digit = 0; digit < 8; ++digit)
                    hex[field * 8 + digit] = (char)tolower((unsigned char)text[field * 9 + digit]);
            }
            hex[32] = '\0';
            if (util_parse_hex(hex, options->target_identity, MCU_IDENTITY_SIZE) != 0) return -1;
        }
        else if (strcmp(argv[index], "--signer-timeout-ms") == 0)
        {
            char *end = NULL;
            unsigned long value = strtoul(argv[index + 1], &end, 10);

            if ((seen & SEEN_SIGNER_TIMEOUT) != 0u) return -1;
            seen |= SEEN_SIGNER_TIMEOUT;
            if (end == NULL || *end != '\0' || value == 0u ||
                value > TOKEN_SIGNER_DEFAULT_TIMEOUT_MS)
            {
                return -1;
            }
            options->signer_timeout_ms = (uint32_t)value;
        }
        else
        {
            return -1;
        }
    }
    if ((seen & SEEN_TARGET) == 0u ||
        index != argc || options->job_dir == NULL || options->job_id == NULL ||
        options->ifname == NULL || options->signer_endpoint == NULL ||
        options->signer_uid == 0u || options->signer_gid == 0u ||
        options->signer_socket_gid == 0u ||
        options->job_dir[0] != '/' || strstr(options->job_dir, "//") != NULL ||
        strstr(options->job_dir, "/./") != NULL || strstr(options->job_dir, "/../") != NULL ||
        !is_uuid_v4(options->job_id) || !valid_ifname(options->ifname))
    {
        return -1;
    }
    basename = strrchr(options->job_dir, '/');
    return basename != NULL && strcmp(basename + 1, options->job_id) == 0 ? 0 : -1;
}
