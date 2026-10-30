/*
 * CANopen Object Dictionary storage object (blank example).
 *
 * @file        CO_storageBlank.c
 * @author      Janez Paternoster
 * @copyright   2021 Janez Paternoster
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "CO_storageBlank.h"

#include <string.h>

#include "flash_map.h"
#include "sysflash.h"

CO_ReturnError_t CO_storageBlank_init(uint32_t* storage,
                                      uint32_t* storageInitError)
{
    const struct flash_area* area = NULL;

    if ((storage == NULL) || (storageInitError == NULL))
    {
        return CO_ERROR_ILLEGAL_ARGUMENT;
    }

    *storage = UINT32_MAX;
    *storageInitError = 0U;
    if ((flash_area_open(FLASH_AREA_COMMUNICATION_CONFIG, &area) != 0) ||
        (flash_area_read(area, 0U, storage, sizeof(*storage)) != 0))
    {
        *storageInitError = 1U;
        return CO_ERROR_DATA_CORRUPT;
    }

    return CO_ERROR_NO;
}

uint32_t CO_storageBlank_auto_process(uint32_t* storage,
                                      bool_t closeFiles)
{
    const struct flash_area* area = NULL;
    uint32_t current = UINT32_MAX;
    uint32_t verify = UINT32_MAX;
    uint32_t program_unit[4];

    (void)closeFiles;
    if ((storage == NULL) ||
        (flash_area_open(FLASH_AREA_COMMUNICATION_CONFIG, &area) != 0))
    {
        return 1U;
    }

    if ((flash_area_read(area, 0U, &current, sizeof(current)) == 0) &&
        (current == *storage))
    {
        return 0U;
    }

    (void)memset(program_unit, 0xFF, sizeof(program_unit));
    program_unit[0] = *storage;
    return ((flash_area_erase(area, 0U,
                              FLASH_AREA_COMMUNICATION_CONFIG_SIZE) == 0) &&
            (flash_area_write(area, 0U,
                              program_unit, sizeof(program_unit)) == 0) &&
            (flash_area_read(area, 0U, &verify, sizeof(verify)) == 0) &&
            (verify == *storage)) ? 0U : 1U;
}
