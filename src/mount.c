#include "mount.h"

#include <assert.h>
#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "log.h"

static const struct mount_table {
    const char *source;
    const char *target;
    const char *fstype;
    const char *options;
    unsigned long flags;
    bool fatal;
} mount_table[] = {
    { NULL, "/sys", "sysfs", NULL, MS_NOSUID | MS_NOEXEC | MS_NODEV, true },
    { NULL, "/proc", "proc", NULL, MS_NOSUID | MS_NOEXEC | MS_NODEV, true },
    { NULL, "/dev", "devtmpfs", "mode=0755", MS_NOSUID | MS_STRICTATIME, true },
    { NULL, "/dev/pts", "devpts", "mode=0620", MS_NOSUID | MS_NOEXEC, true },
    { NULL, "/dev/shm", "tmpfs", "mode=1777", MS_NOSUID | MS_NODEV | MS_STRICTATIME, true },
    { NULL, "/run", "tmpfs", "mode=0755", MS_NOSUID | MS_NODEV | MS_STRICTATIME, true },
    { NULL, "/tmp", "tmpfs", NULL, 0, true },
    { NULL, "/sys/kernel/debug", "debugfs", NULL, 0, false },
    { NULL, "/sys/kernel/security", "securityfs", NULL, MS_NOSUID | MS_NOEXEC | MS_NODEV, false },
#ifdef COMPILING_COVERAGE
    { "/dev/sdb", "/gcov", "ext4", NULL, 0, true },
#endif
};

struct mount_point {
    struct mount_point *next;
    char *path;
};

bool
mount_mount_filesystems(void)
{
    const struct mount_table *mnt;
    bool result = true;

    for (mnt = mount_table;
            mnt < mount_table + ARRAY_SIZE(mount_table);
            mnt++) {
        const char *source = (mnt->source != NULL) ? mnt->source : "none";
        int err;

        err = mkdir(mnt->target, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
        if (err < 0) {
            if ((errno != EEXIST) && mnt->fatal) {
                log_message("could not mkdir '%s': %m\n", mnt->target);
                result = false;
                goto end;
            }
        }

        log_message("mounting '%s' from '%s' to '%s', options=%s\n",
                mnt->fstype, source, mnt->target,
                (mnt->options != NULL) ? mnt->options : "(none)");
        err = mount(source, mnt->target, mnt->fstype, mnt->flags, mnt->options);
        if (err < 0) {
            if (errno == EBUSY || !mnt->fatal) {
                log_message("could not mount '%s' from '%s' to '%s', options=%s: %m\n",
                        mnt->fstype, source, mnt->target,
                        (mnt->options != NULL) ? mnt->options : "(none)");
            } else {
                log_message("could not mount '%s' from '%s' to '%s', options=%s: %m\n",
                        mnt->fstype, source, mnt->target,
                        (mnt->options != NULL) ? mnt->options : "(none)");
                result = false;
                goto end;
            }
        }
    }

end:
    return result;
}

static struct mount_point *
get_mountpoints(void)
{
    FILE *mounts_file;
    struct mount_point *list = NULL;

    mounts_file = fopen("/proc/self/mountinfo", "re");
    if (mounts_file == NULL) {
        log_message("Could not open mountinfo file: %m\n");
        goto error_open;
    }

    while (true) {
        const struct mount_table *mnt;
        struct mount_point *entry;
        bool should_umount = true;
        char *path;
        int r;

        r = fscanf(mounts_file,
                "%*s "      /* mount id */
                "%*s "      /* parent id*/
                "%*s "      /* major: minor*/
                "%*s "      /* root */
                "%ms "      /* mount point path. This is what we want */
                "%*[^\n]",  /* Discard everything else */
                &path);
        if (r != 1) {
            if (r == EOF) {
                break;
            }
            continue;
        }

        /* No need to umount these 'system' mountpoints */
        for (mnt = mount_table;
                mnt < (mount_table + ARRAY_SIZE(mount_table));
                mnt++) {
            if (strcmp(path, mnt->target) == 0) {
                should_umount = false;
                break;
            }
        }

        if (!should_umount) {
            free(path);
            continue;
        }

        errno = 0;
        entry = calloc(1, sizeof(struct mount_point));
        if (entry == NULL) {
            log_message("Could not create mount point entry: %m\n");
            free(path);
            goto error_entry;
        }

        entry->path = path;
        entry->next = list;
        list = entry;
    }

error_entry:
    fclose(mounts_file);
error_open:
    return list;
}

static void
remove_mountpoint(struct mount_point **list, struct mount_point *mp)
{
    assert(mp);
    assert(list);

    if (*list == mp) {
        *list = mp->next;
    } else {
        struct mount_point *current = *list;

        while (current != NULL) {
            if (current->next == mp) {
                current->next = mp->next;
                break;
            }
            current = current->next;
        }
    }

    free(mp->path);
    free(mp);
}

void
mount_umount_filesystems(void)
{
    struct mount_point *mp, *mp_list;
    bool changed;

    mp_list = get_mountpoints();
    changed = false;
    do {
        /* We keep umounting filesystems as long as we can. Some filesystems
         * maybe mounted on top of others, so the 'bottom' ones will only umount
         * when there's nothing left on top of them. After one iteration without
         * being able to umount anything, we give up. */
        mp = mp_list;
        changed = false;
        while (mp != NULL) {
            int err;
            struct mount_point *next = mp->next;

            log_message("Umounting %s\n", mp->path);

            errno = 0;
            err = umount(mp->path);
            if (err == 0) {
                log_message("Umounted %s\n", mp->path);
                changed = true;
                remove_mountpoint(&mp_list, mp);
            } else {
                log_message("Could not umount: %s: %m\n", mp->path);
            }
            mp = next;
        }
    } while (changed);

    while (mp_list != NULL) {
        remove_mountpoint(&mp_list, mp_list);
    }
}
