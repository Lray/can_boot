#ifndef MCU_THIRDPARTY_TINYCRYPT_IWDG_COMPAT_H
#define MCU_THIRDPARTY_TINYCRYPT_IWDG_COMPAT_H

static inline void IWDG_Feed(void)
{
  /* The MCU COSE verifier reuses MCUboot TinyCrypt sources; Can has no IWDG instance. */
}

#endif /* MCU_THIRDPARTY_TINYCRYPT_IWDG_COMPAT_H */
