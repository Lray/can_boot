#ifndef SHARED_SECURITY_ACCESS_LEVEL_H
#define SHARED_SECURITY_ACCESS_LEVEL_H

/*
 * Shared UDS SecurityAccess subfunction-level contract between the ECU
 * firmware (can/) and the Linux gateway (gateway/).  Single source of truth;
 * both trees include it and must not redefine these macros locally.
 *
 * Naming follows the ECU tree (UPPER_SNAKE with U suffix).
 */

#define SECURITY_ACCESS_LEVEL_PROGRAMMING_SEED 0x01U
#define SECURITY_ACCESS_LEVEL_PROGRAMMING_KEY 0x02U

#endif /* SHARED_SECURITY_ACCESS_LEVEL_H */
