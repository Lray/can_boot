#include "orchestrator_args.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "package_store.h"
#include "util.h"

static int parse_sha256(const char *text, uint8_t digest[PACKAGE_SHA256_SIZE])
{
    return digest != NULL && util_parse_hex(text, digest, PACKAGE_SHA256_SIZE) == 0 ? 0 : -1;
}

static int parse_size(const char *text, uint64_t *value_out)
{
    return util_parse_size(text, value_out) == 0 && *value_out <= PACKAGE_STORE_MAX_SIZE ? 0 : -1;
}

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

int orchestrator_args_parse(int argc, char **argv, OrchestratorOptions_t *options)
{
    enum
    {
        SEEN_JOB_ID = 1u << 0,
        SEEN_EXPECTED_SIZE = 1u << 1,
        SEEN_EXPECTED_SHA256 = 1u << 2,
        SEEN_WORK_ROOT = 1u << 3,
        SEEN_SIGNER_ENDPOINT = 1u << 4,
        SEEN_SIGNER_UID = 1u << 5,
        SEEN_SIGNER_GID = 1u << 6,
        SEEN_SIGNER_SOCKET_GID = 1u << 7,
        SEEN_SIGNER_TIMEOUT = 1u << 8,
    };
    unsigned int seen = 0u;
    int index;

    if (options == NULL)
    {
        return -1;
    }
    memset(options, 0, sizeof(*options));
    options->can_ifname = ORCHESTRATOR_DEFAULT_CAN_IFNAME;
    options->endpoint = ORCHESTRATOR_DEFAULT_ENDPOINT;
    for (index = 1; index + 1 < argc; index += 2)
    {
        const char *name = argv[index];
        const char *value = argv[index + 1];

        if (strcmp(name, "--job-id") == 0)
        {
            if ((seen & SEEN_JOB_ID) != 0u) return -1;
            seen |= SEEN_JOB_ID;
            options->job_id = value;
        }
        else if (strcmp(name, "--expected-size") == 0)
        {
            if ((seen & SEEN_EXPECTED_SIZE) != 0u) return -1;
            seen |= SEEN_EXPECTED_SIZE;
            options->expected_size = value;
        }
        else if (strcmp(name, "--expected-sha256") == 0)
        {
            if ((seen & SEEN_EXPECTED_SHA256) != 0u) return -1;
            seen |= SEEN_EXPECTED_SHA256;
            options->expected_sha256 = value;
        }
        else if (strcmp(name, "--work-root") == 0)
        {
            if ((seen & SEEN_WORK_ROOT) != 0u) return -1;
            seen |= SEEN_WORK_ROOT;
            options->work_root = value;
        }
        else if (strcmp(name, "--ifname") == 0)
        {
            options->can_ifname = value;
        }
        else if (strcmp(name, "--endpoint") == 0)
        {
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

int orchestrator_args_validate(const OrchestratorOptions_t *options, uint64_t *size_out,
                               uint8_t sha256_out[PACKAGE_SHA256_SIZE])
{
    if (options == NULL || size_out == NULL || sha256_out == NULL || options->job_id == NULL ||
        options->expected_size == NULL || options->expected_sha256 == NULL ||
        options->work_root == NULL || options->signer_endpoint == NULL ||
        options->signer_uid == NULL || options->signer_gid == NULL ||
        options->signer_socket_gid == NULL || options->signer_timeout_ms == NULL)
    {
        return -1;
    }
    if (!is_uuid_v4(options->job_id))
    {
        return -2;
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
    if (parse_size(options->expected_size, size_out) != 0)
    {
        return -7;
    }
    if (parse_sha256(options->expected_sha256, sha256_out) != 0)
    {
        return -8;
    }
    return 0;
}

static int open_fixed_executable(const char *path)
{
    struct stat worker_stat;
    int worker_fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);

    if (worker_fd < 0 || fstat(worker_fd, &worker_stat) != 0 || !S_ISREG(worker_stat.st_mode) ||
        worker_stat.st_nlink != 1 || (worker_stat.st_mode & 022u) != 0 ||
        (worker_stat.st_mode & 0111u) == 0 ||
        (worker_stat.st_uid != 0u && worker_stat.st_uid != geteuid()))
    {
        if (worker_fd >= 0)
        {
            (void)close(worker_fd);
        }
        return -1;
    }
    return worker_fd;
}

int orchestrator_args_open_worker(void)
{
    return open_fixed_executable(ORCHESTRATOR_WORKER_PATH);
}

void orchestrator_args_print_usage(const char *program)
{
    fprintf(stderr,
            "usage: %s --job-id UUID "
            "--expected-size N --expected-sha256 HEX --work-root /abs/path "
            "--signer-endpoint /abs/v1.sock --signer-uid UID --signer-gid GID "
            "--signer-socket-gid GID --signer-timeout-ms N "
            "[--ifname awlink0] [--endpoint ipc:///run/ecu-ota/remote-handler/ecu-v1]\n",
            program);
}
