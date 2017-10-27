#include <assert.h>
#include <errno.h>
#include <fcntl.h>
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

#include "inittab.h"
#include "log.h"
#include "mainloop.h"
#include "mount.h"
#include "watchdog.h"

#ifndef TIMEOUT_TERM
#define TIMEOUT_TERM 3000
#endif

#ifndef TIMEOUT_ONE_SHOT
#define TIMEOUT_ONE_SHOT 3000
#endif

#ifndef INITTAB_FILENAME
#define INITTAB_FILENAME "/etc/inittab2"
#endif

enum stage {
    STAGE_SETUP, /* Setting up the system, filesystems, etc */
    STAGE_STARTUP, /* Starting applications defined on inittab */
    STAGE_RUN, /* System is up and running. init is waiting on epoll loop */
    STAGE_TERMINATION, /* Got signal to shutdown and is sending SIGTERM to processes */
    STAGE_SHUTDOWN, /* Running all shutdown process defined on inittab */
    STAGE_CLOSE /* Closing final resours before halt */
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

static int shutdown_command = RB_AUTOBOOT; /* Is this a sensible default? */

static void
remove_process(struct process **list, struct process *p)
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

static void
free_process_list(struct process **list)
{
    while (*list != NULL) {
        remove_process(list, *list);
    }
}

static int
run_exec(const char *command)
{
    /* should run the new process using a bash ? */
    int r;

    /* Give a controlle terminal for the process */
    r = ioctl(STDIN_FILENO, TIOCSCTTY, 1);
    if (r == -1) {
        return r;
    }

    /* Should configure ENV ? */
    return execl("/bin/sh", "/bin/sh", "-c", command, NULL);
}

static void
setup_signals(sigset_t *mask)
{
    int i, r;

    int signals[] = {
        SIGCHLD,    /* To monitor started processes */
        SIGTERM,    /* Reboot signal */
        SIGUSR1,    /* Halt signal */
        SIGUSR2     /* Shutdown signal */
    };

    r = sigemptyset(mask);
    assert(r == 0);

    for (i = 0; i < (sizeof(signals)/sizeof(signals[0])); i++) {
        r = sigaddset(mask, signals[i]);
        assert(r == 0);
    }

    r = sigprocmask(SIG_SETMASK, mask, NULL);
    assert(r == 0);
}

/* Try to open the /dev/console, the maximum attempts is 10 times with
 * 100000 microseconds between each attempt
 */
static int
open_console(const char *terminal, int mode)
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

static bool
reset_console(int fd)
{
    int r;
    struct termios tty;
    bool result = false;

    r = tcgetattr(fd, &tty);
    if (r == -1) {
        goto end;
    }

    /* TODO assess what is really relevant to our case */
    tty.c_cflag &= CBAUD|CBAUDEX|CSIZE|CSTOPB|PARENB|PARODD;
    tty.c_cflag |= HUPCL|CLOCAL|CREAD;
    tty.c_iflag = IGNPAR|ICRNL|IXON|IXANY;
    tty.c_oflag = OPOST|ONLCR;
    tty.c_lflag = ISIG|ICANON|ECHO|ECHOCTL|ECHOPRT|ECHOKE;

    tty.c_cc[VINTR]     = CINTR;            /* ^C */
    tty.c_cc[VQUIT]     = CQUIT;            /* ^\ */
    tty.c_cc[VERASE]    = CERASE;           /* ASCII DEL (0177) */
    tty.c_cc[VKILL]     = CKILL;            /* ^X */
    tty.c_cc[VEOF]      = CEOF;             /* ^D */
    tty.c_cc[VTIME]     = 0;
    tty.c_cc[VMIN]      = 1;
    tty.c_cc[VSTART]    = CSTART;           /* ^Q */
    tty.c_cc[VSTOP]     = CSTOP;            /* ^S */
    tty.c_cc[VSUSP]     = CSUSP;            /* ^Z */
    tty.c_cc[VEOL]      = _POSIX_VDISABLE;
    tty.c_cc[VREPRINT]  = CREPRINT;         /* ^R */
    tty.c_cc[VWERASE]   = CWERASE;          /* ^W */
    tty.c_cc[VLNEXT]    = CLNEXT;           /* ^V */
    tty.c_cc[VEOL2]     = _POSIX_VDISABLE;

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

static int
setup_stty(const char *terminal)
{
    int r, tty;

    tty = open_console(terminal, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (tty == -1) {
        goto err_open;
    }

    r = dup2(tty, STDIN_FILENO);
    if (r == -1) {
        goto err_dup_in;
    }

    r = dup2(tty, STDOUT_FILENO);
    if (r == -1) {
        goto err_dup_out;
    }

    r = dup2(tty, STDERR_FILENO);
    if (r == -1) {
        goto err_dup_errno;
    }

    if (!reset_console(tty)) {
        goto err_console;
    }

    return tty;

err_console:
    (void)close(STDERR_FILENO);
err_dup_errno:
    (void)close(STDOUT_FILENO);
err_dup_out:
    (void)close(STDIN_FILENO);
err_dup_in:
    (void)close(tty);
err_open:
    return -1;
}

/* Expects SIGCHLD to be disabled when called */
static pid_t
spawn_exec(const char *command, const char *console)
{
    int r;
    pid_t p;
    sigset_t mask;

    p = fork();

    log_message("fork result for '%s': %d\n", command, p);
    /* the caller is reponsible to check the error */
    if (p != 0) {
        return p;
    }

    /* child code */
    r = sigemptyset(&mask);
    assert(r == 0);

    r = sigprocmask(SIG_SETMASK, &mask, NULL);
    assert(r == 0);

    /* Become a session leader */
    p = setsid();
    if (p == -1) {
        return -1;
    }

    /* Configure terminal for child */
    if (setup_stty(console) < 0) {
        return -1;
    }

    return run_exec(command);
}

static enum timeout_result
one_shot_timeout_cb(void)
{
    if (remaining.pending_finish > 0U) {
        log_message("Some process are taking longer than expected to complete\n");
        /* TODO what to do? */
    }

    one_shot_timeout = NULL;
    return TIMEOUT_STOP;
}

static bool
start_processes(struct inittab_entry *list)
{
    int32_t current_order;
    bool result = true;

    if (list != NULL) {
        struct inittab_entry *entry;

        remaining.pending_finish = 0;
        entry = list;
        current_order = entry->order;

        while ((entry != NULL) && (entry->order == current_order)) {
            struct process *p;

            /* First, let's see if we have memory for anciliary struct */
            p = calloc(1, sizeof(struct process));
            if (p == NULL) {
                result = false;
                break;
            }
            /* Always /dev/console? Do we always need this? */
            p->pid = spawn_exec(entry->process_name, "/dev/console");

            if (p->pid > 0) {
                /* Stores inittab entry information on process struct */
                memcpy(&p->config, entry, sizeof(struct inittab_entry));

                if (is_one_shot_entry(&p->config)) {
                    remaining.pending_finish++;
                }

                p->next = running_processes;
                running_processes = p;
            } else {
                log_message("Could not start process!\n");
                free(p);
                /* TODO what to do? if a safe one fails, maybe safe state?*/
            }

            entry = entry->next;
        }

        remaining.remaining = entry;
    }

    if (result) {
        one_shot_timeout = mainloop_add_timeout(TIMEOUT_ONE_SHOT, one_shot_timeout_cb);
        if (one_shot_timeout == NULL) {
            /* TODO We couldn't add a timeout to watch over one_shot process
             * startup time. How bad is that? */
            log_message("Init won't be able to watch one-shot process startup time\n");
        }
    }

    return result;
}

/* Run after each mainloop iteration. Ensures that init
 * is on correct 'stage' - and perform actions of that stage
 */
static void
stage_maintenance(void)
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
                /* No more process to start, decide on what next*/
                if (current_stage == STAGE_STARTUP) {
                    current_stage = STAGE_RUN;
                    /* We can rest until signal to terminate */
                    mainloop_set_post_iteration_callback(NULL);
                } else {
                    current_stage = STAGE_CLOSE;
                }
            }
        }
        break;
    case STAGE_TERMINATION:
        /* If all process finished, time to start 'shutdown' ones */
        if (running_processes == NULL) {
            current_stage = STAGE_SHUTDOWN;
            start_processes(inittab_entries.shutdown_list);

            /* Since all processes ended, no need for timer to kill them anymore */
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

static struct process *
find_process(pid_t pid)
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

static enum timeout_result
kill_timeout_cb(void)
{
    if (current_stage == STAGE_TERMINATION) {
        struct process *p = running_processes;

        log_message("Sending KILL signal to processes that refused to term in timeout\n");
        while (p != NULL) {
            log_message("Sending KILL signal to %d (%s)\n", p->pid, p->config.process_name);
            kill(p->pid, SIGKILL);
            p = p->next;
        }
    }

    kill_timeout = NULL;
    return TIMEOUT_STOP;
}

static void
term_running_process(void)
{
    struct process *p = running_processes;
    while (p != NULL) {
        log_message("Sending TERM signal to %d (%s)\n", p->pid, p->config.process_name);
        kill(p->pid, SIGTERM);
        p = p->next;
    }

    /* Set up a timer to kill any process that refuses to die */
    kill_timeout = mainloop_add_timeout(TIMEOUT_TERM, kill_timeout_cb);
}

static void
handle_shutdown_cmd(struct signalfd_siginfo *info, int command)
{
    (void)info; /* Not used */

    /* Ensure 'remaining list' is cleaned up */
    remaining.remaining = NULL;
    remaining.pending_finish = 0;

    /* We wait for all running process to exit before starting shutdown ones */
    /* TODO is this right? */
    current_stage = STAGE_TERMINATION;
    term_running_process();

    shutdown_command = command;

    /* Stages will change again, let's keep track */
    mainloop_set_post_iteration_callback(stage_maintenance);
}

static void
handle_child_exit(struct signalfd_siginfo *info)
{
    pid_t pid;

    /* Get our process info from pid */
    struct process *p = find_process((pid_t)info->ssi_pid);

    if (p == NULL) {
        /* TODO what to do? */
        log_message("Couldn't find process %d\n", info->ssi_pid);
        return;
    }

    /* Reap process */
    pid = waitpid(p->pid, NULL, WNOHANG);
    if (pid == -1) {
        /* TODO wait, what? It should not happen as epoll told that this
         * process exited. But, what to do if it does happen? */
        return;
    }

    /* A safe process crash asks for safe_mode */
    if (is_safe_entry(&p->config) && ((info->ssi_code == CLD_KILLED) || (info->ssi_code == CLD_DUMPED))) {
        /* TODO safe mode */
    }

    log_message("Was it called %d\n", p->config.type);
    /* One shot process terminated decrement counter to start remaining_processes*/
    if (is_one_shot_entry(&p->config)) {
        // TODO account only for child termination
        remaining.pending_finish--;
        log_message("Pending decreased to %d\n", remaining.pending_finish);
    }

    /* Process exited, remove from our running process list */
    remove_process(&running_processes, p);
}

static void
signal_handler(struct signalfd_siginfo *info)
{
    log_message("Received signal - si_signo: %d - ssi_code: %d - ssi_pid: %d - ssi_status %d\n",
            info->ssi_signo, info->ssi_code, info->ssi_pid, info->ssi_status);

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

static bool
do_reboot(int cmd)
{
    bool result = true;

    /* Umount fs */
    sync(); /* Ensure fs are synced */
    mount_umount_filesystems();

    close_watchdog(true);

    if (reboot(cmd) < 0) {
        log_message("Reboot command failed: %m\n");
        result = false;
    }

    return result;
}

static bool
disable_sysrq(void)
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

static bool
setup_console(void)
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

int
main(int argc, char *argv[])
{
    sigset_t mask;
    struct mainloop_signal_handler *msh = NULL;
    int r, result = EXIT_SUCCESS;
    pid_t p;

    current_stage = STAGE_SETUP;

    if (getpid() != 1) {
        result = EXIT_FAILURE;
        goto end;
    }

    if (!read_inittab(INITTAB_FILENAME, &inittab_entries)) {
        result = EXIT_FAILURE;
        goto end;
    }

    (void)umask(0);

    /* Ensure init will not block any umount call later */
    r = chdir("/");
    if (r == -1) {
        result = EXIT_FAILURE;
        goto end;
    }

    if (!setup_console()) {
        result = EXIT_FAILURE;
        goto end;
    }

    /* Become a session leader */
    p = setsid();
    if (p == -1) {
        result = EXIT_FAILURE;
        goto end;
    }

    if (!mount_mount_filesystems()) {
        result = EXIT_FAILURE;
        goto end;
    }

    /* Block signals that should only be caught by epoll */
    setup_signals(&mask);

    /* To catch Ctrl+alt+del. We will receive SIGINT on Ctrl+alt+del (which we ignore) */
    r = reboot(RB_DISABLE_CAD);
    if (r < 0) {
        log_message("Could not disable Ctrl+Alt+Del: %m\n");
        /* How big of a problem is this? Should we abort or life goes on? */
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

    /* Start initial list of process */
    current_stage = STAGE_STARTUP;
    start_processes(inittab_entries.startup_list);

    mainloop_start();

    if (!do_reboot(shutdown_command)) {
        result = EXIT_FAILURE;
        goto end;
    }

end:

    free_process_list(&running_processes);

    free_inittab_entry_list(inittab_entries.startup_list);
    free_inittab_entry_list(inittab_entries.shutdown_list);
    free_inittab_entry_list(inittab_entries.safe_mode_entry);

    if (msh != NULL) {
        mainloop_remove_signal_handler(msh);
    }

    return result;
}
