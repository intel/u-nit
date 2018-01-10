/*
 * Copyright (C) 2018 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#ifndef SAFE_MODE_HEADER_
#define SAFE_MODE_HEADER_

#include <stdbool.h>

void safe_mode_wait(const char *safe_mode_cmd, int pipe_fd);
bool execute_safe_mode(int pipe_fd, const char *failed_process_cmd, int sig);

#endif
