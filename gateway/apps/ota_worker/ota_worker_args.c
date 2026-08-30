#include "ota_worker_args.h"

#include <stdlib.h>
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

int ota_worker_args_parse_options(int argc, char **argv, OtaWorkerOptions_t *options)
{
    enum
    {
        SEEN_JOB_DIR = 1u << 0,
        SEEN_JOB_ID = 1u << 1,
        SEEN_IFNAME = 1u << 2,
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
            options->signer_endpoint = argv[index + 1];
        }
        else if (strcmp(argv[index], "--signer-uid") == 0)
        {
            if (parse_uid(argv[index + 1], &options->signer_uid) != 0)
            {
                return -1;
            }
        }
        else if (strcmp(argv[index], "--signer-gid") == 0)
        {
            if (parse_uid(argv[index + 1], &options->signer_gid) != 0)
            {
                return -1;
            }
        }
        else if (strcmp(argv[index], "--signer-socket-gid") == 0)
        {
            if (parse_uid(argv[index + 1], &options->signer_socket_gid) != 0)
            {
                return -1;
            }
        }
        else if (strcmp(argv[index], "--signer-timeout-ms") == 0)
        {
            char *end = NULL;
            unsigned long value = strtoul(argv[index + 1], &end, 10);

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
    if (index != argc || options->job_dir == NULL || options->job_id == NULL ||
        options->ifname == NULL ||
        options->job_dir[0] != '/' || strstr(options->job_dir, "//") != NULL ||
        strstr(options->job_dir, "/./") != NULL || strstr(options->job_dir, "/../") != NULL ||
        !is_uuid_v4(options->job_id) || !valid_ifname(options->ifname))
    {
        return -1;
    }
    basename = strrchr(options->job_dir, '/');
    return basename != NULL && strcmp(basename + 1, options->job_id) == 0 ? 0 : -1;
}
