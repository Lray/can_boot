# RT-Thread ULog import

This directory is a curated component import, not a second RT-Thread source
tree. It matches the RT-Thread Nano package selected in STM32CubeMX.

- Upstream: `RT-Thread/rt-thread` tag `v4.1.1`
- Source commit: `aab2428d4177a02cd3b0fd020e47a88de379a6ab`
- Imported ULog core: `components/utilities/ulog/{ulog.c,ulog.h,ulog_def.h}`
- Required native IPC support: `components/drivers/{include/ipc/ringbuffer.h,include/ipc/ringblk_buf.h,ipc/ringbuffer.c,ipc/ringblk_buf.c}`

The CMake source list intentionally excludes upstream `backend/console_be.c`
and `backend/file_be.c`. Consequently this migration creates ULog and its
asynchronous worker thread but performs no UART, CAN, filesystem, or other
output I/O. A later backend must register through ULog's native backend API.

The three imported source files include these two IPC headers directly instead
of upstream's aggregate `rtdevice.h`. That is the only source adaptation: it
avoids importing/enabling RT-Thread's unrelated device framework merely to use
its ring buffers.
