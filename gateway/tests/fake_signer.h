#ifndef FAKE_SIGNER_H
#define FAKE_SIGNER_H

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

typedef struct
{
    pthread_t thread;
    int listen_fd;
    int mismatch_request_id;
    const uint8_t *expected_seed;
    size_t expected_seed_len;
    char directory[64];
    char socket_path[108];
} FakeSignerSession_t;

/** Bind a Unix-socket fake signer, verify one request against the expected
 *  seed, and reply with the canned token. Session must outlive the request. */
void fake_signer_start(FakeSignerSession_t *session, const uint8_t *expected_seed,
                       size_t expected_seed_len, int mismatch_request_id);

/** Join the server thread and clean up the socket and temporary directory. */
int fake_signer_join(FakeSignerSession_t *session);

#endif