#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "package_store.h"

typedef struct
{
    char path[64];
    int fd;
} JobDir_t;

static void init_store(PackageStore_t *store, int job_dir_fd, const uint8_t *expected,
                       size_t expected_len)
{
    uint8_t digest[PACKAGE_SHA256_SIZE];

    sha256_compute(expected, expected_len, digest);
    assert(package_store_init(store, job_dir_fd, expected_len, digest) == 0);
    memset(digest, 0, sizeof(digest));
}

static void init_store_for_size(PackageStore_t *store, int job_dir_fd, uint64_t expected_size)
{
    static const uint8_t digest[PACKAGE_SHA256_SIZE];

    assert(package_store_init(store, job_dir_fd, expected_size, digest) == 0);
}

static void open_job_dir(JobDir_t *job)
{
    strcpy(job->path, "/tmp/gateway-package-store-XXXXXX");
    assert(mkdtemp(job->path) != NULL);
    job->fd = open(job->path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    assert(job->fd >= 0);
}

static void close_job_dir(const JobDir_t *job)
{
    assert(close(job->fd) == 0);
    assert(rmdir(job->path) == 0);
}

static void assert_partial_gone(int job_fd)
{
    struct stat partial_stat;

    assert(fstatat(job_fd, PACKAGE_INPUT_PARTIAL_DIRECTORY, &partial_stat,
                   AT_SYMLINK_NOFOLLOW) != 0);
    assert(errno == ENOENT);
}

static void assert_published_image(int job_fd, const uint8_t *expected, size_t expected_len)
{
    uint8_t buffer[64];
    struct stat input_stat;
    struct stat member_stat;
    int input_fd = openat(job_fd, PACKAGE_INPUT_DIRECTORY,
                          O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    int member_fd;

    assert(input_fd >= 0);
    assert(fstat(input_fd, &input_stat) == 0);
    assert(S_ISDIR(input_stat.st_mode));
    assert((input_stat.st_mode & 0777u) == 0700u);
    assert(input_stat.st_uid == geteuid());
    member_fd = openat(input_fd, PACKAGE_IMAGE_NAME, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    assert(member_fd >= 0);
    assert(fstat(member_fd, &member_stat) == 0);
    assert(S_ISREG(member_stat.st_mode));
    assert((member_stat.st_mode & 0777u) == 0400u);
    assert(member_stat.st_nlink == 1);
    assert(member_stat.st_uid == geteuid());
    assert(read(member_fd, buffer, sizeof(buffer)) == (ssize_t)expected_len);
    assert(memcmp(buffer, expected, expected_len) == 0);
    assert(close(member_fd) == 0);
    assert(close(input_fd) == 0);
}

static void send_data_assert_ack(PackageStore_t *store, const char *command, const void *body,
                                 size_t body_len)
{
    assert(package_store_handle_data(store, command, strlen(command),
                                     (const uint8_t *)body, body_len) == 0);
}

static void remove_published_tree(int job_fd)
{
    int input_fd = openat(job_fd, PACKAGE_INPUT_DIRECTORY,
                          O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);

    assert(input_fd >= 0);
    assert(unlinkat(input_fd, PACKAGE_IMAGE_NAME, 0) == 0);
    assert(close(input_fd) == 0);
    assert(unlinkat(job_fd, PACKAGE_INPUT_DIRECTORY, AT_REMOVEDIR) == 0);
}

static void test_publishes_image_atomically(void)
{
    static const uint8_t image[] = {5u, 6u, 7u, 8u, 9u};
    JobDir_t job;
    PackageStore_t store;

    open_job_dir(&job);
    init_store(&store, job.fd, image, sizeof(image));
    assert(package_store_handle_init(&store, "INIT:5", 6u) == 0);
    assert(store.expected_size == 5u && store.received_size == 0u);
    send_data_assert_ack(&store, "DATA", image, 2u);
    send_data_assert_ack(&store, "DATA", image + 2u, 3u);
    assert(store.state == PACKAGE_STORE_COMPLETE);
    assert(store.received_size == 5u);
    assert_published_image(job.fd, image, sizeof(image));
    assert_partial_gone(job.fd);

    package_store_abort(&store);
    assert_published_image(job.fd, image, sizeof(image));
    assert(package_store_handle_data(&store, "DATA", 4u, (const uint8_t *)image, sizeof(image)) ==
           PACKAGE_STORE_ERR_SEQUENCE);
    remove_published_tree(job.fd);
    close_job_dir(&job);
}

static void test_rejects_malformed_init(void)
{
    static const char *protocol_rejected[] = {"INIT",    "INIT:",  "INIT:05",
                                              "INIT:x1", "INIT:-3"};
    static const char *size_rejected[] = {"INIT:0", "INIT:131073",
                                          "INIT:99999999999999999999"};
    size_t index;

    for (index = 0u; index < sizeof(protocol_rejected) / sizeof(protocol_rejected[0]); ++index)
    {
        const char *command = protocol_rejected[index];
        JobDir_t job;
        PackageStore_t store;

        open_job_dir(&job);
        init_store(&store, job.fd, (const uint8_t *)"x", 1u);
        assert(package_store_handle_init(&store, command, strlen(command)) ==
               PACKAGE_STORE_ERR_PROTOCOL);
        assert(store.state == PACKAGE_STORE_FAILED);
        assert_partial_gone(job.fd);
        package_store_abort(&store);
        assert(store.state == PACKAGE_STORE_FAILED);
        close_job_dir(&job);
    }
    for (index = 0u; index < sizeof(size_rejected) / sizeof(size_rejected[0]); ++index)
    {
        const char *command = size_rejected[index];
        JobDir_t job;
        PackageStore_t store;

        open_job_dir(&job);
        init_store(&store, job.fd, (const uint8_t *)"x", 1u);
        assert(package_store_handle_init(&store, command, strlen(command)) ==
               PACKAGE_STORE_ERR_SIZE);
        assert(store.state == PACKAGE_STORE_FAILED);
        assert_partial_gone(job.fd);
        package_store_abort(&store);
        assert(store.state == PACKAGE_STORE_FAILED);
        close_job_dir(&job);
    }
}

static void test_rejects_data_overflow_and_cleans(void)
{
    JobDir_t job;
    PackageStore_t store;

    open_job_dir(&job);
    init_store(&store, job.fd, (const uint8_t *)"abcd", 4u);
    assert(package_store_handle_init(&store, "INIT:4", 6u) == 0);
    assert(package_store_handle_data(&store, "DATA", 4u, (const uint8_t *)"abcde", 5u) ==
           PACKAGE_STORE_ERR_SIZE);
    assert(store.state == PACKAGE_STORE_FAILED);
    assert_partial_gone(job.fd);
    close_job_dir(&job);
}

static void test_rejects_announced_size_mismatch_and_cleans(void)
{
    JobDir_t job;
    PackageStore_t store;

    open_job_dir(&job);
    init_store(&store, job.fd, (const uint8_t *)"abc", 3u);
    assert(package_store_handle_init(&store, "INIT:4", 6u) == PACKAGE_STORE_ERR_SIZE);
    assert(store.state == PACKAGE_STORE_FAILED);
    assert_partial_gone(job.fd);
    close_job_dir(&job);
}

static void test_rejects_hash_mismatch_without_publication(void)
{
    JobDir_t job;
    PackageStore_t store;

    open_job_dir(&job);
    init_store(&store, job.fd, (const uint8_t *)"abcde", 5u);
    assert(package_store_handle_init(&store, "INIT:5", 6u) == 0);
    assert(package_store_handle_data(&store, "DATA", 4u, (const uint8_t *)"abcdf", 5u) ==
           PACKAGE_STORE_ERR_HASH);
    assert(store.state == PACKAGE_STORE_FAILED);
    assert_partial_gone(job.fd);
    close_job_dir(&job);
}

static void test_rejects_bad_data_command_and_cleans(void)
{
    static uint8_t oversized[PACKAGE_STORE_MAX_DATA + 1u];
    JobDir_t job;
    PackageStore_t store;

    memset(oversized, 0xA5u, sizeof(oversized));
    open_job_dir(&job);
    init_store_for_size(&store, job.fd, 70000u);
    assert(package_store_handle_init(&store, "INIT:70000", 10u) == 0);
    assert(package_store_handle_data(&store, "DATA", 4u, NULL, 0u) ==
           PACKAGE_STORE_ERR_PROTOCOL);
    assert(package_store_handle_data(&store, "DATA2", 5u, (const uint8_t *)oversized, 4u) ==
           PACKAGE_STORE_ERR_SEQUENCE);
    assert(store.state == PACKAGE_STORE_FAILED);
    assert_partial_gone(job.fd);
    close_job_dir(&job);
}

static void test_rejects_oversize_body_and_cleans(void)
{
    static uint8_t oversized[PACKAGE_STORE_MAX_DATA + 1u];
    JobDir_t job;
    PackageStore_t store;

    memset(oversized, 0x5Au, sizeof(oversized));
    open_job_dir(&job);
    init_store_for_size(&store, job.fd, 70000u);
    assert(package_store_handle_init(&store, "INIT:70000", 10u) == 0);
    assert(package_store_handle_data(&store, "DATA", 4u, (const uint8_t *)oversized,
                                     sizeof(oversized)) == PACKAGE_STORE_ERR_PROTOCOL);
    assert(store.state == PACKAGE_STORE_FAILED);
    assert_partial_gone(job.fd);
    close_job_dir(&job);
}

static void test_enforces_command_sequence(void)
{
    JobDir_t job;
    PackageStore_t store;

    open_job_dir(&job);
    init_store(&store, job.fd, (const uint8_t *)"xy", 2u);
    assert(package_store_handle_data(&store, "DATA", 4u, (const uint8_t *)"x", 1u) ==
           PACKAGE_STORE_ERR_SEQUENCE);
    assert(store.state == PACKAGE_STORE_NEW);
    assert(package_store_handle_init(&store, "INIT:2", 6u) == 0);
    assert(package_store_handle_init(&store, "INIT:2", 6u) == PACKAGE_STORE_ERR_SEQUENCE);
    assert(store.state == PACKAGE_STORE_RECEIVING);
    send_data_assert_ack(&store, "DATA", "xy", 2u);
    assert(store.state == PACKAGE_STORE_COMPLETE);
    remove_published_tree(job.fd);
    close_job_dir(&job);
}

static void test_abort_discards_incomplete_transfer(void)
{
    JobDir_t job;
    PackageStore_t store;

    open_job_dir(&job);
    init_store(&store, job.fd, (const uint8_t *)"abc", 3u);
    assert(package_store_handle_init(&store, "INIT:3", 6u) == 0);
    send_data_assert_ack(&store, "DATA", "ab", 2u);
    package_store_abort(&store);
    assert(store.state == PACKAGE_STORE_FAILED);
    assert_partial_gone(job.fd);
    close_job_dir(&job);
}

int main(void)
{
    test_publishes_image_atomically();
    test_rejects_malformed_init();
    test_rejects_data_overflow_and_cleans();
    test_rejects_announced_size_mismatch_and_cleans();
    test_rejects_hash_mismatch_without_publication();
    test_rejects_bad_data_command_and_cleans();
    test_rejects_oversize_body_and_cleans();
    test_enforces_command_sequence();
    test_abort_discards_incomplete_transfer();
    return 0;
}
