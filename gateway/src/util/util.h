#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <stdint.h>

/** Zero a memory region without the compiler optimizing the write away. */
void secure_zero(void *data, size_t length);

/** Return nonzero when text has exactly length lowercase hex characters. */
int is_lower_hex(const char *text, size_t length);

/** Return nonzero when text is a version-4 UUID in canonical lowercase form. */
int is_uuid_v4(const char *text);

/** Return nonzero when ifname is a usable network interface name. */
int valid_ifname(const char *ifname);

/**
 * Decode a lowercase hex string into output_len bytes.  Returns 0 on success
 * or -1 when the text length, character set, or a digit parse fails.
 */
int util_parse_hex(const char *text, uint8_t *output, size_t output_len);

/**
 * Parse a positive decimal size with no leading zero.  Returns 0 on success
 * or -1 on overflow or format violations.  The caller applies any additional
 * domain limit.
 */
int util_parse_size(const char *text, uint64_t *value_out);

/**
 * Parse a positive decimal identity value in the range 1..65535.  Returns 0
 * on success or -1 on overflow or format violations.
 */
int util_parse_identity(const char *text, unsigned long *value_out);

/**
 * Return the monotonic clock in milliseconds, or zero on clock failure.
 */
uint64_t util_monotonic_ms(void);

/**
 * Sleep for delay_ms, retrying on EINTR.  Returns 0 on success or -1 on an
 * unhandled nanosleep error.
 */
int util_sleep_ms(uint32_t delay_ms);

#endif
