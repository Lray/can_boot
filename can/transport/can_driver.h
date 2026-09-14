/**
 * Interface between CAN hardware and CANopenNode.
 *
 * @file        can_driver.h
 * @ingroup     driver
 * @author      Janez Paternoster
 * @copyright   2004 - 2020 Janez Paternoster
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

#ifndef CAN_DRIVER_H
#define CAN_DRIVER_H

#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "main.h"

#if defined(FDCAN) || defined(FDCAN1) || defined(FDCAN2) || defined(FDCAN3)
#define STM32_FDCAN_Driver 1
#elif defined(CAN) || defined(CAN1) || defined(CAN2) || defined(CAN3)
#define STM32_CAN_Driver 1
#else
#error This STM32 does not support CAN or FDCAN
#endif

#define LITTLE_ENDIAN
#define SWAP_16(x) x
#define SWAP_32(x) x
#define SWAP_64(x) x

typedef uint_fast8_t bool_t;
typedef float float32_t;
typedef double float64_t;

typedef struct {
    uint32_t ident;
    uint8_t dlc;
    uint8_t data[8];
} can_rx_msg_t;

#define can_rx_msg_read_ident(msg) ((uint16_t)(((can_rx_msg_t*)(msg)))->ident)
#define can_rx_msg_read_dlc(msg)   ((uint8_t)(((can_rx_msg_t*)(msg)))->dlc)
#define can_rx_msg_read_data(msg)  ((uint8_t*)(((can_rx_msg_t*)(msg)))->data)

typedef struct {
    uint16_t ident;
    uint16_t mask;
    void* object;
    void (*CANrx_callback)(void* object, void* message);
} can_rx_t;

typedef struct {
    uint32_t ident;
    uint8_t DLC;
    uint8_t data[8];
    volatile bool_t bufferFull;
    volatile bool_t syncFlag;
} can_tx_t;

typedef struct {
    void* CANptr;
    can_rx_t* rxArray;
    uint16_t rxSize;
    can_tx_t* txArray;
    uint16_t txSize;
    uint16_t CANerrorStatus;
    volatile bool_t CANnormal;
    volatile bool_t useCANrxFilters;
    volatile bool_t bufferInhibitFlag;
    volatile bool_t firstCANtxMessage;
    volatile uint16_t CANtxCount;
    uint32_t errOld;
} can_module_t;

typedef struct {
    void* addr;
    size_t len;
    uint8_t subIndexOD;
    uint8_t attr;
    void* addrNV;
} can_storage_entry_t;

typedef struct {
#ifdef STM32_FDCAN_Driver
    FDCAN_HandleTypeDef* CANHandle;
#else
    CAN_HandleTypeDef* CANHandle;
#endif
    void (*HWInitFunction)(void);
} can_stm32_t;

#define MemoryBarrier()
#define FLAG_READ(rxNew) ((rxNew) != NULL)
#define FLAG_SET(rxNew)                                                                                               \
    do {                                                                                                               \
        MemoryBarrier();                                                                                               \
        rxNew = (void*)1L;                                                                                             \
    } while (0)
#define FLAG_CLEAR(rxNew)                                                                                             \
    do {                                                                                                               \
        MemoryBarrier();                                                                                               \
        rxNew = NULL;                                                                                                  \
    } while (0)

#ifndef LOCK_BASEPRI_ENABLE
#define LOCK_BASEPRI_ENABLE 0
#endif

#ifndef LOCK_BASEPRI_PRIO_LEVEL
#define LOCK_BASEPRI_PRIO_LEVEL 0
#endif

#ifndef LOCK_BASEPRI_NVIC_PRIO_BITS
#define LOCK_BASEPRI_NVIC_PRIO_BITS __NVIC_PRIO_BITS
#endif

#if LOCK_BASEPRI_ENABLE
#define LOCK_GENERIC(localvarname)                                                                                    \
    do {                                                                                                               \
        uint32_t localvarname = __get_BASEPRI();                                                                       \
        __set_BASEPRI_MAX(LOCK_BASEPRI_PRIO_LEVEL << (8UL - LOCK_BASEPRI_NVIC_PRIO_BITS));                            \
        __DSB();                                                                                                       \
        __ISB();
#define UNLOCK_GENERIC(localvarname)                                                                                  \
    __set_BASEPRI(localvarname);                                                                                       \
    __DSB();                                                                                                           \
    __ISB();                                                                                                           \
    }                                                                                                                  \
    while (0)
#else
#define LOCK_GENERIC(localvarname)                                                                                    \
    do {                                                                                                               \
        uint32_t localvarname = __get_PRIMASK();                                                                       \
        __disable_irq();
#define UNLOCK_GENERIC(localvarname)                                                                                  \
    __set_PRIMASK(localvarname);                                                                                       \
    }                                                                                                                  \
    while (0)
#endif

#define LOCK_CAN_SEND(CAN_MODULE)   LOCK_GENERIC(primask_send)
#define UNLOCK_CAN_SEND(CAN_MODULE) UNLOCK_GENERIC(primask_send)
#define LOCK_EMCY(CAN_MODULE)       LOCK_GENERIC(primask_emcy)
#define UNLOCK_EMCY(CAN_MODULE)     UNLOCK_GENERIC(primask_emcy)
#define LOCK_OD(CAN_MODULE)         LOCK_GENERIC(primask_od)
#define UNLOCK_OD(CAN_MODULE)       UNLOCK_GENERIC(primask_od)

#ifdef __cplusplus
extern "C" {
#endif

/* Stack configuration default global values. For more information see file config.h. */
#ifndef CONFIG_GLOBAL_FLAG_CALLBACK_PRE
#define CONFIG_GLOBAL_FLAG_CALLBACK_PRE (0)
#endif
#ifndef CONFIG_GLOBAL_RT_FLAG_CALLBACK_PRE
#define CONFIG_GLOBAL_RT_FLAG_CALLBACK_PRE (0)
#endif
#ifndef CONFIG_GLOBAL_FLAG_TIMERNEXT
#define CONFIG_GLOBAL_FLAG_TIMERNEXT (0)
#endif
#ifndef CONFIG_GLOBAL_FLAG_OD_DYNAMIC
#define CONFIG_GLOBAL_FLAG_OD_DYNAMIC CONFIG_FLAG_OD_DYNAMIC
#endif
#ifdef DEBUG_COMMON
#if (CONFIG_DEBUG) & CONFIG_DEBUG_SDO_CLIENT
#define DEBUG_SDO_CLIENT(msg) DEBUG_COMMON(msg)
#endif
#if (CONFIG_DEBUG) & CONFIG_DEBUG_SDO_SERVER
#define DEBUG_SDO_SERVER(msg) DEBUG_COMMON(msg)
#endif
#endif

/**
 * @defgroup driver Driver
 * Interface between CAN hardware and CANopenNode.
 *
 * @ingroup CANopen_301
 * @{
 * CANopenNode is designed for speed and portability. It runs efficiently on devices from simple 16-bit microcontrollers
 * to PC computers. It can run in multiple threads. Reception of CAN frames is pre-processed with very fast functions.
 * Time critical objects, such as PDO or SYNC are processed in real-time thread and other objects are processed in
 * normal thread. See Flowchart in [README.md](index.html) for more information.
 *
 * @anchor obj
 * #### CANopenNode Object
 * CANopenNode is implemented as a collection of different objects, for example SDO, SYNC, Emergency, PDO, NMT,
 * Heartbeat, etc. Code is written in C language and tries to be object oriented. So each CANopenNode Object is
 * implemented in a pair of .h/.c files. It basically contains a structure with all necessary variables and some
 * functions which operates on it. CANopenNode Object is usually connected with one or more CAN receive or transmit
 * frames.
 *
 * #### Terminology
 * - **CAN frame** is a single CAN frame, which is transmitted or received on the CAN bus. By default it consists of
 *   11-bit identifier, 0-8 bytes of data, and some control bits.
 * - **CANopen message** is an application-layer message in the CANopen protocol that defines the meaning of a CAN frame
 *   (e.g., PDO, SDO, NMT, EMCY) and represents structured communication between devices on top of the CAN bus.
 *
 * #### Hardware interface of CANopenNode
 * It consists of minimum three files:
 * - **driver.h** file declares common functions. This file is part of the CANopenNode. It is included from each .c
 *   file from CANopenNode.
 * - **driver_target.h** file declares microcontroller specific type declarations and defines some macros, which are
 *   necessary for CANopenNode. This file is included from driver.h.
 * - **driver.c** file defines functions declared in driver.h.
 *
 * **driver_target.h** and **driver.c** files are specific for each different microcontroller and are not part of
 * CANopenNode. There are separate projects for different microcontrollers, which usually include CANopenNode as a git
 * submodule. CANopenNode only includes those two files in the `example` directory and they are basically empty. It
 * should be possible to compile the `CANopenNode/example` on any system, however compiled program is not usable.
 * driver.h contains documentation for all necessary macros, types and functions.
 *
 * See [CANopenNode/Wiki](https://github.com/CANopenNode/CANopenNode/wiki) for a known list of available implementations
 * of CANopenNode on different systems and microcontrollers. Everybody is welcome to extend the list with a link to his
 * own implementation.
 *
 * Implementation of the hardware interface for specific microcontroller is not always an easy task. For reliable and
 * efficient operation it is necessary to know some parts of the target microcontroller in detail (for example threads
 * (or interrupts), CAN module, etc.).
 */

/** Major version number of CANopenNode */
#define VERSION_MAJOR 4
/** Minor version number of CANopenNode */
#define VERSION_MINOR 0

/* Macros and declarations in following part are only used for documentation. */
#ifdef DOXYGEN
/**
 * @defgroup dataTypes Basic definitions
 * @{
 *
 * Target specific basic definitions and data types.
 *
 * Must be defined in the **driver_target.h** file.
 *
 * Depending on processor or compiler architecture, one of the two macros must be defined: LITTLE_ENDIAN or
 * BIG_ENDIAN. CANopen itself is little endian.
 *
 * Basic data types may be specified differently on different architectures. Usually `true` and `false` are defined in
 * `<stdbool.h>`, `NULL` is defined in `<stddef.h>`, `int8_t` to `uint64_t` are defined in `<stdint.h>`.
 */
#define LITTLE_ENDIAN                 /**< LITTLE_ENDIAN or BIG_ENDIAN must be defined */
#define SWAP_16(x)    x               /**< Macro must swap bytes, if BIG_ENDIAN is defined */
#define SWAP_32(x)    x               /**< Macro must swap bytes, if BIG_ENDIAN is defined */
#define SWAP_64(x)    x               /**< Macro must swap bytes, if BIG_ENDIAN is defined */
#define NULL             (0)             /**< NULL, for general usage */
#define true             1               /**< Logical true, for general use */
#define false            0               /**< Logical false, for general use */
typedef uint_fast8_t bool_t;             /**< Boolean data type for general use */
typedef signed char int8_t;              /**< INTEGER8 in CANopen (0002h), 8-bit signed integer */
typedef signed int int16_t;              /**< INTEGER16 in CANopen (0003h), 16-bit signed integer */
typedef signed long int int32_t;         /**< INTEGER32 in CANopen (0004h), 32-bit signed integer */
typedef signed long long int int64_t;    /**< INTEGER64 in CANopen (0015h), 64-bit signed integer */
typedef unsigned char uint8_t;           /**< UNSIGNED8 in CANopen (0005h), 8-bit unsigned integer */
typedef unsigned int uint16_t;           /**< UNSIGNED16 in CANopen (0006h), 16-bit unsigned integer */
typedef unsigned long int uint32_t;      /**< UNSIGNED32 in CANopen (0007h), 32-bit unsigned integer */
typedef unsigned long long int uint64_t; /**< UNSIGNED64 in CANopen (001Bh), 64-bit unsigned integer */
typedef float float32_t;  /**< REAL32 in CANopen (0008h), single precision floating point value, 32-bit */
typedef double float64_t; /**< REAL64 in CANopen (0011h), double precision floating point value, 64-bit */
/** @} */

/**
 * @defgroup CAN_Frame_reception Reception of CAN frames
 * @{
 *
 * Target specific definitions and description of CAN frame reception
 *
 * CAN frames in CANopenNode are usually received by its own thread or higher priority interrupt. Received CAN
 * frames are first filtered by hardware or by software. Thread then examines its 11-bit CAN-id and mask and
 * determines, to which \ref obj "CANopenNode Object" it belongs to. After that it calls predefined CANrx_callback()
 * function, which quickly pre-processes the frame and fetches the relevant data. CANrx_callback() function is defined
 * by each \ref obj "CANopenNode Object" separately. Pre-processed fetched data are later processed in another
 * thread.
 *
 * If \ref obj "CANopenNode Object" reception of specific CAN frame, it must first configure its own can_rx_t
 * object with the can_rx_buffer_init() function.
 */

/**
 * CAN receive callback function which pre-processes received CAN frame
 *
 * It is called by fast CAN receive thread. Each \ref obj "CANopenNode Object" defines its own and registers it with
 * can_rx_buffer_init(), by passing function pointer.
 *
 * @param object pointer to specific \ref obj "CANopenNode Object", registered with can_rx_buffer_init()
 * @param rxMsg pointer to received CAN frame
 */
void CANrx_callback(void* object, void* rxMsg);

/**
 * CANrx_callback() can read CAN identifier from received CAN frame
 *
 * Must be defined in the **driver_target.h** file.
 *
 * This is target specific function and is specific for specific microcontroller. It is best to implement it by using
 * inline function or macro. `rxMsg` parameter should cast to a pointer to structure. For best efficiency structure may
 * have the same alignment as CAN registers inside CAN module.
 *
 * @param rxMsg Pointer to received frame
 * @return 11-bit CAN standard identifier.
 */
static inline uint16_t
can_rx_msg_read_ident(void* rxMsg) {
    return 0;
}

/**
 * CANrx_callback() can read Data Length Code from received CAN frame
 *
 * See also can_rx_msg_read_ident():
 *
 * @param rxMsg Pointer to received CAN frame
 * @return data length in bytes (0 to 8)
 */
static inline uint8_t
can_rx_msg_read_dlc(void* rxMsg) {
    return 0;
}

/**
 * CANrx_callback() can read pointer to data from received CAN frame
 *
 * See also can_rx_msg_read_ident():
 *
 * @param rxMsg Pointer to received frame
 * @return pointer to data buffer
 */
static inline const uint8_t*
can_rx_msg_read_data(void* rxMsg) {
    return NULL;
}

/**
 * Configuration object for received CAN frame for specific \ref obj "CANopenNode Object".
 *
 * Must be defined in the **driver_target.h** file.
 *
 * Data fields of this structure are used exclusively by the driver. Usually it has the following data fields, but they
 * may differ for different microcontrollers. Array of multiple can_rx_t objects is included inside can_module_t.
 */
typedef struct {
    uint16_t ident; /**< Standard CAN Identifier (bits 0..10) + RTR (bit 11) */
    uint16_t mask;  /**< Standard CAN Identifier mask with the same alignment as ident */
    void* object;   /**< \ref obj "CANopenNode Object" initialized in from can_rx_buffer_init() */
    void (*pCANrx_callback)(void* object,
                            void* message); /**< Pointer to CANrx_callback() initialized in can_rx_buffer_init() */
} can_rx_t;

/** @} */

/**
 * @defgroup CAN_Frame_transmission Transmission of CAN frames
 * @{
 *
 * Target specific definitions and description of CAN frame transmission
 *
 * If \ref obj "CANopenNode Object" needs transmitting CAN frame, it must first configure its own can_tx_t object
 * with the can_tx_buffer_init() function. CAN frame can then be sent with can_send() function. If at that moment
 * CAN transmit buffer inside microcontroller's CAN module is free, frame is copied directly to the CAN module.
 * Otherwise can_send() function sets _bufferFull_ flag to true. Frame will be then sent by CAN TX interrupt as soon
 * as CAN module is freed. Until frame is not copied to CAN module, its contents must not change. If there are
 * multiple can_tx_t objects with _bufferFull_ flag set to true, then can_tx_t with lower index will be sent first.
 */

/**
 * Configuration object for transmit CAN frame for specific \ref obj "CANopenNode Object".
 *
 * Must be defined in the **driver_target.h** file.
 *
 * Data fields of this structure are used exclusively by the driver. Usually it has the following data fields, but they
 * may differ for different microcontrollers. Array of multiple can_tx_t objects is included inside can_module_t.
 */
typedef struct {
    uint32_t ident;             /**< CAN identifier as aligned in CAN module */
    uint8_t DLC;                /**< Length of CAN frame */
    uint8_t data[8];            /**< 8 data bytes */
    volatile bool_t bufferFull; /**< True if previous frame is still in the buffer */
    volatile bool_t syncFlag;   /**< Synchronous PDO frames has this flag set. It prevents them to be sent outside the
                                   synchronous window */
} can_tx_t;

/** @} */

/**
 * Complete CAN module object.
 *
 * Must be defined in the **driver_target.h** file.
 *
 * Usually it has the following data fields, but they may differ for different microcontrollers.
 */
typedef struct {
    void* CANptr;                    /**< From can_module_init() */
    can_rx_t* rxArray;             /**< From can_module_init() */
    uint16_t rxSize;                 /**< From can_module_init() */
    can_tx_t* txArray;             /**< From can_module_init() */
    uint16_t txSize;                 /**< From can_module_init() */
    uint16_t CANerrorStatus;         /**< CAN error status bitfield, see @ref CAN_ERR_status_t */
    volatile bool_t CANnormal;       /**< CAN module is in normal mode */
    volatile bool_t useCANrxFilters; /**< Value different than zero indicates, that CAN module hardware filters are used
                                        for CAN reception. If there is not enough hardware filters, they won't be used.
                                        In this case will be *all* received CAN frames processed by software. */
    volatile bool_t bufferInhibitFlag; /**< If flag is true, then frame in transmit buffer is synchronous PDO message,
                                          which will be aborted, if clearPendingSyncPDOs() function will be called by
                                          application. This may be necessary if Synchronous window time was expired. */
    volatile bool_t
        firstCANtxMessage; /**< Equal to 1, when the first transmitted frame (bootup message) is in CAN TX buffers */
    volatile uint16_t
        CANtxCount;  /**< Number of frames in transmit buffer, which are waiting to be copied to the CAN module */
    uint32_t errOld; /**< Previous state of CAN errors */
} can_module_t;

/**
 * Data storage object for one entry.
 *
 * Must be defined in the **driver_target.h** file.
 *
 * For more information on Data storage see @ref storage or **storage.h** file. Structure members documented here
 * are always required or required with @ref storage_eeprom. Target system may add own additional, hardware specific
 * variables.
 */
typedef struct {
    void* addr;         /**< Address of data to store, always required. */
    size_t len;         /**< Length of data to store, always required. */
    uint8_t subIndexOD; /**< Sub index in OD objects 1010 and 1011, from 2 to 127. Writing 0x65766173 to 1010,subIndexOD
                           will store data to non-volatile memory Writing 0x64616F6C to 1011,subIndexOD will restore
                           default data, always required. */
    uint8_t attr;       /**< Attribute from @ref storage_attributes_t, always required. */
    void* storageModule; /**< Pointer to storage module, target system specific usage, required with @ref
                            storage_eeprom. */
    uint16_t crc; /**< CRC checksum of the data stored in eeprom, set on store, required with @ref storage_eeprom. */
    size_t eepromAddrSignature; /**< Address of entry signature inside eeprom, set by init, required with @ref
                                   storage_eeprom. */
    size_t eepromAddr; /**< Address of data inside eeprom, set by init, required with @ref storage_eeprom. */
    size_t offset; /**< Offset of next byte being updated by automatic storage, required with @ref storage_eeprom. */
    void* additionalParameters; /**< Additional target specific parameters, optional. */
} can_storage_entry_t;

/**
 * @defgroup critical_sections Critical sections
 * @{
 *
 * Protection of critical sections in multi-threaded operation.
 *
 * CANopenNode is designed to run in different threads, as described in [README.md](index.html). Threads are implemented
 * differently in different systems. In microcontrollers threads are interrupts with different priorities, for example.
 * It is necessary to protect sections, where different threads access to the same resource. In simple systems
 * interrupts or scheduler may be temporary disabled between access to the shared resource. Otherwise mutexes or
 * semaphores can be used.
 *
 * #### Reentrant functions
 * Functions can_send() from C_driver.h, and error() from Emergency.h may be called from different threads.
 * Critical sections must be protected. Either by disabling scheduler or interrupts or by mutexes or semaphores.
 * Lock/unlock macro is called with pointer to CAN module, which may be used inside.
 *
 * #### Object Dictionary variables
 * In general, there are two threads, which accesses OD variables: mainline (initialization, storage, SDO access) and
 * timer (PDO access). CANopenNode uses locking mechanism, where SDO server (or other mainline code) prevents execution
 * of the real-time thread at the moment it reads or writes OD variable. LOCK_OD(CAN_MODULE) and
 * UNLOCK_OD(CAN_MODULE) macros are used to protect:
 * - Whole real-time thread,
 * - SDO server protects read/write access to OD variable.   Locking of long OD variables, not accessible from real-time
 *   thread, may   block RT thread.
 * - Any mainline code, which accesses PDO-mappable OD variable, must protect   read/write with locking macros. Use @ref
 *   OD_mappable() for check.
 * - Other cases, where non-PDO-mappable OD variable is used inside real-time   thread by some other part of the user
 *   application must be considered with   special care. Also when there are multiple threads accessing the OD
 *   (e.g. when using a RTOS), you should always lock the OD.
 *
 * #### Synchronization functions for CAN receive
 * After CAN frame is received, it is pre-processed in CANrx_callback(), which copies some data into appropriate
 * object and at the end sets **new_frame** flag. This flag is then pooled in another thread, which further processes
 * the data. The problem is, that compiler optimization may shuffle memory operations, so it is necessary to ensure,
 * that **new_frame** flag is surely set at the end. It is necessary to use [Memory
 * barrier](https://en.wikipedia.org/wiki/Memory_barrier).
 *
 * If receive function runs inside IRQ, no further synchronization is needed. Otherwise, some kind of synchronization
 * has to be included. The following example uses GCC builtin memory barrier `__sync_synchronize()`. More information
 * can be found [here](https://stackoverflow.com/questions/982129/what-does-sync-synchronize-do#982179).
 */

#define LOCK_CAN_SEND(CAN_MODULE)   /**< Lock critical section in can_send() */
#define UNLOCK_CAN_SEND(CAN_MODULE) /**< Unlock critical section in can_send() */
#define LOCK_EMCY(CAN_MODULE)       /**< Lock critical section in errorReport() or errorReset() */
#define UNLOCK_EMCY(CAN_MODULE)     /**< Unlock critical section in errorReport() or errorReset() */
#define LOCK_OD(CAN_MODULE)         /**< Lock critical section when accessing Object Dictionary */
#define UNLOCK_OD(CAN_MODULE)       /**< Unock critical section when accessing Object Dictionary */

/** Check if new CAN frame has arrived */
#define FLAG_READ(rxNew)            ((rxNew) != NULL)
/** Set new CAN frame flag */
#define FLAG_SET(rxNew)                                                                                             \
    {                                                                                                                  \
        __sync_synchronize();                                                                                          \
        rxNew = (void*)1L;                                                                                             \
    }
/** Clear new CAN frame flag */
#define FLAG_CLEAR(rxNew)                                                                                           \
    {                                                                                                                  \
        __sync_synchronize();                                                                                          \
        rxNew = NULL;                                                                                                  \
    }

/** @} */
#endif /* DOXYGEN */

/**
 * @defgroup Default_CAN_ID_t Default CANopen identifiers
 * @{
 *
 * Default CANopen identifiers for CANopen communication objects. Same as 11-bit addresses of CAN frames. These are
 * default identifiers and can be changed in CANopen. Especially PDO identifiers are configured in PDO linking phase of
 * the CANopen network configuration.
 */
#define CAN_ID_NMT_SERVICE 0x000U /**< 0x000 Network management */
#define CAN_ID_GFC         0x001U /**< 0x001 Global fail-safe command */
#define CAN_ID_SYNC        0x080U /**< 0x080 Synchronous message */
#define CAN_ID_EMERGENCY   0x080U /**< 0x080 Emergency messages (+nodeID) */
#define CAN_ID_TIME        0x100U /**< 0x100 Time message */
#define CAN_ID_SRDO_1      0x0FFU /**< 0x0FF Default SRDO1 (+2*nodeID) */
#define CAN_ID_TPDO_1      0x180U /**< 0x180 Default TPDO1 (+nodeID) */
#define CAN_ID_RPDO_1      0x200U /**< 0x200 Default RPDO1 (+nodeID) */
#define CAN_ID_TPDO_2      0x280U /**< 0x280 Default TPDO2 (+nodeID) */
#define CAN_ID_RPDO_2      0x300U /**< 0x300 Default RPDO2 (+nodeID) */
#define CAN_ID_TPDO_3      0x380U /**< 0x380 Default TPDO3 (+nodeID) */
#define CAN_ID_RPDO_3      0x400U /**< 0x400 Default RPDO3 (+nodeID) */
#define CAN_ID_TPDO_4      0x480U /**< 0x480 Default TPDO4 (+nodeID) */
#define CAN_ID_RPDO_4      0x500U /**< 0x500 Default RPDO4 (+nodeID) */
#define CAN_ID_SDO_SRV     0x580U /**< 0x580 SDO response from server (+nodeID) */
#define CAN_ID_SDO_CLI     0x600U /**< 0x600 SDO request from client (+nodeID) */
#define CAN_ID_HEARTBEAT   0x700U /**< 0x700 Heartbeat message */
#define CAN_ID_LSS_SLV     0x7E4U /**< 0x7E4 LSS response from slave */
#define CAN_ID_LSS_MST     0x7E5U /**< 0x7E5 LSS request from master */

/** @} */ /* Default_CAN_ID_t */

/**
 * Restricted CAN-IDs
 *
 * Macro for verifying 'Restricted CAN-IDs', as specified by standard CiA301. They shall not be used for SYNC, TIME,
 * EMCY, PDO and SDO.
 */
#ifndef IS_RESTRICTED_CAN_ID
#define IS_RESTRICTED_CAN_ID(CAN_ID)                                                                                \
    (((CAN_ID) <= 0x7FU) || (((CAN_ID) >= 0x101U) && ((CAN_ID) <= 0x180U))                                             \
     || (((CAN_ID) >= 0x581U) && ((CAN_ID) <= 0x5FFU)) || (((CAN_ID) >= 0x601U) && ((CAN_ID) <= 0x67FU))               \
     || (((CAN_ID) >= 0x6E0U) && ((CAN_ID) <= 0x6FFU)) || ((CAN_ID) >= 0x701U))
#endif

/**
 * @defgroup CAN_ERR_status_t CAN error status bitmasks
 * @{
 *
 * CAN warning level is reached, if CAN transmit or receive error counter is more or equal to 96. CAN passive level is
 * reached, if counters are more or equal to 128. Transmitter goes in error state 'bus off' if transmit error counter is
 * more or equal to 256.
 */
#define CAN_ERRTX_WARNING    0x0001U /**< 0x0001 CAN transmitter warning */
#define CAN_ERRTX_PASSIVE    0x0002U /**< 0x0002 CAN transmitter passive */
#define CAN_ERRTX_BUS_OFF    0x0004U /**< 0x0004 CAN transmitter bus off */
#define CAN_ERRTX_OVERFLOW   0x0008U /**< 0x0008 CAN transmitter overflow */
#define CAN_ERRTX_PDO_LATE   0x0080U /**< 0x0080 TPDO is outside sync window */
#define CAN_ERRRX_WARNING    0x0100U /**< 0x0100 CAN receiver warning */
#define CAN_ERRRX_PASSIVE    0x0200U /**< 0x0200 CAN receiver passive */
#define CAN_ERRRX_OVERFLOW   0x0800U /**< 0x0800 CAN receiver overflow */
#define CAN_ERR_WARN_PASSIVE 0x0303U /**< 0x0303 combination */

/** @} */ /* CAN_ERR_status_t */

/**
 * Return values of some CANopen functions. If function was executed successfully it returns 0 otherwise it returns <0.
 */
typedef enum {
    ERROR_NO = 0,                /**< Operation completed successfully */
    ERROR_ILLEGAL_ARGUMENT = -1, /**< Error in function arguments */
    ERROR_OUT_OF_MEMORY = -2,    /**< Memory allocation failed */
    ERROR_TIMEOUT = -3,          /**< Function timeout */
    ERROR_ILLEGAL_BAUDRATE = -4, /**< Illegal baudrate passed to function can_module_init() */
    ERROR_RX_OVERFLOW = -5,      /**< Previous CAN frame was not processed yet */
    ERROR_RX_PDO_OVERFLOW = -6,  /**< previous PDO was not processed yet */
    ERROR_RX_MSG_LENGTH = -7,    /**< Wrong receive CAN frame length */
    ERROR_RX_PDO_LENGTH = -8,    /**< Wrong receive PDO length */
    ERROR_TX_OVERFLOW = -9,      /**< Previous CAN frame is still waiting, buffer full */
    ERROR_TX_PDO_WINDOW = -10,   /**< Synchronous TPDO is outside window */
    ERROR_TX_UNCONFIGURED = -11, /**< Transmit buffer was not configured properly */
    ERROR_OD_PARAMETERS = -12,   /**< Error in Object Dictionary parameters */
    ERROR_DATA_CORRUPT = -13,    /**< Stored data are corrupt */
    ERROR_CRC = -14,             /**< CRC does not match */
    ERROR_TX_BUSY = -15,         /**< Sending rejected because driver is busy. Try again */
    ERROR_WRONG_NMT_STATE = -16, /**< Command can't be processed in current state */
    ERROR_SYSCALL = -17,         /**< Syscall failed */
    ERROR_INVALID_STATE = -18,   /**< Driver not ready */
    ERROR_NODE_ID_UNCONFIGURED_LSS =
        -19 /**< Node-id is in LSS unconfigured state. If objects are handled properly, this may not be an error. */
} can_return_error_t;

/**
 * Request CAN configuration (stopped) mode and *wait* until it is set.
 *
 * @param CANptr Pointer to CAN device
 */
void can_set_configuration_mode(void* CANptr);

/**
 * Request CAN normal (operational) mode and *wait* until it is set.
 *
 * @param CANmodule can_module_t object.
 */
void can_set_normal_mode(can_module_t* CANmodule);

/**
 * Initialize CAN module object.
 *
 * Function must be called in the communication reset section. CAN module must be in Configuration Mode before.
 *
 * @param CANmodule This object will be initialized.
 * @param CANptr Pointer to CAN device.
 * @param rxArray Array for handling received CAN frames
 * @param rxSize Size of the above array. Must be equal to number of receiving CAN objects.
 * @param txArray Array for handling transmitting CAN frames
 * @param txSize Size of the above array. Must be equal to number of transmitting CAN objects.
 * @param CANbitRate Valid values are (in kbps): 10, 20, 50, 125, 250, 500, 800, 1000. If value is illegal, bitrate
 * defaults to 125.
 *
 * Return #can_return_error_t: ERROR_NO or ERROR_ILLEGAL_ARGUMENT.
 */
can_return_error_t can_module_init(can_module_t* CANmodule, void* CANptr, can_rx_t rxArray[], uint16_t rxSize,
                                   can_tx_t txArray[], uint16_t txSize, uint16_t CANbitRate);

/**
 * Switch off CANmodule. Call at program exit.
 *
 * @param CANmodule CAN module object.
 */
void can_module_disable(can_module_t* CANmodule);

/**
 * Configure CAN frame receive buffer.
 *
 * Function configures specific CAN receive buffer. It sets CAN identifier and connects buffer with specific object.
 * Function must be called for each member in _rxArray_ from can_module_t.
 *
 * @param CANmodule This object.
 * @param index Index of the specific buffer in _rxArray_.
 * @param ident 11-bit standard CAN Identifier. If two or more CANrx buffers have the same _ident_, then buffer with
 * lowest _index_ has precedence and other CANrx buffers will be ignored.
 * @param mask 11-bit mask for identifier. Most usually set to 0x7FF. Received CAN frame (rcvMsg) will be accepted
 * if the following condition is true: (((rcvMsgId ^ ident) & mask) == 0).
 * @param rtr If true, 'Remote Transmit Request' CAN frames will be accepted.
 * @param object CANopen object, to which buffer is connected. It will be used as an argument to CANrx_callback. Its
 * type is (void), CANrx_callback will change its type back to the correct object type.
 * @param CANrx_callback Pointer to function, which will be called, if received CAN frame matches the identifier. It
 * must be fast function.
 *
 * Return #can_return_error_t: ERROR_NO ERROR_ILLEGAL_ARGUMENT or ERROR_OUT_OF_MEMORY (not enough masks for
 * configuration).
 */
can_return_error_t can_rx_buffer_init(can_module_t* CANmodule, uint16_t index, uint16_t ident, uint16_t mask,
                                    bool_t rtr, void* object, void (*CANrx_callback)(void* object, void* message));

/**
 * Configure CAN frame transmit buffer.
 *
 * Function configures specific CAN transmit buffer. Function must be called for each member in _txArray_ from
 * can_module_t.
 *
 * @param CANmodule This object.
 * @param index Index of the specific buffer in _txArray_.
 * @param ident 11-bit standard CAN Identifier.
 * @param rtr If true, 'Remote Transmit Request' CAN frames will be transmitted.
 * @param noOfBytes Length of CAN frame in bytes (0 to 8 bytes).
 * @param syncFlag This flag bit is used for synchronous TPDO messages. If it is set, CAN frame will not be sent, if
 * current time is outside synchronous window.
 *
 * @return Pointer to transmit CAN frame buffer. 8 bytes data array inside buffer should be written, before
 * can_send() function is called. Zero is returned in case of wrong arguments.
 */
can_tx_t* can_tx_buffer_init(can_module_t* CANmodule, uint16_t index, uint16_t ident, bool_t rtr, uint8_t noOfBytes,
                               bool_t syncFlag);

/**
 * Send CAN frame.
 *
 * @param CANmodule This object.
 * @param buffer Pointer to transmit buffer, returned by can_tx_buffer_init(). Data bytes must be written in buffer
 * before function call.
 *
 * @return #can_return_error_t: ERROR_NO, ERROR_TX_OVERFLOW or ERROR_TX_PDO_WINDOW (Synchronous TPDO is outside
 * window).
 */
can_return_error_t can_send(can_module_t* CANmodule, can_tx_t* buffer);

/**
 * Clear all synchronous TPDOs from CAN module transmit buffers.
 *
 * CANopen allows synchronous PDO communication only inside time between SYNC message and SYNC Window. If time is
 * outside this window, new synchronous PDOs must not be sent and all pending sync TPDOs, which may be on CAN TX
 * buffers, may optionally be cleared.
 *
 * This function checks (and aborts transmission if necessary) CAN TX buffers when it is called. Function should be
 * called by the stack in the moment, when SYNC time was just passed out of synchronous window.
 *
 * @param CANmodule This object.
 */
void can_clear_pending_sync_pdos(can_module_t* CANmodule);

/**
 * Process can module - verify CAN errors
 *
 * Function must be called cyclically. It should calculate CANerrorStatus bitfield for CAN errors defined in @ref
 * CAN_ERR_status_t.
 *
 * @param CANmodule This object.
 */
void can_module_process(can_module_t* CANmodule);

/**
 * Get uint8_t value from memory buffer
 *
 * @param buf Memory buffer to get value from.
 *
 * @return Value
 */
static inline uint8_t
can_get_uint8(const void* buf) {
    uint8_t value;
    (void)memmove((void*)&value, buf, sizeof(value));
    return value;
}

/** Get uint16_t value from memory buffer, see @ref can_get_uint8 */
static inline uint16_t
can_get_uint16(const void* buf) {
    uint16_t value;
    (void)memmove((void*)&value, buf, sizeof(value));
    return value;
}

/** Get uint32_t value from memory buffer, see @ref can_get_uint8 */
static inline uint32_t
can_get_uint32(const void* buf) {
    uint32_t value;
    (void)memmove((void*)&value, buf, sizeof(value));
    return value;
}

/**
 * Write uint8_t value into memory buffer
 *
 * @param buf Memory buffer.
 * @param value Value to be written into buf.
 *
 * @return number of bytes written.
 */
static inline uint8_t
can_set_uint8(void* buf, uint8_t value) {
    (void)memmove(buf, (const void*)&value, sizeof(value));
    return (uint8_t)(sizeof(value));
}

/** Write uint16_t value into memory buffer, see @ref can_set_uint8 */
static inline uint8_t
can_set_uint16(void* buf, uint16_t value) {
    (void)memmove(buf, (const void*)&value, sizeof(value));
    return (uint8_t)(sizeof(value));
}

/** Write uint32_t value into memory buffer, see @ref can_set_uint8 */
static inline uint8_t
can_set_uint32(void* buf, uint32_t value) {
    (void)memmove(buf, (const void*)&value, sizeof(value));
    return (uint8_t)(sizeof(value));
}

/** @} */ /* driver */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* CAN_DRIVER_H */
