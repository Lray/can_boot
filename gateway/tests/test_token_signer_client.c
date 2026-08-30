#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "fake_signer.h"
#include "profile.h"
#include "token_signer_client.h"

static const uint8_t challenge_seed[] = {1u, 2u, 3u, 4u};

static void run_response_case(int mismatch_request_id, int expected_rc)
{
    FakeSignerSession_t server = {0};
    TokenSignerClient_t client = {0};
    uint8_t token[SECURITY_ACCESS_TOKEN_MAX_SIZE] = {0};
    size_t token_len = 0u;

    fake_signer_start(&server, challenge_seed, sizeof(challenge_seed),
                      mismatch_request_id);
    client.endpoint = server.socket_path;
    client.expected_peer_uid = geteuid();
    client.expected_peer_gid = getegid();
    client.expected_socket_uid = geteuid();
    client.expected_socket_gid = getegid();
    client.expected_socket_mode = 0660u;
    client.timeout_ms = TOKEN_SIGNER_DEFAULT_TIMEOUT_MS;
    assert(token_signer_client_issue(&client, challenge_seed, sizeof(challenge_seed),
                                     token, sizeof(token), &token_len) == expected_rc);
    if (expected_rc == 0)
    {
        static const uint8_t expected_token[] = {0xD2u, 0x84u, 0x40u};

        assert(token_len == sizeof(expected_token));
        assert(memcmp(token, expected_token, sizeof(expected_token)) == 0);
    }
    else
    {
        assert(token_len == 0u);
    }
    assert(fake_signer_join(&server) == 0);
}

int main(void)
{
    run_response_case(0, 0);
    run_response_case(1, TOKEN_SIGNER_ERR_RESPONSE);
    return 0;
}