#ifndef ECU_THIRDPARTY_TINYCRYPT_IWDG_COMPAT_H
#define ECU_THIRDPARTY_TINYCRYPT_IWDG_COMPAT_H

static inline void IWDG_Feed(void)
{
  /* The ECU COSE verifier reuses MCUboot TinyCrypt sources; Can has no IWDG instance. */
}

#endif /* ECU_THIRDPARTY_TINYCRYPT_IWDG_COMPAT_H */
