/*
 * Copyright (C) 2017 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#ifndef MAINLOOP_HEADER_
#define MAINLOOP_HEADER_

#include <stdbool.h>
#include <sys/signalfd.h>

enum timeout_result { TIMEOUT_STOP, TIMEOUT_CONTINUE };

struct mainloop_timeout;
struct mainloop_signal_handler;

bool mainloop_setup(void);
void mainloop_exit(void);
bool mainloop_start(void);

struct mainloop_timeout *
mainloop_add_timeout(uint32_t msec, enum timeout_result (*timeout_cb)(void));
void mainloop_remove_timeout(struct mainloop_timeout *mt);

struct mainloop_signal_handler *
mainloop_add_signal_handler(sigset_t *mask,
			    void (*signal_cb)(struct signalfd_siginfo *info));
void mainloop_remove_signal_handler(struct mainloop_signal_handler *msh);

void mainloop_set_post_iteration_callback(void (*cb)(void));

#endif
