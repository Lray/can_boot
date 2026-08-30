#include "security_access.h"
#include "security_token.h"

#include <string.h>

#define SECURITY_ACCESS_MAX_FAILED_ATTEMPTS 3U
#define SECURITY_ACCESS_DELAY_MS 5000U

typedef struct
{
    bool seed_valid;
    bool unlocked;
    uint8_t failed_attempts;
    uint8_t seed[SECURITY_ACCESS_SEED_SIZE];
    uint32_t delay_until_ms;
} security_access_state_t;

static security_access_state_t s_security;

static bool SecurityAccess_TimeReached(uint32_t now_ms, uint32_t due_ms)
{
  return ((int32_t)(now_ms - due_ms) >= 0);
}

static void SecurityAccess_ClearSeed(void)
{
  memset(s_security.seed, 0, sizeof(s_security.seed));
  s_security.seed_valid = false;
}

/* 锁定期（5 秒）到期时重置失败计数，避免三次错误密钥后测试设备被永久锁定。 */
static void SecurityAccess_ExpireLockout(uint32_t now_ms)
{
  if ((s_security.failed_attempts >= SECURITY_ACCESS_MAX_FAILED_ATTEMPTS) &&
      SecurityAccess_TimeReached(now_ms, s_security.delay_until_ms))
  {
    s_security.failed_attempts = 0U;
  }
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

void SecurityAccess_Poll(uint32_t now_ms)
{
    SecurityAccess_ExpireLockout(now_ms);
}

bool SecurityAccess_IsUnlocked(void)
{
    return s_security.unlocked;
}

security_access_result_t SecurityAccess_RequestSeed(uint32_t now_ms,
                                                            uint8_t seed[SECURITY_ACCESS_SEED_SIZE])
{
  if (seed == 0)
  {
    return SECURITY_ACCESS_RESULT_INVALID_ARG;
  }

  SecurityAccess_ExpireLockout(now_ms);
  if (s_security.failed_attempts >= SECURITY_ACCESS_MAX_FAILED_ATTEMPTS)
  {
    return SECURITY_ACCESS_RESULT_DELAY_ACTIVE;
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
                                                          uint16_t length,
                                                          uint32_t now_ms)
{
  security_token_result_t token_result;

  SecurityAccess_ExpireLockout(now_ms);
  if (s_security.failed_attempts >= SECURITY_ACCESS_MAX_FAILED_ATTEMPTS)
  {
    return SECURITY_ACCESS_RESULT_DELAY_ACTIVE;
  }

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
    s_security.failed_attempts = 0U;
    s_security.unlocked = true;
    SecurityAccess_ClearSeed();
    return SECURITY_ACCESS_RESULT_OK;
  }

  SecurityAccess_ClearSeed();
  s_security.failed_attempts++;
  if (s_security.failed_attempts >= SECURITY_ACCESS_MAX_FAILED_ATTEMPTS)
  {
    s_security.delay_until_ms = now_ms + SECURITY_ACCESS_DELAY_MS;
    return SECURITY_ACCESS_RESULT_EXCEEDED_ATTEMPTS;
  }

  return SECURITY_ACCESS_RESULT_INVALID_KEY;
}

void SecurityAccess_GetLockoutStatus(uint32_t now_ms,
                                         uint8_t *failed_attempts,
                                         uint32_t *remaining_delay_ms)
{
  if (failed_attempts != 0)
  {
    *failed_attempts = s_security.failed_attempts;
  }

  if (remaining_delay_ms != 0)
  {
    if ((s_security.failed_attempts >= SECURITY_ACCESS_MAX_FAILED_ATTEMPTS) &&
        !SecurityAccess_TimeReached(now_ms, s_security.delay_until_ms))
    {
      *remaining_delay_ms = s_security.delay_until_ms - now_ms;
    }
    else
    {
      *remaining_delay_ms = 0U;
    }
  }
}
