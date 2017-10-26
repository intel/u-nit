#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include "log.h"
#include "mainloop.h"

#define MAX_EVENTS 8

enum callback_type {CALLBACK_SIGNAL, CALLBACK_TIMEOUT};

struct callback_data {
    int fd;
    enum callback_type type;
};

struct mainloop_signal_handler {
    struct callback_data cb_data;
    void (*callback) (struct signalfd_siginfo *info);
};

struct mainloop_timeout {
    struct callback_data cb_data;
    enum timeout_result (*callback) (void);
};

static int epollfd = -1;
static bool should_exit = true;
static void (*post_iteration_callback)(void);

static bool
add_fd(int fd, struct callback_data *data)
{
    bool result = true;
    struct epoll_event epev = { };

    assert(epollfd > -1);
    assert(fd > -1);

    epev.events = EPOLLIN;
    epev.data.ptr = data;

    log_message("Adding %d to %d epoll\n", fd, epollfd);

    if (epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &epev) < 0) {
        log_message("Error adding file descriptor to epoll: %m\n");
        result = false;
    }

    return result;
}

static void
remove_fd(int fd)
{
    if ((fd > -1) && (epollfd > -1)) {
        log_message("Removing %d from %d epoll\n", fd, epollfd);

        errno = 0;
        if (epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, NULL) < 0) {
            log_message("Could not remove file descriptor from epoll: %m\n");
        }
    }
}

bool
mainloop_setup(void)
{
    bool result = true;

    /* We should not have set up before */
    assert(epollfd == -1);

    epollfd = epoll_create1(EPOLL_CLOEXEC);
    if (epollfd == -1) {
        log_message("Error creating epoll fd: %m\n");
        result = false;
    }

    return result;
}

void
mainloop_exit(void)
{
    assert(!should_exit);

    should_exit = true;
}

void
mainloop_set_post_iteration_callback(void (*cb)(void))
{
    post_iteration_callback = cb;
}

bool
mainloop_start(void)
{
    assert(epollfd != -1);
    assert(should_exit);

    should_exit = false;
    while (!should_exit) {
        int i, r;
        ssize_t s;
        struct epoll_event events[MAX_EVENTS] = { };

        r = epoll_wait(epollfd, events, MAX_EVENTS, -1);
        if ((r < 0) && (errno == EINTR)) {
            continue;
        }

        if (r < 0) {
            log_message("epoll_wait error: %m\n");
            assert(false); /* Anything other than EINTR should not happen */
        }

        for (i = 0; i < r; i++) {
            struct callback_data *cb_data = events[i].data.ptr;

            errno = 0;

            switch (cb_data->type) {
                case CALLBACK_SIGNAL: {
                    struct signalfd_siginfo info;

                    s = read(cb_data->fd, &info, sizeof(struct signalfd_siginfo));
                    if (s != (ssize_t)sizeof(struct signalfd_siginfo)) {
                        log_message("A Error reading signal: %m\n");
                        goto error_reading;
                    }
                    ((struct mainloop_signal_handler*)cb_data)->callback(&info);
                    break;
                }
                case CALLBACK_TIMEOUT: {
                    uint64_t u;

                    s = read(cb_data->fd, &u, sizeof(uint64_t));
                    if (s != (ssize_t)sizeof(uint64_t)) {
                        log_message("Error reading timeout: %m\n");
                        goto error_reading;
                    }
                    if (((struct mainloop_timeout *)cb_data)->callback() != TIMEOUT_CONTINUE) {
                        mainloop_remove_timeout((struct mainloop_timeout *)cb_data);
                    }
                    break;
                }
                default: {
                    log_message("Unexpected callback type %d\n", cb_data->type);
                    break;
                }
            }

            if (post_iteration_callback != NULL) {
                post_iteration_callback();
            }
        }
    }

    close(epollfd);
    epollfd = -1;

    return true;

error_reading:
    close(epollfd);

    return false;
}

struct mainloop_timeout *
mainloop_add_timeout(uint32_t msec, enum timeout_result (*timeout_cb)(void))
{
    int timerfd, r;
    struct itimerspec ts = { };
    struct mainloop_timeout *mt = NULL;

    assert(msec != 0U);
    assert(timeout_cb != NULL);
    assert(epollfd != -1);

    errno = 0;
    mt = calloc(1, sizeof(struct mainloop_timeout));
    if (mt == NULL) {
        log_message("Could not add timeout: %m\n");
        goto alloc_error;
    }

    mt->cb_data.type = CALLBACK_TIMEOUT;
    mt->callback = timeout_cb;

    timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    if (timerfd < 0) {
        log_message("Could not add timeout: %m\n");
        goto timerfd_error;
    }
    mt->cb_data.fd = timerfd;

    ts.it_value.tv_sec = (time_t)msec / 1000;
    ts.it_value.tv_nsec = ((long)msec % 1000) * 1000000;
    ts.it_interval.tv_sec = ts.it_value.tv_sec;
    ts.it_interval.tv_nsec = ts.it_value.tv_nsec;
    r = timerfd_settime(timerfd, 0, &ts, NULL);
    assert(r == 0);

    if (!add_fd(timerfd, &mt->cb_data)) {
        goto add_fd_error;
    }

    return mt;

add_fd_error:
    close(timerfd);
timerfd_error:
alloc_error:
    free(mt);

    return NULL;
}

void
mainloop_remove_timeout(struct mainloop_timeout *mt)
{
    assert(mt != NULL);

    remove_fd(mt->cb_data.fd);
    (void)close(mt->cb_data.fd);

    free(mt);
}

struct mainloop_signal_handler *
mainloop_add_signal_handler(sigset_t *mask, void (*signal_cb)(struct signalfd_siginfo *info))
{
    int sig_fd;
    struct mainloop_signal_handler *msh = NULL;

    assert(mask != NULL);
    assert(signal_cb != NULL);
    assert(epollfd != -1);

    errno = 0;
    msh = calloc(1, sizeof(struct mainloop_signal_handler));
    if (msh == NULL) {
        perror("Could not add signal handler");
        log_message("Could not add signal handler: %m\n");
        goto alloc_error;
    }

    msh->cb_data.type = CALLBACK_SIGNAL;
    msh->callback = signal_cb;

    sig_fd = signalfd(-1, mask, SFD_CLOEXEC | SFD_NONBLOCK);
    if (sig_fd < 0) {
        log_message("Could not add signal handler: %m\n");
        goto signalfd_error;
    }
    msh->cb_data.fd = sig_fd;

    if (!add_fd(sig_fd, &msh->cb_data)) {
        goto add_fd_error;
    }

    return msh;

add_fd_error:
    close(sig_fd);
signalfd_error:
alloc_error:
    free(msh);

    return NULL;
}

void
mainloop_remove_signal_handler(struct mainloop_signal_handler *msh)
{
    assert(msh != NULL);

    remove_fd(msh->cb_data.fd);
    (void)close(msh->cb_data.fd);

    free(msh);
}
