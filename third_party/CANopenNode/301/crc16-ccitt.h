/**
 * Calculation of CRC 16 CCITT polynomial.
 *
 * @file        crc16-ccitt.h
 * @ingroup     CO_crc16_ccitt
 * @author      Lammert Bies
 * @author      Janez Paternoster
 * @copyright   2012 - 2020 Janez Paternoster
 *
 * This file is part of <https://github.com/CANopenNode/CANopenNode>, a CANopen Stack.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this
 * file except in compliance with the License. You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under the License is
 * distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and limitations under the License.
 */

#ifndef CRC16_CCITT_H
#define CRC16_CCITT_H

#include "301/CO_driver.h"

#ifndef CO_CONFIG_CRC16
#define CO_CONFIG_CRC16 (0)
#endif

#if (((CO_CONFIG_CRC16)&CO_CONFIG_CRC16_ENABLE) != 0) || defined CO_DOXYGEN

#ifdef __cplusplus
extern "C" {
#endif

void crc16_ccitt_single(uint16_t* crc, const uint8_t chr);
uint16_t crc16_ccitt(const uint8_t block[], size_t blockLength, uint16_t crc);

#ifdef __cplusplus
}
#endif

#endif
#endif
