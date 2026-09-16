#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <zmq.h>

#include "update_job.h"
#include "package_store.h"
#include "mcu_updater_args.h"
#include "remote_handler.h"
#include "byte_order.h"
#include "mcu_registry.h"
#include "shared/mcu_identity.h"

#define MCU_UPDATER_JOB_ID_SIZE 37u

typedef struct
{
    RemoteHandler_t handler;
    uint8_t identity[MCU_IDENTITY_SIZE];
} McuEndpoint_t;

static int harden_process(void)
{
    struct rlimit no_core = {0u, 0u};

    (void)umask(0077u);
    return setrlimit(RLIMIT_CORE, &no_core) == 0 && prctl(PR_SET_DUMPABLE, 0) == 0 ? 0 : -1;
}

static int random_bytes(uint8_t *buffer, size_t length)
{
    size_t offset = 0u;

    while (offset < length)
    {
        ssize_t count = getrandom(buffer + offset, length - offset, 0);

        if (count < 0 && errno == EINTR)
        {
            continue;
        }
        if (count <= 0)
        {
            return -1;
        }
        offset += (size_t)count;
    }
    return 0;
}

static int create_job_directory(int root_fd, const char *work_root,
                                char job_id[MCU_UPDATER_JOB_ID_SIZE],
                                char *job_path, size_t job_path_cap, int *job_fd_out)
{
    uint8_t id[16];
    int attempt;

    for (attempt = 0; attempt < 8; ++attempt)
    {
        int length;
        int job_fd;

        if (random_bytes(id, sizeof(id)) != 0)
        {
            return -1;
        }
        id[6] = (uint8_t)((id[6] & 0x0fu) | 0x40u);
        id[8] = (uint8_t)((id[8] & 0x3fu) | 0x80u);
        length = snprintf(job_id, MCU_UPDATER_JOB_ID_SIZE,
                          "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
                          "%02x%02x%02x%02x%02x%02x",
                          id[0], id[1], id[2], id[3], id[4], id[5], id[6], id[7],
                          id[8], id[9], id[10], id[11], id[12], id[13], id[14], id[15]);
        memset(id, 0, sizeof(id));
        if (length != (int)MCU_UPDATER_JOB_ID_SIZE - 1 ||
            snprintf(job_path, job_path_cap, "%s/%s", work_root, job_id) < 0 ||
            strlen(work_root) + MCU_UPDATER_JOB_ID_SIZE > job_path_cap)
        {
            return -1;
        }
        if (mkdirat(root_fd, job_id, 0700) != 0)
        {
            if (errno == EEXIST)
            {
                continue;
            }
            return -1;
        }
        job_fd = openat(root_fd, job_id, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (job_fd < 0)
        {
            (void)unlinkat(root_fd, job_id, AT_REMOVEDIR);
            return -1;
        }
        *job_fd_out = job_fd;
        return 0;
    }
    return -1;
}

static void make_mcu_config(const McuUpdaterOptions_t *options,
                            const uint8_t identity[MCU_IDENTITY_SIZE],
                            McuUpdateConfig_t *config)
{
    memset(config, 0, sizeof(*config));
    config->can_ifname = options->can_ifname;
    config->signer_endpoint = options->signer_endpoint;
    config->signer_uid = (uid_t)strtoul(options->signer_uid, NULL, 10);
    config->signer_gid = (gid_t)strtoul(options->signer_gid, NULL, 10);
    config->signer_socket_gid = (gid_t)strtoul(options->signer_socket_gid, NULL, 10);
    config->signer_timeout_ms = (uint32_t)strtoul(options->signer_timeout_ms, NULL, 10);
    config->target_identity = identity;
}

static void close_endpoints(McuEndpoint_t endpoints[], size_t count)
{
    while (count > 0u)
    {
        remote_handler_close(&endpoints[--count].handler);
    }
}

static int open_endpoints(const McuUpdaterOptions_t *options,
                          McuEndpoint_t endpoints[], size_t *count_out)
{
    McuRegistry registry;
    size_t index;
    size_t registered_count;
    int rc = mcu_registry_open(&registry, options->can_ifname, false);

    if (rc != 0 || registry.count == 0u)
    {
        if (rc == 0) mcu_registry_close(&registry);
        return -1;
    }
    registered_count = registry.count;
    for (index = 0u; index < registered_count; ++index)
    {
        const CO_LSS_address_t *identity = &registry.devices[index].identity;
        char endpoint[sizeof(endpoints[index].handler.endpoint_path)];
        int length = snprintf(endpoint, sizeof(endpoint),
                              "%s-%08" PRIX32 "-%08" PRIX32 "-%08" PRIX32 "-%08" PRIX32,
                              options->endpoint_base, identity->identity.vendorID,
                              identity->identity.productCode,
                              identity->identity.revisionNumber,
                              identity->identity.serialNumber);

        if (length <= 0 || (size_t)length >= sizeof(endpoint)) break;
        for (size_t field = 0u; field < 4u; ++field)
        {
            byte_order_put_u32_be(endpoints[index].identity + field * 4u,
                                  identity->addr[field]);
        }
        if (remote_handler_init(&endpoints[index].handler, endpoint, INT_MAX) != 0) break;
    }
    mcu_registry_close(&registry);
    if (index == 0u || index != registered_count)
    {
        close_endpoints(endpoints, index);
        return -1;
    }
    *count_out = index;
    return 0;
}

static McuEndpoint_t *wait_endpoint(McuEndpoint_t endpoints[], size_t count)
{
    zmq_pollitem_t items[MCU_REGISTRY_CAPACITY];

    for (size_t index = 0u; index < count; ++index)
    {
        items[index] = (zmq_pollitem_t){.socket = endpoints[index].handler.socket,
                                       .events = ZMQ_POLLIN};
    }
    for (;;)
    {
        int rc = zmq_poll(items, (int)count, -1);
        if (rc < 0 && errno == EINTR) continue;
        if (rc <= 0) return NULL;
        for (size_t index = 0u; index < count; ++index)
        {
            if ((items[index].revents & ZMQ_POLLIN) != 0) return &endpoints[index];
        }
    }
}

static void log_image_digest(const char *job_id, const PackageStore_t *store)
{
    char digest_text[(PACKAGE_SHA256_SIZE * 2u) + 1u];
    size_t index;

    for (index = 0u; index < PACKAGE_SHA256_SIZE; ++index)
    {
        (void)snprintf(digest_text + (index * 2u), 3u, "%02x", store->image_sha256[index]);
    }
    fprintf(stderr, "mcu-updater: package accepted job=%s bytes=%llu sha256=%s\n",
            job_id, (unsigned long long)store->received_size, digest_text);
    memset(digest_text, 0, sizeof(digest_text));
}

int main(int argc, char **argv)
{
    McuUpdaterOptions_t options;
    McuEndpoint_t endpoints[MCU_REGISTRY_CAPACITY] = {0};
    size_t endpoint_count = 0u;
    struct stat root_stat;
    int root_fd = -1;

    if (mcu_updater_args_parse(argc, argv, &options) != 0 || harden_process() != 0)
    {
        mcu_updater_args_print_usage(argv[0]);
        return 2;
    }
    if (mcu_updater_args_validate(&options) != 0)
    {
        fprintf(stderr, "mcu-updater: invalid device options\n");
        return 2;
    }
    root_fd = open(options.work_root, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (root_fd < 0 || fstat(root_fd, &root_stat) != 0 || !S_ISDIR(root_stat.st_mode) ||
        root_stat.st_uid != geteuid() || (root_stat.st_mode & 0022u) != 0u)
    {
        fprintf(stderr, "mcu-updater: unsafe work root\n");
        goto out;
    }
    if (open_endpoints(&options, endpoints, &endpoint_count) != 0)
    {
        fprintf(stderr, "mcu-updater: cannot bind registered target endpoints\n");
        goto out;
    }
    fprintf(stderr, "mcu-updater: READY targets=%zu\n", endpoint_count);

    for (;;)
    {
        PackageStore_t store;
        char command[REMOTE_HANDLER_CMD_MAX];
        uint8_t body[PACKAGE_STORE_MAX_DATA];
        char job_id[MCU_UPDATER_JOB_ID_SIZE];
        char job_path[PATH_MAX];
        size_t body_len = 0u;
        int job_fd = -1;
        int store_ready = 0;
        int final_reply_sent = 0;
        int rc;
        McuEndpoint_t *endpoint = wait_endpoint(endpoints, endpoint_count);

        memset(&store, 0, sizeof(store));
        if (endpoint == NULL ||
            remote_handler_receive(&endpoint->handler, command, sizeof(command), body, sizeof(body),
                                   &body_len) != 0)
        {
            fprintf(stderr, "mcu-updater: remote receive failure\n");
            break;
        }
        if (body_len != 0u ||
            create_job_directory(root_fd, options.work_root, job_id, job_path,
                                 sizeof(job_path), &job_fd) != 0 ||
            package_store_init(&store, job_fd) != 0)
        {
            (void)remote_handler_reply(&endpoint->handler, "NACK");
            if (job_fd >= 0)
            {
                (void)close(job_fd);
                (void)unlinkat(root_fd, job_id, AT_REMOVEDIR);
            }
            continue;
        }
        store_ready = 1;
        rc = package_store_handle_init(&store, command, strlen(command));
        if (rc == 0)
        {
            char reply[32];
            int reply_len = snprintf(reply, sizeof(reply), "ACK:%u", MCU_UPDATE_REMOTE_WAIT_MS);

            rc = reply_len > 0 && (size_t)reply_len < sizeof(reply)
                     ? remote_handler_reply(&endpoint->handler, reply)
                     : -1;
        }
        else
        {
            (void)remote_handler_reply(&endpoint->handler, "NACK");
        }

        while (rc == 0 && store.state == PACKAGE_STORE_RECEIVING)
        {
            McuUpdateConfig_t config;
            McuUpdateResult_t result = {0};
            int update_rc;

            if (remote_handler_receive(&endpoint->handler, command, sizeof(command), body, sizeof(body),
                                       &body_len) != 0)
            {
                rc = -1;
                break;
            }
            rc = package_store_handle_data(&store, command, strlen(command), body, body_len);
            if (rc == 0 && store.state == PACKAGE_STORE_RECEIVING)
            {
                rc = remote_handler_reply(&endpoint->handler, "ACK");
                continue;
            }
            if (rc != 0)
            {
                (void)remote_handler_reply(&endpoint->handler, "NACK");
                break;
            }
            log_image_digest(job_id, &store);
            make_mcu_config(&options, endpoint->identity, &config);
            update_rc = mcu_update_run_job(job_path, &config, &result);
            mcu_update_log_result("mcu-updater", update_rc, &result);
            rc = remote_handler_reply(&endpoint->handler, update_rc == 0 ? "ACK" : "NACK");
            final_reply_sent = rc == 0;
        }

        if (store_ready)
        {
            package_store_discard(&store);
        }
        if (job_fd >= 0)
        {
            (void)close(job_fd);
            if (unlinkat(root_fd, job_id, AT_REMOVEDIR) != 0)
            {
                fprintf(stderr, "mcu-updater: cannot remove session job=%s\n", job_id);
                break;
            }
        }
        if (!final_reply_sent && rc != 0)
        {
            fprintf(stderr, "mcu-updater: transaction failed job=%s\n", job_id);
        }
    }

out:
    close_endpoints(endpoints, endpoint_count);
    if (root_fd >= 0)
    {
        (void)close(root_fd);
    }
    return 1;
}
