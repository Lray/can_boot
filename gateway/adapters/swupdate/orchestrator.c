#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "package_store.h"
#include "orchestrator_args.h"
#include "remote_handler.h"

#define ORCHESTRATOR_WORKER_TIMEOUT_MS 500000u
#define ORCHESTRATOR_RECEIVE_MIN_MS 600000
#define ORCHESTRATOR_RECEIVE_MS_PER_BYTE 60u

static int receive_timeout_for(uint64_t expected_bytes)
{
    uint64_t scaled = expected_bytes * ORCHESTRATOR_RECEIVE_MS_PER_BYTE;

    if (scaled < ORCHESTRATOR_RECEIVE_MIN_MS)
    {
        return ORCHESTRATOR_RECEIVE_MIN_MS;
    }
    return scaled > (uint64_t)INT_MAX ? INT_MAX : (int)scaled;
}

static uint64_t monotonic_milliseconds(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
    {
        return 0u;
    }
    return ((uint64_t)now.tv_sec * 1000u) + ((uint64_t)now.tv_nsec / 1000000u);
}

static int wait_worker(pid_t pid)
{
    uint64_t start = monotonic_milliseconds();
    struct timespec delay = {0, 100000000L};
    int status = 0;

    for (;;)
    {
        pid_t result = waitpid(pid, &status, WNOHANG);

        if (result == pid)
        {
            return WIFEXITED(status) ? WEXITSTATUS(status) : 28;
        }
        if (result < 0 && errno != EINTR)
        {
            return 28;
        }
        if (monotonic_milliseconds() - start >= ORCHESTRATOR_WORKER_TIMEOUT_MS)
        {
            (void)kill(pid, SIGTERM);
            (void)nanosleep(&delay, NULL);
            (void)kill(pid, SIGKILL);
            (void)waitpid(pid, &status, 0);
            return 26;
        }
        (void)nanosleep(&delay, NULL);
    }
}

static int launch_worker(const OrchestratorOptions_t *options, const char *job_path,
                         int worker_fd)
{
    pid_t pid = fork();

    if (pid < 0)
    {
        return 28;
    }
    if (pid == 0)
    {
        char *const worker_argv[] = {
            (char *)ORCHESTRATOR_WORKER_BASENAME, "--job-dir",    (char *)job_path,
            "--job-id",                           (char *)options->job_id,
            "--ifname",                           (char *)options->can_ifname,
            "--signer-endpoint",                  (char *)options->signer_endpoint,
            "--signer-uid",                       (char *)options->signer_uid,
            "--signer-gid",                       (char *)options->signer_gid,
            "--signer-socket-gid",                (char *)options->signer_socket_gid,
            "--signer-timeout-ms",                (char *)options->signer_timeout_ms,
            NULL,
        };
        char *const worker_env[] = {NULL};

        (void)fexecve(worker_fd, worker_argv, worker_env);
        _exit(28);
    }
    return wait_worker(pid);
}

int main(int argc, char **argv)
{
    OrchestratorOptions_t options;
    PackageStore_t store;
    RemoteHandler_t handler = {0};
    struct stat root_stat;
    uint8_t expected_sha256[PACKAGE_SHA256_SIZE];
    uint64_t expected_size = 0u;
    char job_path[1024];
    int root_fd = -1;
    int job_dir_fd = -1;
    int worker_fd = -1;
    int session_ready = 0;
    int handler_ready = 0;
    int exit_code = 1;
    int options_rc;

    (void)umask(0007u);
    if (orchestrator_args_parse(argc, argv, &options) != 0)
    {
        orchestrator_args_print_usage(argv[0]);
        return 2;
    }
    options_rc = orchestrator_args_validate(&options, &expected_size, expected_sha256);
    if (options_rc != 0)
    {
        fprintf(stderr, "orchestrator: invalid job options code=%d\n", options_rc);
        return 2;
    }
    worker_fd = orchestrator_args_open_worker();
    if (worker_fd < 0)
    {
        fprintf(stderr, "orchestrator: fixed worker is unavailable or unsafe\n");
        return 2;
    }
    root_fd = open(options.work_root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (root_fd < 0 || fstat(root_fd, &root_stat) != 0 || !S_ISDIR(root_stat.st_mode) ||
        root_stat.st_uid != geteuid() || (root_stat.st_mode & 0022u) != 0u ||
        mkdirat(root_fd, options.job_id, 0750) != 0)
    {
        fprintf(stderr, "orchestrator: cannot create exclusive job directory\n");
        goto out;
    }
    job_dir_fd = openat(root_fd, options.job_id, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (job_dir_fd < 0 ||
        snprintf(job_path, sizeof(job_path), "%s/%s", options.work_root, options.job_id) < 0 ||
        strlen(options.work_root) + strlen(options.job_id) + 2u > sizeof(job_path) ||
        package_store_init(&store, job_dir_fd, expected_size, expected_sha256) != 0)
    {
        fprintf(stderr, "orchestrator: invalid job storage\n");
        goto out;
    }
    session_ready = 1;
    if (remote_handler_init(&handler, options.endpoint,
                            receive_timeout_for(expected_size)) != 0)
    {
        fprintf(stderr, "orchestrator: cannot bind remote handler endpoint\n");
        goto out;
    }
    handler_ready = 1;
    fprintf(stderr, "orchestrator: READY job=%s expected_bytes=%llu\n", options.job_id,
            (unsigned long long)expected_size);

    for (;;)
    {
        char command[REMOTE_HANDLER_CMD_MAX];
        uint8_t body[PACKAGE_STORE_MAX_DATA];
        size_t body_len = 0u;
        int rc;

        if (remote_handler_receive(&handler, command, sizeof(command), body, sizeof(body),
                                   &body_len) != 0)
        {
            fprintf(stderr, "orchestrator: receive timeout or protocol failure\n");
            break;
        }
        if (store.state == PACKAGE_STORE_NEW)
        {
            rc = package_store_handle_init(&store, command, strlen(command));
            if (rc == 0)
            {
                rc = remote_handler_reply(&handler, "ACK:540000");
            }
        }
        else if (store.state == PACKAGE_STORE_RECEIVING)
        {
            rc = package_store_handle_data(&store, command, strlen(command), body, body_len);
            if (rc == 0 && store.state != PACKAGE_STORE_COMPLETE)
            {
                rc = remote_handler_reply(&handler, "ACK");
            }
            else if (rc == 0)
            {
                int worker_rc = 28;

                fprintf(stderr, "orchestrator: package accepted bytes=%llu\n",
                        (unsigned long long)store.received_size);
                worker_rc = launch_worker(&options, job_path, worker_fd);
                if (worker_rc != 0)
                {
                    fprintf(stderr, "orchestrator: worker terminal failure code=%d\n", worker_rc);
                }
                else
                {
                    rc = remote_handler_reply(&handler, "ACK");
                }
                if (rc == 0 && worker_rc == 0)
                {
                    exit_code = 0;
                }
                else
                {
                    exit_code = 1;
                }
            }
        }
        else
        {
            rc = PACKAGE_STORE_ERR_SEQUENCE;
        }
        if (rc != 0)
        {
            exit_code = 1;
        }
        if (store.state == PACKAGE_STORE_COMPLETE || rc != 0)
        {
            break;
        }
    }

out:
    if (handler_ready)
    {
        remote_handler_close(&handler);
    }
    if (session_ready && store.state != PACKAGE_STORE_COMPLETE)
    {
        package_store_abort(&store);
    }
    if (worker_fd >= 0)
    {
        (void)close(worker_fd);
    }
    if (job_dir_fd >= 0)
    {
        (void)close(job_dir_fd);
    }
    if (root_fd >= 0)
    {
        (void)close(root_fd);
    }
    memset(expected_sha256, 0, sizeof(expected_sha256));
    return exit_code;
}
