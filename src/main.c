#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h>
#include <unistd.h>

#include "log.h"
#include "mainloop.h"
#include "parser.h"

#ifndef TIMEOUT_TERM
#define TIMEOUT_TERM 3000
#endif

#ifndef TIMEOUT_ONE_SHOT
#define TIMEOUT_ONE_SHOT 3000
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

struct inittab_entry_list {
    struct inittab_entry_list *next;
    struct inittab_entry entry;
};

struct remaining_entries {
    struct inittab_entry_list *remaining;
    uint32_t pending_finish;
};

static struct remaining_entries remaining;

static struct inittab_entry safe_mode_entry;
static struct inittab_entry_list *startup_entries;
static struct inittab_entry_list *shutdown_entries;

static struct process *running_processes;

static enum stage current_stage;

static struct mainloop_timeout *kill_timeout;
static struct mainloop_timeout *one_shot_timeout;

static void
usage(const char *invocation_name)
{
    log_message("Usage: \n"
            " %s <inittab-file>\n"
            "\n"
            "System and service manager [early development]\n",
            invocation_name);
}

static bool
add_entry_to_list(struct inittab_entry_list **list, struct inittab_entry *entry)
{
    struct inittab_entry_list *current;
    struct inittab_entry_list *list_item;
    bool result = true;

    assert(list != NULL);
    assert(entry != NULL);

    list_item = calloc(1, sizeof(struct inittab_entry_list));
    if (list_item == NULL) {
        result = false;
        goto end;
    }

    memcpy(&list_item->entry, entry, sizeof(struct inittab_entry));

    /* Add item to list ensuring item->config.order is respected,
     * and keeping list stable - or, if a new item with same order
     * is added, it is kept after all previous items */
    if ((*list == NULL) || ((*list)->entry.order > entry->order)) {
        list_item->next = *list;
        *list = list_item;
    } else {
        current = *list;
        while ((current->next != NULL) && (current->next->entry.order <= entry->order)) {
            current = current->next;
        }
        list_item->next = current->next;
        current->next = list_item;
    }

end:
    return result;
}

static void
debug_entries_lists(void)
{
    struct inittab_entry_list *entry;

    log_message("STARTUP LIST:\n");
    entry = startup_entries;
    while (entry != NULL) {
        log_message("\t[Entry] order: %d, core_id: %d, type: %d, process: '%s'\n",
                entry->entry.order, entry->entry.core_id, entry->entry.type, entry->entry.process_name);
        entry = entry->next;
    }

    log_message("SHUTDOWN LIST:\n");
    entry = shutdown_entries;
    while (entry != NULL) {
        log_message("\t[Entry] order: %d, core_id: %d, type: %d, process: '%s'\n",
                entry->entry.order, entry->entry.core_id, entry->entry.type, entry->entry.process_name);
        entry = entry->next;
    }

    log_message("SAFE MODE:\n");
    log_message("\t[Entry] order: %d, core_id: %d, type: %d, entry: '%s'\n",
            safe_mode_entry.order, safe_mode_entry.core_id,
            safe_mode_entry.type, safe_mode_entry.process_name);
}

static void
free_entries_lists(void)
{
    struct inittab_entry_list *tmp;

    while (startup_entries != NULL) {
        tmp = startup_entries;
        startup_entries = startup_entries->next;
        free(tmp);
    }

    while (shutdown_entries != NULL) {
        tmp = shutdown_entries;
        shutdown_entries = shutdown_entries->next;
        free(tmp);
    }
}

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

static bool
handle_entry(struct inittab_entry *entry)
{
    bool result = true;

    assert(entry != NULL);

    switch (entry->type) {
        case ONE_SHOT:
        case SAFE_ONE_SHOT:
        case SERVICE:
        case SAFE_SERVICE: {
            add_entry_to_list(&startup_entries, entry);
            break;
        }
        case SHUTDOWN:
        case SAFE_SHUTDOWN: {
            add_entry_to_list(&shutdown_entries, entry);
            break;
        }
        case SAFE_MODE: {
            log_message(">>>>> [%s]\n", safe_mode_entry.process_name);
            if (safe_mode_entry.process_name[0] != '\0') {
                log_message("Safe process already defined before '%.20s'(...)\n", entry->process_name);
                result = false;
                break;
            }
            memcpy(&safe_mode_entry, entry, sizeof(struct inittab_entry));
            break;
        }
        default: {
            /* Should never happen */
            assert(false);
        }
    }

    return result;
}

static int
run_exec(const char *command)
{
    /* should run the new process using a bash ? */
//    int r;

    /* Give a controlle terminal for the process */
//    r = ioctl(STDIN_FILENO, TIOCSCTTY, 1);
//    if (r == -1) {
//        return r;
//    }

    /* Should configure ENV ? */
    return execl("/bin/sh", "/bin/sh", "-c", command, NULL);
}

static void
setup_signals(sigset_t *mask)
{
    int i, r;

    int signals[] = {
        SIGCHLD,
        SIGINT
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
        /* Restore the signals */
//        r = sigprocmask(SIG_SETMASK, &old_mask, NULL);
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
//    r = setup_stty(console);
//    if (r == -1) {
//        return -1;
//    }

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
start_processes(struct inittab_entry_list *list)
{
    int32_t current_order;
    bool result = true;

    if (list != NULL) {
        struct inittab_entry_list *list_item;

        remaining.pending_finish = 0;
        current_order = list->entry.order;
        list_item = list;
        while ((list_item != NULL) && (list_item->entry.order == current_order)) {
            struct process *p;

            /* First, let's see if we have memory for anciliary struct */
            p = calloc(1, sizeof(struct process));
            if (p == NULL) {
                result = false;
                break;
            }
            p->pid = spawn_exec(list_item->entry.process_name, "");

            if (p->pid > 0) {
                /* Stores inittab entry information on process struct */
                memcpy(&p->config, &(list_item->entry), sizeof(struct inittab_entry));

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

            list_item = list_item->next;
        }

        remaining.remaining = list_item;
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

static bool
read_inittab(const char *filename)
{
    FILE *fp = NULL;
    struct inittab_entry entry = {0};
    enum inittab_parse_result r;
    bool error = false, result = true;

    assert(filename != NULL);

    errno = 0;
    fp = fopen(filename, "re");
    if (fp == NULL) {
        log_message("Couldn't open inittab file: %m\n");
        result = false;
        goto end;
    }

    log_message("Reading inittab entries...\n");
    while (true) {
        bool exit_loop = false;

        r = inittab_parse_entry(fp, &entry);
        if (r == RESULT_OK) {
            log_message("[Entry] order: %d, core_id: %d, type: %d, process: '%s'\n",
                    entry.order, entry.core_id, entry.type, entry.process_name);

            if (!handle_entry(&entry)) {
                error = true;
                exit_loop = true;
            }
        } else if (r == RESULT_ERROR) {
            error = true;
            /* TODO currently, `inittab_parse_entry` itself prints error. Maybe it's
             * better if it returned (via a pointer arg) information about the error,
             * so caller print it */
        } else {
            exit_loop = true;
        }

        if (exit_loop) {
            break;
        }
    }

    if (safe_mode_entry.process_name[0] == '\0') {
        log_message("No <safe-mode> entry on inittab. Can't go on!\n");
        error = true;
        /* TODO is this the right approach? */
    }

    if (error) {
        log_message("Error(s) during inittab parsing. Exiting!\n");
        result = false;
        free_entries_lists();

        goto cleanup;
    }

cleanup:
    errno = 0;
    if (fclose(fp) != 0) {
        log_message("Error closing inittab file: %m\n");
    }

end:
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
            start_processes(shutdown_entries);

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
handle_sigint(struct signalfd_siginfo *info)
{
    /* Ensure 'remaining list' is cleaned up */
    remaining.remaining = NULL;
    remaining.pending_finish = 0;

    /* We wait for all running process to exit before starting shutdown ones */
    /* TODO is this right? */
    current_stage = STAGE_TERMINATION;
    term_running_process();
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
    log_message("ssi_signo: %d - ssi_code: %d - ssi_pid: %d - ssi_status %d\n",
            info->ssi_signo, info->ssi_code, info->ssi_pid, info->ssi_status);

    switch (info->ssi_signo) {
    case SIGCHLD:
        handle_child_exit(info);
        break;
    case SIGINT:
        handle_sigint(info);
        break;
    default:
        break;
    }
}

int
main(int argc, char *argv[])
{
    sigset_t mask;
    struct mainloop_signal_handler *msh = NULL;
    int result = EXIT_SUCCESS;

    current_stage = STAGE_SETUP;

    /* TODO this arg stuff will change once init starts as PID 1 */
    if (argc != 2) {
        usage(argv[0]);
        goto end;
    }

    if (!read_inittab(argv[1])) {
        result = EXIT_FAILURE;
        goto end;
    }

    debug_entries_lists();

    /* Block signals that should only be caught by epoll */
    setup_signals(&mask);

    /* Prepares mainloop to run */
    if (!mainloop_setup()) {
        goto end;
    }

    /* Sets handler to be run after each iteration. This handler
     * will track init state machine  */
    mainloop_set_post_iteration_callback(stage_maintenance);

    msh = mainloop_add_signal_handler(&mask, signal_handler);
    if (msh == NULL) {
        goto end;
    }

    /* Start initial list of process */
    current_stage = STAGE_STARTUP;
    start_processes(startup_entries);

    mainloop_start();

end:

    free_entries_lists();
    free_process_list(&running_processes);

    if (msh != NULL) {
        mainloop_remove_signal_handler(msh);
    }

    return result;
}
