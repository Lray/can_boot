#ifndef BOOT_OBSERVATION_H
#define BOOT_OBSERVATION_H

#include <stdbool.h>
#include <stdint.h>

#include "shared/mcuboot_image_version.h"

/**
 * Read the bootloader's persisted active-slot observation.
 *
 * An invalid or erased record is reported as SLOT_INVALID; callers must not
 * substitute a default slot.
 */
uint8_t BootObservation_GetActiveSlot(void);

/** Return the slot not selected by the bootloader, or SLOT_INVALID. */
uint8_t BootObservation_GetInactiveSlot(void);

/**
 * Read the observed active image's complete MCUboot version.
 *
 * Returns false when the active observation or MCUboot image header cannot be
 * read and validated.
 */
bool BootObservation_GetRunningImageVersion(
    mcuboot_image_version_t *version_out);

#endif /* BOOT_OBSERVATION_H */
