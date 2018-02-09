/*
 * Copyright (C) 2017 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "safe-mode.h"

#include "cmdline.h"
#include "log.h"

struct shared_info {
	char process_name[1024];
	int signal;
};

#ifdef COMPILING_COVERAGE
extern void __gcov_flush(void);
#endif

/* This code runs on child process (hence usage of printf) */
void safe_mode_wait(const char *safe_mode_cmd, int pipe_fd)
{
	int i, r;
	ssize_t size;
	size_t read_so_far = 0;
	sigset_t mask;
	struct shared_info info = {};
	struct cmdline_contents cmd_contents = {};
	char *signal_str = NULL;

	r = sigemptyset(&mask);
	assert(r == 0);

	/* Let's ignore all signals */
	r = sigprocmask(SIG_SETMASK, &mask, NULL);
	assert(r == 0);

	while (true) {
		errno = 0;
		size = read(pipe_fd, &info + read_so_far,
			    sizeof(struct shared_info) - read_so_far);

		if (size < 0) {
			/* All signals are blocked above, should we really care
			 * about EINTR? */
			if (errno != EINTR) {
				printf("[Safe mode placeholder] Error reading "
				       "from "
				       "pipe: %m\n");

				/* What's the right thing to do? Currently,
				 * exits so pid 1 notices */
				goto error;
			}
		} else if (size == 0) {
			printf(
			    "[Safe mode placeholder] Unexpected end of pipe\n");
			goto error;
		} else {
			read_so_far += size;
			if (read_so_far == sizeof(struct shared_info)) {
				break; /* We've read everything */
			}
		}
	}

	printf("[Safe mode placeholder] Got process '%s' and signal '%d', "
	       "executing safe mode application\n",
	       info.process_name, info.signal);

	/* As pipefd may have been duped before, let's close it here to be sure
	 */
	(void)close(pipe_fd);

	if (!parse_cmdline(safe_mode_cmd, &cmd_contents)) {
		goto error;
	}

	/* Substitute tags <proc> and <exitcode> */
	if (asprintf(&signal_str, "%d", info.signal) < 0) {
		printf("[Safe mode placeholder] Couldn't send signal number to "
		       "safe process\n");
		signal_str = NULL;
	}
	for (i = 0; cmd_contents.args[i] != NULL; i++) {
		if (strcmp("<proc>", cmd_contents.args[i]) == 0) {
			cmd_contents.args[i] = info.process_name;
		} else if ((strcmp("<exitcode>", cmd_contents.args[i]) == 0) &&
			   (signal_str != NULL)) {
			cmd_contents.args[i] = signal_str;
		}
	}

#ifdef COMPILING_COVERAGE
	__gcov_flush();
	sync();
#endif

	errno = 0;
	if (execvpe(cmd_contents.args[0], (char *const *)cmd_contents.args,
		    (char *const *)cmd_contents.env) < 0) {
		printf("[Safe mode placeholder] Could not execute safe "
		       "process: %m\n");
		goto error;
	}

error:
	free(signal_str);
#ifdef COMPILING_COVERAGE
	__gcov_flush();
	sync();
#endif
	_exit(1);
}

bool execute_safe_mode(int pipe_fd, const char *failed_process_cmd, int sig)
{
	bool result = true;
	size_t written = 0;
	struct shared_info info = {};
	ssize_t size;

	assert(pipe_fd != 0);
	assert(failed_process_cmd != NULL);

	strncpy(info.process_name, failed_process_cmd,
		sizeof(info.process_name) - 1U);
	info.process_name[sizeof(info.process_name) - 1U] = '\0';
	info.signal = sig;

	while (written != sizeof(struct shared_info)) {
		errno = 0;
		size = write(pipe_fd, &info + written,
			     sizeof(struct shared_info) - written);
		if (size < 0) {
			/* pid1 shouldn't be bothered by signals but those on
			 * signalfd. Do we really need to care about EINTR? */
			if (errno != EINTR) {
				log_message(
				    "Could not write to safe mode pipe!");
				result = false;
				goto end;
			}
		} else if (size > 0) {
			written += size;
		} else {
			/* A zero write is unexpected - could it happen? */
			log_message("Zero write to safe mode pipe!");
			result = false;
			goto end;
		}
	}

end:
	return result;
}
