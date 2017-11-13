/*
 * Copyright (C) 2017 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "log.h"

static int fd = -1;

FORMAT_PRINTF(1, 2)
void log_message(const char *s, ...)
{
	va_list va_alist;
	char buf[256];

	/* NOTE: this code expects `errno` to be unchanged. If adding any code
	 * before it that changes `errno`, remember to save it and restore
	 * before this code */
	va_start(va_alist, s);
	vsnprintf(buf, sizeof(buf), s, va_alist);
	va_end(va_alist);

	/* If we failed to open log file, no need to log anything */
	/* TODO maybe it's possible to do something, like try again later if
	 * failed by EINTR. Or wait fs to be mounted? */
	if (log_fd() != -1) {
		(void)write(fd, buf, strlen(buf));
	}
}

int log_fd(void)
{
	if (fd == -1) {
		/* TODO should we care about open() and write() being
		 * interrupted by signal? Doc says that is a problem for 'slow
		 * devices' - is our device slow? */
		fd = open(LOG_FILE,
			  O_WRONLY | O_CREAT | O_CLOEXEC | O_NOCTTY | O_SYNC,
			  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	}

	return fd;
}
