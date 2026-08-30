# isotp-c import

This directory contains the unmodified runtime component from the maintained
`SimonCahill/isotp-c` project used by `driftregion/iso14229`.

- Upstream: https://github.com/SimonCahill/isotp-c
- Release: `v1.8.0`
- Source commit: `abb9e552df0e7ca0148c146124795341d57124fe`
- License: MIT
- Imported runtime files: `isotp.c`, `isotp.h`, `isotp_config.h`,
  `isotp_defines.h`, `isotp_user.h`, and upstream `CMakeLists.txt`

Project-specific CAN transmission, time, and debug hooks remain outside this
directory in `transport/isotp_stm32.c`. Compile-time product settings are
applied by `cmake/thirdparty.cmake`; upstream runtime sources are not patched.
