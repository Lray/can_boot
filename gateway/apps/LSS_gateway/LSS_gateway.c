/*
 * Mainline organization and Unix socket integration adapted from CANopenLinux
 * CO_main_basic.c and CO_epoll_interface.c, f1348d4072cdabea4c3435a13c721ac29ab4cc91.
 * Copyright 2020 Janez Paternoster; 2018-2020 Neuberger Gebaeudeautomation GmbH.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "309/CO_gateway_ascii.h"
#include "LSS_gateway.h"

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/un.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#define MAIN_THREAD_INTERVAL_US 100000U
#define CANSEND_DELAY_US 100U

static volatile sig_atomic_t end_program;

/* Application logging callback required by the upstream SocketCAN driver. */
void log_printf(int priority, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    vsyslog(priority, format, arguments);
    va_end(arguments);
}

static void signal_handler(int signal_number) {
    (void)signal_number;
    end_program = 1;
}

static uint64_t clock_gettime_us(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return (uint64_t)now.tv_sec * UINT64_C(1000000) + (uint64_t)now.tv_nsec / 1000U;
}

/* Unsent bytes remain in the official GTWA response buffer. */
static size_t write_response(void *object, const char *buffer, size_t count, uint8_t *connection_ok) {
    int *fd = object;
    if (*fd < 0) {
        *connection_ok = 0;
        return count;
    }
    ssize_t written = write(*fd, buffer, count);
    return written < 0 ? 0 : (size_t)written;
}

static int close_client(int epoll_fd, int listener, int *client, CO_GTWA_t *gtwa) {
    if (*client < 0) {
        return 0;
    }
    (void)epoll_ctl(epoll_fd, EPOLL_CTL_DEL, *client, NULL);
    (void)close(*client);
    *client = -1;
    CO_LSSmaster_t *master = gtwa->LSSmaster;
    (void)CO_LSSmaster_swStateDeselect(master);
    CO_LSSmaster_changeTimeout(master, CO_LSSmaster_DEFAULT_TIMEOUT);
    if (CO_GTWA_init(gtwa, master, 0) != CO_ERROR_NO) {
        return -1;
    }
    CO_GTWA_initRead(gtwa, write_response, client);
    struct epoll_event event = {.events = EPOLLIN | EPOLLONESHOT, .data.fd = listener};
    return epoll_ctl(epoll_fd, EPOLL_CTL_MOD, listener, &event);
}

int main(void) {
    CO_CANmodule_t CANmodule = {0};
    CO_CANrx_t rx[1];
    CO_CANtx_t tx[1];
    CO_LSSmaster_t master;
    CO_GTWA_t gtwa;
    CO_CANptrSocketCan_t CANptr = {.can_ifindex = (int)if_nametoindex("awlink0"), .epoll_fd = -1};
    int timer_fd = -1;
    int listener = -1;
    int client = -1;
    int program_exit = EXIT_FAILURE;
    bool socket_bound = false;
    struct itimerspec timer = {.it_interval = {.tv_nsec = MAIN_THREAD_INTERVAL_US * 1000U},
                              .it_value = {.tv_nsec = 1}};
    struct epoll_event event;

    (void)umask(0077);
    if (CANptr.can_ifindex == 0 || signal(SIGPIPE, SIG_IGN) == SIG_ERR ||
        signal(SIGINT, signal_handler) == SIG_ERR || signal(SIGTERM, signal_handler) == SIG_ERR) {
        goto out;
    }

    /* Create the mainline event loop and command socket before CAN initialization. */
    CANptr.epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (CANptr.epoll_fd < 0 || timer_fd < 0 || timerfd_settime(timer_fd, 0, &timer, NULL) != 0) {
        goto out;
    }
    event.events = EPOLLIN;
    event.data.fd = timer_fd;
    if (epoll_ctl(CANptr.epoll_fd, EPOLL_CTL_ADD, timer_fd, &event) != 0) {
        goto out;
    }
    listener = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    struct sockaddr_un address = {.sun_family = AF_UNIX, .sun_path = LSS_GATEWAY_SOCKET};
    if (listener < 0 || bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0) {
        goto out;
    }
    socket_bound = true;
    if (listen(listener, 50) != 0) {
        goto out;
    }
    event.events = EPOLLIN | EPOLLONESHOT;
    event.data.fd = listener;
    if (epoll_ctl(CANptr.epoll_fd, EPOLL_CTL_ADD, listener, &event) != 0) {
        goto out;
    }

    /* Initialize only the CAN driver, LSS master, and official ASCII engine. */
    CO_CANsetConfigurationMode(&CANptr);
    if (CO_CANmodule_init(&CANmodule, &CANptr, rx, 1, tx, 1, 0) != CO_ERROR_NO ||
        CO_LSSmaster_init(&master, CO_LSSmaster_DEFAULT_TIMEOUT, &CANmodule, 0, CO_CAN_ID_LSS_SLV,
                          &CANmodule, 0, CO_CAN_ID_LSS_MST) != CO_ERROR_NO ||
        CO_GTWA_init(&gtwa, &master, 0) != CO_ERROR_NO) {
        goto out;
    }
    CO_GTWA_initRead(&gtwa, write_response, &client);
    CO_CANsetNormalMode(&CANmodule);
    uint64_t previous_us = clock_gettime_us();
    if (!CANmodule.CANnormal || previous_us == 0) {
        goto out;
    }
    fprintf(stderr, "LSS_gateway: READY interface=awlink0 socket=%s\n", LSS_GATEWAY_SOCKET);

    while (!end_program) {
        /* Wait, dispatch CAN RX and socket input, then process the official engine. */
        int ready = epoll_wait(CANptr.epoll_fd, &event, 1, -1);
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        uint64_t now_us = clock_gettime_us();
        if (ready != 1 || now_us == 0) {
            goto out;
        }
        uint32_t difference_us = (uint32_t)(now_us - previous_us);
        uint32_t timer_next_us = MAIN_THREAD_INTERVAL_US;
        previous_us = now_us;

        if (event.data.fd == timer_fd) {
            uint64_t ticks;
            if (read(timer_fd, &ticks, sizeof(ticks)) != (ssize_t)sizeof(ticks) && errno != EAGAIN) {
                goto out;
            }
        } else if (!CO_CANrxFromEpoll(&CANmodule, &event, NULL, NULL)) {
            if (event.data.fd == listener) {
                client = accept4(listener, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
                if (client < 0) {
                    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                        goto out;
                    }
                    event.events = EPOLLIN | EPOLLONESHOT;
                    if (epoll_ctl(CANptr.epoll_fd, EPOLL_CTL_MOD, listener, &event) != 0) {
                        goto out;
                    }
                } else {
                    event.events = EPOLLIN;
                    event.data.fd = client;
                    if (epoll_ctl(CANptr.epoll_fd, EPOLL_CTL_ADD, client, &event) != 0) {
                        goto out;
                    }
                }
            } else if (event.data.fd == client) {
                if (event.events & (EPOLLERR | EPOLLHUP)) {
                    if (close_client(CANptr.epoll_fd, listener, &client, &gtwa) != 0) {
                        goto out;
                    }
                } else if (event.events & EPOLLIN) {
                    char buffer[CO_CONFIG_GTWA_COMM_BUF_SIZE];
                    size_t space = CO_GTWA_write_getSpace(&gtwa);
                    if (space != 0) {
                        ssize_t received = read(client, buffer, space);
                        if (received == 0 || (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                            if (close_client(CANptr.epoll_fd, listener, &client, &gtwa) != 0) {
                                goto out;
                            }
                        } else if (received > 0) {
                            CO_GTWA_write(&gtwa, buffer, (size_t)received);
                        }
                    }
                }
            }
        }
        CO_CANmodule_process(&CANmodule);
        //bool_t enable传入client？
        CO_GTWA_process(&gtwa, client >= 0, difference_us, &timer_next_us);

        /* Follow the official timerNext_us scheduling and CAN TX retry interval. */
        if (CANmodule.CANtxCount != 0 && timer_next_us > CANSEND_DELAY_US) {
            timer_next_us = CANSEND_DELAY_US;
        }
        if (timer_next_us < MAIN_THREAD_INTERVAL_US) {
            timer.it_value.tv_nsec = (timer_next_us + 1U) * 1000U;
            if (timerfd_settime(timer_fd, 0, &timer, NULL) != 0) {
                goto out;
            }
        }
    }
    program_exit = EXIT_SUCCESS;

out:
    if (program_exit != EXIT_SUCCESS) {
        fprintf(stderr, "LSS_gateway: startup or event-loop failure\n");
    }
    if (close_client(CANptr.epoll_fd, listener, &client, &gtwa) != 0) {
        program_exit = EXIT_FAILURE;
    }
    CO_CANsetConfigurationMode(&CANptr);
    CO_CANmodule_disable(&CANmodule);
    if (listener >= 0) {
        (void)close(listener);
    }
    if (socket_bound) {
        (void)unlink(LSS_GATEWAY_SOCKET);
    }
    if (timer_fd >= 0) {
        (void)close(timer_fd);
    }
    if (CANptr.epoll_fd >= 0) {
        (void)close(CANptr.epoll_fd);
    }
    return program_exit;
}
