#ifndef SECURITY_ACCESS_ENTROPY_H
#define SECURITY_ACCESS_ENTROPY_H

#include <stdbool.h>

/**
 * Initialize the board-owned hardware entropy source.
 *
 * SecurityAccess_GetEntropy() remains unavailable until this succeeds. A
 * caller that cannot continue without security entropy must fail closed.
 */
bool SecurityAccess_EntropyInit(void);

#endif /* SECURITY_ACCESS_ENTROPY_H */
