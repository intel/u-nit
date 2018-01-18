/*
 * Copyright (C) 2017 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#ifndef CMDLINE_HEADER_
#define CMDLINE_HEADER_

#include <stdbool.h>

#ifndef ARGS_MAX
#define ARGS_MAX 128
#endif

#ifndef ENV_MAX
#define ENV_MAX 128
#endif

struct cmdline_contents {
	const char *args[ARGS_MAX];
	const char *env[ENV_MAX];
	char *_freeable;
};

bool parse_cmdline(const char *cmdline, struct cmdline_contents *contents);
void free_cmdline_contents(struct cmdline_contents *contents);

#endif
