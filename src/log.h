/*
 * Copyright (C) 2017 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#ifndef LOG_HEADER_
#define LOG_HEADER_

#include <stdarg.h>

#include "macros.h"

#ifndef LOG_FILE
#define LOG_FILE "/dev/ttyS1"
#endif

void log_message(const char *s, ...) FORMAT_PRINTF(1, 2);

#endif
