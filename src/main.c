/*
 * Copyright (C) 2017 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/reboot.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/ttydefaults.h> /* CKILL, CINTR, CQUIT, ... */
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "cmdline.h"
#include "inittab.h"
#include "log.h"
#include "mainloop.h"
#include "mount.h"
#include "safe-mode.h"
#include "watchdog.h"

#ifndef TIMEOUT_TERM
#define TIMEOUT_TERM 3000
#endif

#ifndef TIMEOUT_ONE_SHOT
#define TIMEOUT_ONE_SHOT 3000
#endif

#ifndef INITTAB_FILENAME
#define INITTAB_FILENAME "/etc/inittab"
#endif

enum stage {
	STAGE_SETUP,   /* Setting up the system, filesystems, etc */
	STAGE_STARTUP, /* Starting applications defined on inittab */
	STAGE_RUN, /* System is up and running. init is waiting on epoll loop */
	STAGE_TERMINATION, /* Got signal to shutdown and is sending SIGTERM to
			      processes */
	STAGE_SHUTDOWN,    /* Running all shutdown process defined on inittab */
	STAGE_CLOSE	/* Closing final resours before halt */
};

struct process {
	struct process *next;
	struct inittab_entry config;
	pid_t pid;
};

struct remaining_entries {
	struct inittab_entry *remaining;
	uint32_t pending_finish;
};

static struct remaining_entries remaining;

static struct inittab inittab_entries;

static struct process *running_processes;

static enum stage current_stage;

static struct mainloop_timeout *kill_timeout;
static struct mainloop_timeout *one_shot_timeout;

static int safe_mode_pipe_fd;

static int shutdown_command = RB_AUTOBOOT; /* Is this a sensible default? */

static bool safe_mode_on;

#ifdef COMPILING_COVERAGE
extern void __gcov_flush(void);
#endif

__attribute__((noreturn)) static void panic(const char *msg)
{
	log_message(msg);
	log_message("Panicking...");
#ifdef COMPILING_COVERAGE
	__gcov_flush();
	sync();
#endif
	_exit(1);
}

static void remove_process(struct process **list, struct process *p)
{
	bool removed = false;

	assert(p != NULL);
	assert(list != NULL);
	assert(*list != NULL);

	if (p == *list) {
		*list = (*list)->next;
		removed = true;
	} else {
		struct process *current = *list;

		while (current != NULL) {
			if (current->next == p) {
				current->next = p->next;
				removed = true;
				break;
			}
			current = current->next;
		}
	}

	free(p);
	assert(removed);
}

static void free_process_list(struct process **list)
{
	while (*list != NULL) {
		remove_process(list, *list);
	}
}

static void run_exec(struct cmdline_contents *cmd_contents)
{
#ifdef COMPILING_COVERAGE
	__gcov_flush();
	sync();
#endif
	errno = 0;
	if (execvpe(cmd_contents->args[0], (char *const *)cmd_contents->args,
		    (char *const *)cmd_contents->env) < 0) {
		fprintf(stderr, "Could not exec process '%s': %m\n",
			cmd_contents->args[0]);
	}
}

/*
 * This function duplicates fd on a file descriptor whose number
 * is assured to be bigger than STDERR_FILENO. This is necessary
 * when init sets children default file descriptors (stdin, stdout,
 * stderr) to avoid any useful file descriptor (like log_fd()) being
 * accidentally closed because it had a number <= STDERR_FILENO.
 */
static bool safe_dup(int *fd)
{
	bool result = true;
	int tmpfd;

	errno = 0;
	if (*fd <= STDERR_FILENO) {
		tmpfd = fcntl(*fd, F_DUPFD, STDERR_FILENO + 1);

		if (tmpfd < 0) {
			log_message("Couldn't safe dup file descriptor\n");
			result = false;
			goto end;
		}
		(void)close(*fd);
		*fd = tmpfd;
	}

end:
	return result;
}

static bool setup_stdio(void)
{
	int null_fd, out_fd;

	errno = 0;
	null_fd = open("/dev/null", O_RDONLY | O_NOCTTY);
	if (null_fd == -1) {
		log_message("Could not open /dev/null: %m\n");
		goto err_open_null;
	}

	if (!safe_dup(&null_fd)) {
		goto err_safe_dup_null;
	}

	out_fd = log_fd();
	if (out_fd == -1) {
		log_message("Could not open logfile: %m\n");
		goto err_open_out;
	}

	if (!safe_dup(&out_fd)) {
		goto err_safe_dup_out;
	}

	if (dup2(null_fd, STDIN_FILENO) == -1) {
		log_message("Could not dup null fd: %m\n");
		goto err_dup_null;
	}

	if (dup2(out_fd, STDOUT_FILENO) == -1) {
		log_message("Could not dup out fd: %m\n");
		goto err_dup_out;
	}

	if (dup2(out_fd, STDERR_FILENO) == -1) {
		log_message("Could not dup err fd: %m\n");
		goto err_dup_err;
	}

	close(out_fd);
	close(null_fd);

	return true;

err_dup_err:
	close(STDOUT_FILENO);
err_dup_out:
	close(STDIN_FILENO);
err_dup_null:
err_safe_dup_out:
	close(out_fd);
err_open_out:
err_safe_dup_null:
	close(null_fd);
err_open_null:
	return false;
}

static bool setup_safe_mode(struct inittab_entry *entry)
{
	struct process *p;
	int pipefd[2] = {0};

	assert(entry != NULL);

	/* If we are restarting safe-mode placeholder, we need to close previous
	 * pipe */
	if (safe_mode_pipe_fd > 0) {
		(void)close(safe_mode_pipe_fd);
	}

	errno = 0;
	if (pipe2(pipefd, O_CLOEXEC) < 0) {
		log_message("Could not create pipe for safe mode placeholder "
			    "process: %m\n");
		goto error_pipe;
	}

	p = calloc(1, sizeof(struct process));
	if (p == NULL) {
		log_message(
		    "Could not create create placeholder process: %m\n");
		goto error_calloc;
	}

	errno = 0;
	p->pid = fork();

	if (p->pid < 0) {
		log_message(
		    "Could not fork safe mode placeholder process: %m\n");
		goto error_fork;
	} else if (p->pid > 0) {
		memcpy(&p->config, entry, sizeof(struct inittab_entry));
		p->next = running_processes;
		running_processes = p;

		(void)close(pipefd[0]); /* pid1 won't read from it */
		safe_mode_pipe_fd = pipefd[1];

		log_message("Safe mode placeholder process created, pid %d\n",
			    p->pid);
	} else {
		/* p->pid == 0, this code runs on child, never returns */
		free(p); /* Make static analysis happy! */

		/* Dup pipefd[0] to avoid it being accidentaly closed on
		 * setup_stdio() due it not being bigger than STDERR_FILENO */
		if (!safe_dup(&pipefd[0]) || !setup_stdio()) {
#ifdef COMPILING_COVERAGE
			__gcov_flush();
			sync();
#endif
			_exit(1);
		}

		(void)close(pipefd[1]); /* placeholder won't write to it */

		safe_mode_wait(entry->process_name, pipefd[0]);
	}

	return true;

error_fork:
	free(p);
error_calloc:
	(void)close(pipefd[0]);
	(void)close(pipefd[1]);
error_pipe:
	return false;
}

static void setup_signals(sigset_t *mask)
{
	int i, r;

	int signals[] = {
	    SIGCHLD, /* To monitor started processes */
	    SIGTERM, /* Reboot signal */
	    SIGUSR1, /* Halt signal */
	    SIGUSR2  /* Shutdown signal */
	};

	r = sigemptyset(mask);
	assert(r == 0);

	for (i = 0; i < ARRAY_SIZE(signals); i++) {
		r = sigaddset(mask, signals[i]);
		assert(r == 0);
	}

	r = sigprocmask(SIG_SETMASK, mask, NULL);
	assert(r == 0);
}

/* Try to open the /dev/console, the maximum attempts is 10 times with
 * 100000 microseconds between each attempt
 */
static int open_console(const char *terminal, int mode)
{
	int times = 10;
	int tty = -1;

	for (; times > 0; times--) {
		struct timespec pause = {};

		errno = 0;
		tty = open(terminal, mode);
		if (tty >= 0) {
			break;
		}

		if (errno != EIO) {
			break;
		}

		/* Isn't this a really, really long time? */
		pause.tv_nsec = 100 * 1000 * 1000; // 100 msecs
		(void)nanosleep(&pause, NULL);
	}

	return tty;
}

static bool reset_console(int fd)
{
	int r;
	struct termios tty;
	bool result = false;

	r = tcgetattr(fd, &tty);
	if (r == -1) {
		goto end;
	}

	/* TODO assess what is really relevant to our case */
	tty.c_cflag &= CBAUD | CBAUDEX | CSIZE | CSTOPB | PARENB | PARODD;
	tty.c_cflag |= HUPCL | CLOCAL | CREAD;
	tty.c_iflag = IGNPAR | ICRNL | IXON | IXANY;
	tty.c_oflag = OPOST | ONLCR;
	tty.c_lflag = ISIG | ICANON | ECHO | ECHOCTL | ECHOPRT | ECHOKE;

	tty.c_cc[VINTR] = CINTR;   /* ^C */
	tty.c_cc[VQUIT] = CQUIT;   /* ^\ */
	tty.c_cc[VERASE] = CERASE; /* ASCII DEL (0177) */
	tty.c_cc[VKILL] = CKILL;   /* ^X */
	tty.c_cc[VEOF] = CEOF;     /* ^D */
	tty.c_cc[VTIME] = 0;
	tty.c_cc[VMIN] = 1;
	tty.c_cc[VSTART] = CSTART; /* ^Q */
	tty.c_cc[VSTOP] = CSTOP;   /* ^S */
	tty.c_cc[VSUSP] = CSUSP;   /* ^Z */
	tty.c_cc[VEOL] = _POSIX_VDISABLE;
	tty.c_cc[VREPRINT] = CREPRINT; /* ^R */
	tty.c_cc[VWERASE] = CWERASE;   /* ^W */
	tty.c_cc[VLNEXT] = CLNEXT;     /* ^V */
	tty.c_cc[VEOL2] = _POSIX_VDISABLE;

	/*
	 * Now set the terminal line.
	 * We don't care about non-transmitted output data
	 * and non-read input data.
	 */
	r = tcsetattr(fd, TCSANOW, &tty);
	if (r == -1) {
		goto end;
	}

	r = tcflush(fd, TCIOFLUSH);
	if (r == -1) {
		goto end;
	}

	/* Everything went OK */
	result = true;

end:
	return result;
}

static bool setup_stty(const char *terminal)
{
	int r, tty;

	tty = open_console(terminal, O_RDWR | O_NOCTTY | O_CLOEXEC);
	if (tty == -1) {
		log_message("Could not open terminal for child process\n");
		goto err_open;
	}

	if (!safe_dup(&tty)) {
		goto err_dup_in;
	}

	r = dup2(tty, STDIN_FILENO);
	if (r == -1) {
		log_message("Could not dup terminal for STDIN: %m\n");
		goto err_dup_in;
	}

	r = dup2(tty, STDOUT_FILENO);
	if (r == -1) {
		log_message("Could not dup terminal for STDOUT: %m\n");
		goto err_dup_out;
	}

	r = dup2(tty, STDERR_FILENO);
	if (r == -1) {
		log_message("Could not dup terminal for STDERR: %m\n");
		goto err_dup_errno;
	}

	if (!reset_console(tty)) {
		goto err_console;
	}

	(void)close(tty);

	return true;

err_console:
	(void)close(STDERR_FILENO);
err_dup_errno:
	(void)close(STDOUT_FILENO);
err_dup_out:
	(void)close(STDIN_FILENO);
err_dup_in:
	(void)close(tty);
err_open:
	return false;
}

static void setup_child(const char *command, const char *console,
			int32_t core_id)
{
	int r;
	pid_t p;
	sigset_t mask;
	struct cmdline_contents cmd_contents = {};

	r = sigemptyset(&mask);
	assert(r == 0);

	r = sigprocmask(SIG_SETMASK, &mask, NULL);
	assert(r == 0);

	/* TODO check if this can be here (child process) or should be done on
	 * pid 1 */
	if (!parse_cmdline(command, &cmd_contents)) {
		goto end;
	}

	/* Become a session leader */
	p = setsid();
	if (p == -1) {
		goto end;
	}

	/* Set CPU affinity if defined on inittab */
	if (core_id >= 0) {
		cpu_set_t set;

		CPU_ZERO(&set);
		CPU_SET(core_id, &set);

		errno = 0;
		if (sched_setaffinity(0, sizeof(set), &set) == -1) {
			log_message(
			    "Could not set CPU affinity for process '%s': %m\n",
			    command);
			goto end;
		}
	}

	/* Configure terminal for child */
	if (console[0] != '\0') {
		if (!setup_stty(console)) {
			log_message(
			    "Could not setup tty '%s' for process '%s'\n",
			    console, command);
			goto end;
		}

		/* Give a controlling terminal for the process */
		r = ioctl(STDIN_FILENO, TIOCSCTTY, 0);
		if (r == -1) {
			log_message(
			    "Could not handle controlling terminal: %m\n");
			goto end;
		}
	} else {
		if (!setup_stdio()) {
			goto end;
		}
	}

	run_exec(&cmd_contents);

end:
	free_cmdline_contents(&cmd_contents);
	return;
}

/* Expects SIGCHLD to be disabled when called */
static pid_t spawn_exec(const char *command, const char *console,
			int32_t core_id)
{
	pid_t p;

	p = fork();

	log_message("fork result for '%s': %d\n", command, p);
	/* the caller is reponsible to check the error */
	if (p != 0) {
		return p;
	}

	/* child code, should never return */
	setup_child(command, console, core_id);

#ifdef COMPILING_COVERAGE
	__gcov_flush();
	sync();
#endif

	/* If returned, we have a lost process */
	_exit(1);
}

static enum timeout_result one_shot_timeout_cb(void)
{
	if (remaining.pending_finish > 0U) {
		log_message("Some process are taking longer than expected to "
			    "complete\n");
		/* TODO what to do? */
	}

	one_shot_timeout = NULL;
	return TIMEOUT_STOP;
}

static struct process *find_safe_mode_process(void)
{
	struct process *p = running_processes;
	while (p != NULL) {
		if (p->config.type == SAFE_MODE) {
			break;
		}
		p = p->next;
	}

	return p;
}

static void start_safe_mode(const char *process_name, int signal)
{
	bool r;
	struct process *p;

	p = find_safe_mode_process();

	if (p == NULL) {
		/* No safe mode process, let's panic the kernel */
		panic("Safe mode required, but safe mode process "
		      "placeholder not found!\n");
	}
	r = execute_safe_mode(safe_mode_pipe_fd, process_name, signal);
	if (!r) {
		panic("Couldn't start safe mode!");
	}
	/* This will not return to false once is set.
	 It is expected that init will exit after. If that's not the
	 case, this approach must be redefined. */
	safe_mode_on = true;
}

static bool start_processes(struct inittab_entry *list)
{
	int32_t current_order;
	bool result = true, has_one_shot = false;

	if (list != NULL) {
		struct inittab_entry *entry;

		remaining.pending_finish = 0;
		entry = list;
		current_order = entry->order;

		while ((entry != NULL) && (entry->order == current_order)) {
			struct process *p;

			/* First, let's see if we have memory for anciliary
			 * struct */
			p = calloc(1, sizeof(struct process));
			if (p == NULL) {
				result = false;
				break;
			}
			p->pid = spawn_exec(entry->process_name,
					    entry->ctty_path, entry->core_id);

			if (p->pid > 0) {
				/* Stores inittab entry information on process
				 * struct */
				memcpy(&p->config, entry,
				       sizeof(struct inittab_entry));

				if (is_one_shot_entry(&p->config)) {
					remaining.pending_finish++;
					log_message("Pending increased to %d\n",
						    remaining.pending_finish);
					has_one_shot = true;
				}

				p->next = running_processes;
				running_processes = p;
			} else {
				log_message("Could not fork process!\n");
				if (is_safe_entry(entry)) {
					/* TODO check if sending -1 makes sense.
					 * That parameter should be signal (or
					 * exit code) of crashed process. But
					 * here, it just failed to fork... */
					start_safe_mode(entry->process_name,
							-1);
				}
				free(p);
			}

			entry = entry->next;

			/* If next entry is on next order, but no one-shot entry
			 * was started in this order, let's simply start next
			 * order */
			if ((entry != NULL) &&
			    (entry->order != current_order) && !has_one_shot) {
				current_order = entry->order;
			}
		}

		remaining.remaining = entry;
	}

	if (has_one_shot) {
		one_shot_timeout =
		    mainloop_add_timeout(TIMEOUT_ONE_SHOT, one_shot_timeout_cb);
		if (one_shot_timeout == NULL) {
			/* TODO We couldn't add a timeout to watch over one_shot
			 * process startup time. How bad is that? */
			log_message("Init won't be able to watch one-shot "
				    "process startup time\n");
		}
	}

	return result;
}

/* Run after each mainloop iteration. Ensures that init
 * is on correct 'stage' - and perform actions of that stage
 */
static void stage_maintenance(void)
{
	switch (current_stage) {
	case STAGE_STARTUP:
	case STAGE_SHUTDOWN:
		if (remaining.pending_finish == 0U) {
			if (one_shot_timeout != NULL) {
				mainloop_remove_timeout(one_shot_timeout);
				one_shot_timeout = NULL;
			}

			if (remaining.remaining != NULL) {
				start_processes(remaining.remaining);
			} else {
				/* No more process to start, decide on what
				 * next*/
				if (current_stage == STAGE_STARTUP) {
					current_stage = STAGE_RUN;
					/* We can rest until signal to terminate
					 */
					mainloop_set_post_iteration_callback(
					    NULL);
				} else {
					current_stage = STAGE_CLOSE;
				}
			}
		}
		break;
	case STAGE_TERMINATION:
		/* If all process finished, time to start 'shutdown' ones.
		 * Note that safe_mode process (safe_mode on or not)
		 * will not be terminated/killed, unless it run and exited */
		if ((running_processes == NULL) ||
		    (running_processes->next == NULL)) {
			if (inittab_entries.shutdown_list != NULL) {
				current_stage = STAGE_SHUTDOWN;
				start_processes(inittab_entries.shutdown_list);
			} else {
				/* Nothing to run on shutdown. Init is closing
				 */
				current_stage = STAGE_CLOSE;
			}

			/* Since all processes ended, no need for timer to kill
			 * them anymore */
			if (kill_timeout != NULL) {
				mainloop_remove_timeout(kill_timeout);
				kill_timeout = NULL;
			}
		}
		break;
	default:
		/* Nothing */
		break;
	}

	/* Init is closing, abandon the loop */
	if (current_stage == STAGE_CLOSE) {
		mainloop_exit();
	}
}

static struct process *find_process(pid_t pid)
{
	struct process *p = running_processes;
	while (p != NULL) {
		if (p->pid == pid) {
			break;
		}
		p = p->next;
	}

	return p;
}

static enum timeout_result kill_timeout_cb(void)
{
	if (current_stage == STAGE_TERMINATION) {
		struct process *p = running_processes;

		log_message("Sending KILL signal to processes that refused to "
			    "term in timeout\n");
		while (p != NULL) {
			log_message("Sending KILL signal to %d (%s)\n", p->pid,
				    p->config.process_name);
			kill(p->pid, SIGKILL);
			p = p->next;
		}
	}

	kill_timeout = NULL;
	return TIMEOUT_STOP;
}

static void term_running_process(void)
{
	struct process *p = running_processes;
	while (p != NULL) {
		log_message("Sending TERM signal to %d (%s)\n", p->pid,
			    p->config.process_name);
		kill(p->pid, SIGTERM);
		p = p->next;
	}

	/* Set up a timer to kill any process that refuses to die */
	kill_timeout = mainloop_add_timeout(TIMEOUT_TERM, kill_timeout_cb);
}

static void handle_shutdown_cmd(struct signalfd_siginfo *info, int command)
{
	(void)info; /* Not used */

	/* Ensure 'remaining list' is cleaned up */
	remaining.remaining = NULL;
	remaining.pending_finish = 0;

	/* Cancel any pending one-shot timeout */
	if (one_shot_timeout != NULL) {
		mainloop_remove_timeout(one_shot_timeout);
		one_shot_timeout = NULL;
	}

	/* We wait for all running process to exit before starting shutdown ones
	 */
	/* TODO is this right? */
	current_stage = STAGE_TERMINATION;
	term_running_process();

	shutdown_command = command;

	/* Stages will change again, let's keep track */
	mainloop_set_post_iteration_callback(stage_maintenance);
}

static void handle_child_exit(struct signalfd_siginfo *info)
{
	(void)info;

	pid_t pid;
	struct process *p;

	struct {
		const char *process_name;
		int signal;
	} deceased_safe_process = {};

	bool start_safe_process = false;
	bool restart_safe_mode_placeholder = false;

	/* Reap processes. Multiple SIGCHLD may have been coalesced into one
	 * signalfd entry */
	while (true) {
		int wstatus;

		errno = 0;
		pid = waitpid(-1, &wstatus, WNOHANG);
		if (pid <= 0) {
			if (errno != 0 && errno != ECHILD) {
				log_message("Error on waitpid: %m\n");
				/* A safe mode process may have crashed or exit
				 * with failure and init doesn't have a way to
				 * know that if waitpid fails What to do?
				 * Current approach, panic!*/
				panic("Won't go anywhere if waitpid() is not "
				      "working!\n");
			}
			break;
		}

		log_message("child exited: %d\n", pid);

		/* Get our process info from pid */
		p = find_process(pid);
		if (p == NULL) {
			/* TODO what to do? */
			log_message("Couldn't find process %d\n", pid);
			continue;
		}

		log_message("reaping [%d] (%s)'\n", p->pid,
			    p->config.process_name);
		/* A safe process crash - or exitcode != 0 - asks for safe_mode
		 */
		if (is_safe_entry(&p->config) &&
		    (!WIFEXITED(wstatus) || (WEXITSTATUS(wstatus) != 0))) {
			log_message(
			    "Abnormal termination of safe process [%d] (%s)\n",
			    p->pid, p->config.process_name);

			if (p->config.type == SAFE_MODE) {
				/* Safe mode process is dead. Have we started
				 * safe mode or is still just the placeholder
				 * process? If the first, all we can do is
				 * panic. For the later, we'll just try to
				 * restart it (outside this loop, so we can
				 * properly reap all dead children). */
				if (safe_mode_on) {
					panic("Safe mode process crashed!\n");
				}

				restart_safe_mode_placeholder = true;
			} else {
				start_safe_process = true;

				deceased_safe_process.process_name =
				    p->config.process_name;
				if (WIFSIGNALED(wstatus)) {
					deceased_safe_process.signal =
					    WTERMSIG(wstatus);
				}
			}
		}

		/* One shot process terminated decrement counter to start
		 * remaining_processes*/
		if (is_one_shot_entry(&p->config) &&
		    ((current_stage == STAGE_STARTUP) ||
		     (current_stage == STAGE_SHUTDOWN))) {
			remaining.pending_finish--;
			log_message("Pending decreased to %d\n",
				    remaining.pending_finish);
		}

		/* Process exited, remove from our running process list */
		remove_process(&running_processes, p);
	}

	if (start_safe_process) {
		start_safe_mode(deceased_safe_process.process_name,
				deceased_safe_process.signal);
	}

	if (restart_safe_mode_placeholder &&
	    !setup_safe_mode(inittab_entries.safe_mode_entry)) {
		panic("Can't keep normal execution without safe mode "
		      "placeholder process\n");
	}
}

static void signal_handler(struct signalfd_siginfo *info)
{
	log_message("Received signal - si_signo: %d - ssi_code: %d - ssi_pid: "
		    "%d - ssi_status %d\n",
		    info->ssi_signo, info->ssi_code, info->ssi_pid,
		    info->ssi_status);

	switch (info->ssi_signo) {
	case SIGCHLD:
		handle_child_exit(info);
		break;
	case SIGTERM:
		handle_shutdown_cmd(info, RB_AUTOBOOT);
		break;
	case SIGUSR1:
		handle_shutdown_cmd(info, RB_HALT_SYSTEM);
		break;
	case SIGUSR2:
		handle_shutdown_cmd(info, RB_POWER_OFF);
		break;
	default:
		/* Nothing to do*/
		break;
	}
}

static void do_reboot(int cmd)
{
	/* Umount fs */
	sync(); /* Ensure fs are synced */
	mount_umount_filesystems();

	close_watchdog(true);

#ifdef COMPILING_COVERAGE
	__gcov_flush();
	sync();
#endif

	/* Reboot is not caught by address sanitizer, so let's
	 * simply exit application. This is akin to Valgrind
	 * behaviour, as `reboot` fails and init simply exits */
#ifndef __SANITIZE_ADDRESS__
	if (reboot(cmd) < 0) {
		log_message("Reboot command failed: %m\n");
	}
#endif
}

static bool disable_sysrq(void)
{
	FILE *f;
	bool result = true;

	errno = 0;
	f = fopen("/proc/sys/kernel/sysrq", "r+e");
	if (f == NULL) {
		log_message("Could not open sysrq file: %m\n");
		result = false;
		goto end;
	}

	errno = 0;
	if (fputc((int)'0', f) == EOF) {
		log_message("Could not write to sysrq file: %m\n");
		result = false;
	}

	(void)fclose(f);

end:
	return result;
}

static bool setup_console(void)
{
	int tty;
	bool r = false;

	/* Closing stdio descriptors */
	close(STDIN_FILENO);
	close(STDERR_FILENO);
	close(STDOUT_FILENO);

	tty = open_console("/dev/console", O_WRONLY | O_NOCTTY | O_CLOEXEC);
	if (tty != -1) {
		r = reset_console(tty);
		close(tty); /* Why this close here?*/
	}

	return r;
}

#ifndef NDEBUG
static bool is_inside_container(void)
{
	/* This check is valid only if we are PID 1. So, if we are
	 * to be run on diferent PID, we should rethink this function */
	if (getenv("container") != NULL) {
		return true;
	}

	return false;
}
#endif

int main(int argc, char *argv[])
{
	sigset_t mask;
	struct mainloop_signal_handler *msh = NULL;
	int r, result = EXIT_SUCCESS;

	current_stage = STAGE_SETUP;

	if (getpid() != 1) {
		result = EXIT_FAILURE;
		goto end;
	}

	(void)umask(0);

	if (!mount_mount_filesystems()) {
		result = EXIT_FAILURE;
		goto end;
	}

	/* Ensure init will not block any umount call later */
	r = chdir("/");
	if (r == -1) {
		result = EXIT_FAILURE;
		goto end;
	}

#ifndef NDEBUG
	if (!is_inside_container() && !setup_console()) {
#else
	if (!setup_console()) {
#endif
		result = EXIT_FAILURE;
		goto end;
	}

	/* Become a session leader. Only reason to fail is if we already
	 * are session leader - in a container, for instance */
	(void)setsid();

	/* Block signals that should only be caught by epoll */
	setup_signals(&mask);

	/* To catch Ctrl+alt+del. We will receive SIGINT on Ctrl+alt+del (which
	 * we ignore) */
	r = reboot(RB_DISABLE_CAD);
	if (r < 0) {
		log_message("Could not disable Ctrl+Alt+Del: %m\n");
		/* How big of a problem is this? Should we abort or life goes
		 * on? */
	}

	/* Disable sysrq */
	if (!disable_sysrq()) {
		log_message("Coud not disable Sysrq keys\n");
	}

	/* Prepares mainloop to run */
	if (!mainloop_setup()) {
		result = EXIT_FAILURE;
		goto end;
	}

	/* Sets handler to be run after each iteration. This handler
	 * will track init state machine  */
	mainloop_set_post_iteration_callback(stage_maintenance);

	msh = mainloop_add_signal_handler(&mask, signal_handler);
	if (msh == NULL) {
		result = EXIT_FAILURE;
		goto end;
	}

	start_watchdog();

	if (!read_inittab(INITTAB_FILENAME, &inittab_entries)) {
		result = EXIT_FAILURE;
		goto end;
	}

	/* Start a placeholder process to be used if we need to go into safe
	 * mode*/
	if (!setup_safe_mode(inittab_entries.safe_mode_entry)) {
		result = EXIT_FAILURE;
		goto end;
	}

	/* Start initial list of process */
	current_stage = STAGE_STARTUP;
	start_processes(inittab_entries.startup_list);

	mainloop_start();

	free_process_list(&running_processes);

	free_inittab_entry_list(inittab_entries.startup_list);
	free_inittab_entry_list(inittab_entries.shutdown_list);
	free_inittab_entry_list(inittab_entries.safe_mode_entry);

end:

	if (msh != NULL) {
		mainloop_remove_signal_handler(msh);
	}

	if (result != EXIT_FAILURE) {
		do_reboot(shutdown_command);
		/* If we are here, reboot failed */
		result = EXIT_FAILURE;
	}

#ifdef COMPILING_COVERAGE
	__gcov_flush();
	sync();
#endif

	return result;
}
