#include "package_store.h"

#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "fs_util.h"

static void fail_store(PackageStore_t *store)
{
    if (store->member_fd >= 0)
    {
        (void)close(store->member_fd);
        store->member_fd = -1;
    }
    if (store->partial_dir_fd >= 0)
    {
        (void)unlinkat(store->partial_dir_fd, PACKAGE_STORE_MEMBER_PARTIAL_NAME, 0);
        (void)unlinkat(store->partial_dir_fd, PACKAGE_IMAGE_NAME, 0);
        (void)close(store->partial_dir_fd);
        store->partial_dir_fd = -1;
        (void)unlinkat(store->job_dir_fd, PACKAGE_INPUT_PARTIAL_DIRECTORY, AT_REMOVEDIR);
        (void)unlinkat(store->job_dir_fd, PACKAGE_INPUT_DIRECTORY, AT_REMOVEDIR);
    }
    store->state = PACKAGE_STORE_FAILED;
}

static int parse_init_size(const char *command, size_t command_len, uint64_t *size_out)
{
    uint64_t value = 0u;
    size_t index;

    if (command == NULL || size_out == NULL || command_len <= 5u ||
        memcmp(command, "INIT:", 5u) != 0)
    {
        return PACKAGE_STORE_ERR_PROTOCOL;
    }
    if (command_len > 6u && command[5] == '0')
    {
        return PACKAGE_STORE_ERR_PROTOCOL;
    }
    for (index = 5u; index < command_len; ++index)
    {
        char digit = command[index];

        if (digit < '0' || digit > '9')
        {
            return PACKAGE_STORE_ERR_PROTOCOL;
        }
        if (value > (UINT64_MAX - (uint64_t)(digit - '0')) / 10u)
        {
            return PACKAGE_STORE_ERR_SIZE;
        }
        value = (value * 10u) + (uint64_t)(digit - '0');
    }
    if (value == 0u || value > PACKAGE_STORE_MAX_SIZE)
    {
        return PACKAGE_STORE_ERR_SIZE;
    }
    *size_out = value;
    return 0;
}

int package_store_init(PackageStore_t *store, int job_dir_fd, uint64_t expected_size,
                      const uint8_t expected_sha256[PACKAGE_SHA256_SIZE])
{
    if (store == NULL || job_dir_fd < 0 || expected_sha256 == NULL || expected_size == 0u ||
        expected_size > PACKAGE_STORE_MAX_SIZE)
    {
        return PACKAGE_STORE_ERR_INVALID_ARG;
    }
    memset(store, 0, sizeof(*store));
    store->job_dir_fd = job_dir_fd;
    store->partial_dir_fd = -1;
    store->member_fd = -1;
    store->expected_size = expected_size;
    memcpy(store->expected_sha256, expected_sha256, sizeof(store->expected_sha256));
    store->state = PACKAGE_STORE_NEW;
    return 0;
}

int package_store_handle_init(PackageStore_t *store, const char *command, size_t command_len)
{
    uint64_t announced_size = 0u;
    int rc;

    if (store == NULL)
    {
        return PACKAGE_STORE_ERR_INVALID_ARG;
    }
    if (store->state != PACKAGE_STORE_NEW)
    {
        return PACKAGE_STORE_ERR_SEQUENCE;
    }
    rc = parse_init_size(command, command_len, &announced_size);
    if (rc != 0 || announced_size != store->expected_size)
    {
        fail_store(store);
        return rc != 0 ? rc : PACKAGE_STORE_ERR_SIZE;
    }
    if (mkdirat(store->job_dir_fd, PACKAGE_INPUT_PARTIAL_DIRECTORY, 0700) != 0)
    {
        fail_store(store);
        return PACKAGE_STORE_ERR_STORAGE;
    }
    store->partial_dir_fd =
        openat(store->job_dir_fd, PACKAGE_INPUT_PARTIAL_DIRECTORY,
               O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (store->partial_dir_fd < 0)
    {
        (void)unlinkat(store->job_dir_fd, PACKAGE_INPUT_PARTIAL_DIRECTORY, AT_REMOVEDIR);
        fail_store(store);
        return PACKAGE_STORE_ERR_STORAGE;
    }
    store->member_fd = openat(store->partial_dir_fd, PACKAGE_STORE_MEMBER_PARTIAL_NAME,
                              O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (store->member_fd < 0)
    {
        fail_store(store);
        return PACKAGE_STORE_ERR_STORAGE;
    }
    store->received_size = 0u;
    sha256_init(&store->sha256);
    store->state = PACKAGE_STORE_RECEIVING;
    return 0;
}

int package_store_handle_data(PackageStore_t *store, const char *command, size_t command_len,
                             const uint8_t *body, size_t body_len)
{
    uint8_t digest[PACKAGE_SHA256_SIZE];

    if (store == NULL || (body == NULL && body_len > 0u))
    {
        return PACKAGE_STORE_ERR_INVALID_ARG;
    }
    if (store->state != PACKAGE_STORE_RECEIVING)
    {
        return PACKAGE_STORE_ERR_SEQUENCE;
    }
    if (command_len != 4u || command == NULL || memcmp(command, "DATA", 4u) != 0 ||
        body_len == 0u || body_len > PACKAGE_STORE_MAX_DATA)
    {
        fail_store(store);
        return PACKAGE_STORE_ERR_PROTOCOL;
    }
    if ((uint64_t)body_len > store->expected_size - store->received_size)
    {
        fail_store(store);
        return PACKAGE_STORE_ERR_SIZE;
    }
    if (fs_write_all(store->member_fd, body, body_len) != 0)
    {
        fail_store(store);
        return PACKAGE_STORE_ERR_STORAGE;
    }
    sha256_update(&store->sha256, body, body_len);
    store->received_size += body_len;
    if (store->received_size != store->expected_size)
    {
        return 0;
    }
    sha256_final(&store->sha256, digest);
    if (memcmp(digest, store->expected_sha256, sizeof(digest)) != 0)
    {
        memset(digest, 0, sizeof(digest));
        fail_store(store);
        return PACKAGE_STORE_ERR_HASH;
    }
    memset(digest, 0, sizeof(digest));
    if (fchmod(store->member_fd, 0400) != 0 || fsync(store->member_fd) != 0 ||
        close(store->member_fd) != 0)
    {
        store->member_fd = -1;
        fail_store(store);
        return PACKAGE_STORE_ERR_STORAGE;
    }
    store->member_fd = -1;
    if (fs_rename_noreplace(store->partial_dir_fd, PACKAGE_STORE_MEMBER_PARTIAL_NAME,
                            store->partial_dir_fd, PACKAGE_IMAGE_NAME) != 0 ||
        fsync(store->partial_dir_fd) != 0 ||
        fs_rename_noreplace(store->job_dir_fd, PACKAGE_INPUT_PARTIAL_DIRECTORY, store->job_dir_fd,
                            PACKAGE_INPUT_DIRECTORY) != 0 ||
        fsync(store->job_dir_fd) != 0)
    {
        fail_store(store);
        return PACKAGE_STORE_ERR_STORAGE;
    }
    (void)close(store->partial_dir_fd);
    store->partial_dir_fd = -1;
    store->state = PACKAGE_STORE_COMPLETE;
    return 0;
}

void package_store_abort(PackageStore_t *store)
{
    if (store != NULL && (store->state == PACKAGE_STORE_NEW ||
                          store->state == PACKAGE_STORE_RECEIVING))
    {
        fail_store(store);
    }
}
