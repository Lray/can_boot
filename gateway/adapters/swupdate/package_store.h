#ifndef PACKAGE_STORE_H
#define PACKAGE_STORE_H

#include <stddef.h>
#include <stdint.h>

#include "package_input.h"
#include "profile.h"
#include "sha256.h"

#define PACKAGE_STORE_MAX_SIZE DEFAULT_SLOT_SIZE
#define PACKAGE_STORE_MAX_DATA (64u * 1024u)

#define PACKAGE_STORE_ERR_INVALID_ARG (-801)
#define PACKAGE_STORE_ERR_SEQUENCE (-802)
#define PACKAGE_STORE_ERR_PROTOCOL (-803)
#define PACKAGE_STORE_ERR_SIZE (-804)
#define PACKAGE_STORE_ERR_HASH (-805)
#define PACKAGE_STORE_ERR_STORAGE (-806)

#define PACKAGE_STORE_MEMBER_PARTIAL_NAME "image.bin.partial"

typedef enum
{
    PACKAGE_STORE_NEW = 0,
    PACKAGE_STORE_RECEIVING,
    PACKAGE_STORE_COMPLETE,
    PACKAGE_STORE_FAILED,
} PackageStoreState_t;

typedef struct
{
    int job_dir_fd;
    int partial_dir_fd;
    int member_fd;
    uint64_t expected_size;
    uint64_t received_size;
    uint8_t expected_sha256[PACKAGE_SHA256_SIZE];
    Sha256 sha256;
    PackageStoreState_t state;
} PackageStore_t;

int package_store_init(PackageStore_t *store, int job_dir_fd, uint64_t expected_size,
                      const uint8_t expected_sha256[PACKAGE_SHA256_SIZE]);
int package_store_handle_init(PackageStore_t *store, const char *command, size_t command_len);
int package_store_handle_data(PackageStore_t *store, const char *command, size_t command_len,
                             const uint8_t *body, size_t body_len);
void package_store_abort(PackageStore_t *store);

#endif
