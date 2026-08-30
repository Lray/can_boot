#ifndef IMAGE_CONFIRM_H
#define IMAGE_CONFIRM_H

#include <stdbool.h>

#include "shared/image_confirm_result.h"

/**
 * Write the confirmation marker for the currently active image.
 *
 * The startup self-check calls this after the application has started
 * successfully. An already written marker is treated as success.
 *
 * @return true when the marker is already present or was written successfully.
 */
bool ImageConfirm_WriteConfirm(void);

/**
 * Confirm the running image only after the application startup self-check.
 *
 * @param self_check_passed Result of the board/application health check.
 * @return The observed self-check and MCUboot confirmation outcome.
 */
image_confirm_result_t ImageConfirm_RunStartupSelfCheck(
    bool self_check_passed);

/** Return the current boot's startup confirmation outcome. */
image_confirm_result_t ImageConfirm_GetLastStartupResult(void);

#endif /* IMAGE_CONFIRM_H */
