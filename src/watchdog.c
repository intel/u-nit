/*
 * Copyright (C) 2017 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#include "watchdog.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/watchdog.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "log.h"
#include "mainloop.h"

#ifndef WATCHDOG_TIMEOUT_DEFAULT_SECS
#define WATCHDOG_TIMEOUT_DEFAULT_SECS 60
#endif

static int watchdog_fd = -1;
static struct mainloop_timeout *watchdog_timeout;

static enum timeout_result
watchdog_feed(void)
{
    int r;

    log_message("Feeding watchdog\n");
    r = ioctl(watchdog_fd, WDIOC_KEEPALIVE, 0);
    if (r < 0) {
        log_message("Could not ping watchdog: %m\n");
    }

    return TIMEOUT_CONTINUE;
}

void
close_watchdog(bool disarm)
{
    if (watchdog_fd > 0) {
        log_message("Closing watchdog\n");

        if (disarm) {
            int flags;

            flags = WDIOS_DISABLECARD;
            if (ioctl(watchdog_fd, WDIOC_SETOPTIONS, &flags) < 0) {
                log_message("Could not disable watchdog: %m\n");
            }

            /* Be safe and use magic close */
            if (write(watchdog_fd, "V", 1) < 0) {
                log_message("Could not send magic character to watchdog: %m\n");
            }
        }

        (void)close(watchdog_fd);

        if (watchdog_timeout != NULL) {
            mainloop_remove_timeout(watchdog_timeout);
            watchdog_timeout = NULL;
        }
    }
}

void
start_watchdog(void)
{
    uint32_t timeout_ms;
    int timeout = WATCHDOG_TIMEOUT_DEFAULT_SECS;
    int r;

    errno = 0;
    watchdog_fd = open("/dev/watchdog", O_WRONLY | O_CLOEXEC);
    if (watchdog_fd == -1) {
        log_message("Could not open `/dev/watchdog` %m:\n");
        goto end;
    }

    r = ioctl(watchdog_fd, WDIOC_GETTIMEOUT, &timeout);
    if ((r < 0) || (timeout < 1)) {
        timeout = WATCHDOG_TIMEOUT_DEFAULT_SECS;

        if (ioctl(watchdog_fd, WDIOC_SETTIMEOUT, &timeout) < 0) {
            log_message("Could not set watchdog timeout: %m\n");
            close_watchdog(false);
            goto end;
        }
    }

    /* Ensure watchdog feed happens before it expires */
    timeout_ms = (uint32_t)timeout * 900U;

    log_message("Watchdog timeout: %d - keep alive timeout(ms): %"PRIu32"\n",
            timeout, timeout_ms);

    watchdog_timeout = mainloop_add_timeout(timeout_ms, watchdog_feed);
    if (watchdog_timeout == NULL) {
        log_message("Could not create timeout for watchdog\n");
        close_watchdog(false);
        goto end;
    }

end:
    return;
}
