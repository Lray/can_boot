#include "security_access_entropy.h"

#include "stm32u5xx_hal.h"

#include <string.h>

/* Keep ownership of the board RNG handle in this module. The security layer
 * consumes entropy through its narrow function contract and does not depend
 * on a global CubeMX-generated handle. */
static RNG_HandleTypeDef s_rng_handle;
static bool s_rng_ready;

bool SecurityAccess_EntropyInit(void)
{
    memset(&s_rng_handle, 0, sizeof(s_rng_handle));
    s_rng_handle.Instance = RNG;
    s_rng_handle.Init.ClockErrorDetection = RNG_CED_ENABLE;

    __HAL_RCC_RNG_CONFIG(RCC_RNGCLKSOURCE_HSI48);
    if (HAL_RNG_Init(&s_rng_handle) != HAL_OK)
    {
        s_rng_ready = false;
        return false;
    }

    s_rng_ready = true;
    return true;
}

bool SecurityAccess_GetEntropy(uint8_t *output, uint16_t length)
{
    uint16_t offset = 0U;

    if (!s_rng_ready || ((length > 0U) && (output == NULL)))
    {
        return false;
    }

    while (offset < length)
    {
        uint32_t random_word = 0U;
        uint16_t remaining = (uint16_t)(length - offset);
        uint16_t copy_length = remaining;

        if (copy_length > (uint16_t)sizeof(random_word))
        {
            copy_length = (uint16_t)sizeof(random_word);
        }

        if (HAL_RNG_GenerateRandomNumber(&s_rng_handle, &random_word) != HAL_OK)
        {
            memset(output, 0, length);
            s_rng_ready = false;
            return false;
        }

        memcpy(&output[offset], &random_word, copy_length);
        offset = (uint16_t)(offset + copy_length);
    }

    return true;
}
