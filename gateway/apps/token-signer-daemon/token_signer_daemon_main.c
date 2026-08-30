#include <errno.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "token_signer_daemon.h"
#include "token_signer_tee.h"

#define DAEMON_DEFAULT_IDLE_TIMEOUT_S 3600u

static const uint8_t expected_public_x963[64] = {
    0x9A, 0x57, 0x4E, 0x42, 0x9B, 0x51, 0xD2, 0x01,
    0x21, 0x71, 0x16, 0x37, 0xD8, 0x31, 0xA4, 0x95,
    0xE8, 0x1C, 0x2A, 0x8F, 0x8D, 0x49, 0xC5, 0x6A,
    0x61, 0x89, 0x28, 0xD5, 0x84, 0xA4, 0xB9, 0xD3,
    0x22, 0x32, 0x5C, 0x84, 0x5E, 0xD9, 0xEE, 0x46,
    0x3F, 0x25, 0xEA, 0xEA, 0x00, 0x8C, 0x94, 0x11,
    0x65, 0x9D, 0xC3, 0xB7, 0x77, 0x29, 0xFB, 0xB0,
    0x3D, 0x84, 0x4C, 0xD8, 0xF2, 0xF5, 0x1A, 0x80,
};

static int verify_tee_key(TokenSignerTee_t *tee)
{
    uint8_t public_key[64] = {0};

    return token_signer_tee_get_public_key(tee, public_key) == 0 &&
                   memcmp(public_key, expected_public_x963,
                          sizeof(public_key)) == 0
               ? 0
               : -1;
}

static void print_usage(const char *program)
{
    fprintf(stderr,
            "usage: %s --uid UID --gid GID --socket-gid GID --client-uid UID --client-gid GID "
            "[--endpoint /run/ecu-token-signer/v1.sock] [--idle-timeout N]\n",
            program);
}

static int parse_id(const char *text, unsigned long *value_out,
                    int permit_zero)
{
    char *end = NULL;
    unsigned long value;

    if (text == NULL || value_out == NULL || text[0] == '\0')
    {
        return -1;
    }
    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value > 65535u ||
        (!permit_zero && value == 0u))
    {
        return -1;
    }
    *value_out = value;
    return 0;
}

static int harden_process(void)
{
    struct rlimit no_core = {0u, 0u};

    return setrlimit(RLIMIT_CORE, &no_core) == 0 && prctl(PR_SET_DUMPABLE, 0) == 0 &&
                   prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) == 0
               ? 0
               : -1;
}

static int prepare_endpoint_directory(const char *endpoint, uid_t uid,
                                      gid_t socket_gid)
{
    char directory[512];
    const char *slash;
    struct stat directory_stat;
    int created = 0;

    if (endpoint == NULL || endpoint[0] != '/')
    {
        return -1;
    }
    slash = strrchr(endpoint, '/');
    if (slash == NULL || slash == endpoint ||
        (size_t)(slash - endpoint) >= sizeof(directory))
    {
        return -1;
    }
    memcpy(directory, endpoint, (size_t)(slash - endpoint));
    directory[slash - endpoint] = '\0';
    if (mkdir(directory, 0750) == 0)
    {
        created = 1;
    }
    else if (errno != EEXIST)
    {
        return -1;
    }
    if (lstat(directory, &directory_stat) != 0 ||
        !S_ISDIR(directory_stat.st_mode) || directory_stat.st_nlink != 2u ||
        (!created && (directory_stat.st_uid != uid ||
                      directory_stat.st_gid != socket_gid ||
                      (directory_stat.st_mode & 07777u) != 02750u)))
    {
        return -1;
    }
    return chown(directory, uid, socket_gid) == 0 && chmod(directory, 02750) == 0
               ? 0
               : -1;
}

int main(int argc, char **argv)
{
    const char *endpoint = TOKEN_SIGNER_DAEMON_DEFAULT_ENDPOINT;
    TokenSignerDaemonPolicy_t policy = {0};
    TokenSignerTee_t *tee = NULL;
    unsigned long idle_timeout = DAEMON_DEFAULT_IDLE_TIMEOUT_S;
    unsigned long value;
    unsigned int seen = 0u;
    int index;

    for (index = 1; index + 1 < argc; index += 2)
    {
        if (strcmp(argv[index], "--endpoint") == 0)
        {
            endpoint = argv[index + 1];
        }
        else if (strcmp(argv[index], "--uid") == 0 &&
                 parse_id(argv[index + 1], &value, 0) == 0)
        {
            policy.signer_uid = (uid_t)value;
            seen |= 1u << 0;
        }
        else if (strcmp(argv[index], "--gid") == 0 &&
                 parse_id(argv[index + 1], &value, 0) == 0)
        {
            policy.signer_gid = (gid_t)value;
            seen |= 1u << 1;
        }
        else if (strcmp(argv[index], "--socket-gid") == 0 &&
                 parse_id(argv[index + 1], &value, 0) == 0)
        {
            policy.socket_gid = (gid_t)value;
            seen |= 1u << 2;
        }
        else if (strcmp(argv[index], "--client-uid") == 0 &&
                 parse_id(argv[index + 1], &value, 1) == 0)
        {
            policy.client_uid = (uid_t)value;
            seen |= 1u << 3;
        }
        else if (strcmp(argv[index], "--client-gid") == 0 &&
                 parse_id(argv[index + 1], &value, 1) == 0)
        {
            policy.client_gid = (gid_t)value;
            seen |= 1u << 4;
        }
        else if (strcmp(argv[index], "--idle-timeout") == 0 &&
                 parse_id(argv[index + 1], &idle_timeout, 0) == 0 &&
                 idle_timeout <= 86400u)
        {
        }
        else
        {
            print_usage(argv[0]);
            return 2;
        }
    }
    if (index != argc || seen != 0x1Fu || geteuid() != 0u)
    {
        print_usage(argv[0]);
        return 2;
    }
    if (token_signer_tee_connect(&tee) != 0 || verify_tee_key(tee) != 0 ||
        prepare_endpoint_directory(endpoint, policy.signer_uid,
                                   policy.socket_gid) != 0 ||
        setgroups(1, &policy.signer_gid) != 0 ||
        setgid(policy.signer_gid) != 0 || setuid(policy.signer_uid) != 0 ||
        harden_process() != 0)
    {
        fprintf(stderr, "token-signer: secure startup failed\n");
        token_signer_tee_disconnect(tee);
        return 2;
    }
    index = token_signer_daemon_run(endpoint, tee, (unsigned int)idle_timeout,
                                    &policy);
    token_signer_tee_disconnect(tee);
    return index == 0 ? 0 : 1;
}
