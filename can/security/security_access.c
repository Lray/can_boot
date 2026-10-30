#include "security_access.h"
#include "security_token.h"

#include <string.h>

typedef struct
{
    bool seed_valid;
    bool unlocked;
    uint8_t seed[SECURITY_ACCESS_SEED_SIZE];
} security_access_state_t;

static security_access_state_t s_security;

static void SecurityAccess_ClearSeed(void)
{
  memset(s_security.seed, 0, sizeof(s_security.seed));
  s_security.seed_valid = false;
}

static bool SecurityAccess_BuildSeed(uint8_t seed[SECURITY_ACCESS_SEED_SIZE])
{
  uint16_t index;

  if (!SecurityAccess_GetEntropy(seed, SECURITY_ACCESS_SEED_SIZE))
  {
    return false;
  }

  for (index = 0U; index < SECURITY_ACCESS_SEED_SIZE; ++index)
  {
    if (seed[index] != 0U)
    {
      return true;
    }
  }

  memset(seed, 0, SECURITY_ACCESS_SEED_SIZE);
  return false;
}

void SecurityAccess_Init(void)
{
  memset(&s_security, 0, sizeof(s_security));
}

void SecurityAccess_ClearUnlock(void)
{
    s_security.unlocked = false;
    SecurityAccess_ClearSeed();
}

bool SecurityAccess_IsUnlocked(void)
{
    return s_security.unlocked;
}

security_access_result_t SecurityAccess_RequestSeed(
    uint8_t seed[SECURITY_ACCESS_SEED_SIZE])
{
  if (seed == 0)
  {
    return SECURITY_ACCESS_RESULT_INVALID_ARG;
  }

  if (s_security.unlocked)
  {
    memset(seed, 0, SECURITY_ACCESS_SEED_SIZE);
    return SECURITY_ACCESS_RESULT_OK;
  }

  SecurityAccess_ClearSeed();
  memset(seed, 0, SECURITY_ACCESS_SEED_SIZE);
  if (!SecurityAccess_BuildSeed(s_security.seed))
  {
    return SECURITY_ACCESS_RESULT_ENTROPY_UNAVAILABLE;
  }
  memcpy(seed, s_security.seed, SECURITY_ACCESS_SEED_SIZE);
  s_security.seed_valid = true;
  return SECURITY_ACCESS_RESULT_OK;
}

security_access_result_t SecurityAccess_SubmitKey(const uint8_t *key,
                                                  uint16_t length)
{
  security_token_result_t token_result;

  if ((key == 0) ||
      (length == 0U) ||
      (length > SECURITY_TOKEN_MAX_SIZE))
  {
    return SECURITY_ACCESS_RESULT_INVALID_ARG;
  }

  if (!s_security.seed_valid)
  {
    return SECURITY_ACCESS_RESULT_SEQUENCE_ERROR;
  }

  token_result = SecurityToken_VerifyOtaEntry(s_security.seed,
                                              key,
                                              length);
  if (token_result == SECURITY_TOKEN_RESULT_OK)
  {
    s_security.unlocked = true;
    SecurityAccess_ClearSeed();
    return SECURITY_ACCESS_RESULT_OK;
  }

  SecurityAccess_ClearSeed();
  return SECURITY_ACCESS_RESULT_INVALID_KEY;
}
