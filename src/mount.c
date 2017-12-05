/*
 * Copyright (C) 2017 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#include "mount.h"

#include <assert.h>
#include <errno.h>
#include <mntent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>

#include "lexer.h"
#include "log.h"

static const struct mount_table {
	const char *source;
	const char *target;
	const char *fstype;
	const char *options;
	unsigned long flags;
	bool fatal;
} mount_table[] = {
    {NULL, "/sys", "sysfs", NULL, MS_NOSUID | MS_NOEXEC | MS_NODEV, true},
    {NULL, "/proc", "proc", NULL, MS_NOSUID | MS_NOEXEC | MS_NODEV, true},
    {NULL, "/dev", "devtmpfs", "mode=0755", MS_NOSUID | MS_STRICTATIME, true},
    {NULL, "/dev/pts", "devpts", "mode=0620", MS_NOSUID | MS_NOEXEC, true},
    {NULL, "/dev/shm", "tmpfs", "mode=1777",
     MS_NOSUID | MS_NODEV | MS_STRICTATIME, true},
    {NULL, "/run", "tmpfs", "mode=0755", MS_NOSUID | MS_NODEV | MS_STRICTATIME,
     true},
    {NULL, "/tmp", "tmpfs", NULL, 0, true},
    {NULL, "/sys/kernel/debug", "debugfs", NULL, 0, false},
    {NULL, "/sys/kernel/security", "securityfs", NULL,
     MS_NOSUID | MS_NOEXEC | MS_NODEV, false},
#ifdef COMPILING_COVERAGE
    {"/dev/sdb", "/gcov", "ext4", NULL, 0, true},
#endif
};

struct mount_point {
	struct mount_point *next;
	char *path;
};

static bool add_option_flag(char *opt, unsigned long *flags)
{
	static const struct {
		const char *name;
		unsigned long flag;
		bool negated;
	} options[] = {
	    /* Should equal "rw,suid,dev,exec,auto,nouser,async" */
	    {"defaults", MS_NOUSER, false},

	    /* Handled options with a no* version (like dev and nodev) */
	    {"ro", MS_RDONLY, false},
	    {"rw", ~MS_RDONLY, true},
	    {"noexec", MS_NOEXEC, false},
	    {"exec", ~MS_NOEXEC, true},
	    {"nodev", MS_NODEV, false},
	    {"dev", ~MS_NODEV, true},
	    {"nouser", MS_NOUSER, false},
	    {"user", ~MS_NOUSER, true},
	    {"relatime", MS_RELATIME, false},
	    {"norelatime", ~MS_RELATIME, true},
	    {"sync", MS_SYNCHRONOUS, false},
	    {"async", ~MS_SYNCHRONOUS, true},
	    {"silent", MS_SILENT, false},
	    {"loud", ~MS_SILENT, true},
	    {"noatime", MS_NOATIME, false},
	    {"atime", ~MS_NOATIME, true},
	    {"strictatime", MS_STRICTATIME, false},
	    {"nostrictatime", ~MS_STRICTATIME, true},
	    {"nosuid", MS_NOSUID, false},
	    {"suid", ~MS_NOSUID, true},
	    {"nodiratime", MS_NODIRATIME, false},
	    {"diratime", ~MS_NODIRATIME, true},
	    {"iversion", MS_I_VERSION, false},
	    {"noiversion", ~MS_I_VERSION, true},
	    {"mand", MS_MANDLOCK, false},
	    {"nomand", ~MS_MANDLOCK, true},

	    /* Options without negative counterparts */
	    {"dirsync", MS_DIRSYNC, false},
	    {"remount", MS_REMOUNT, false},

	    /* Just to filter them out from mount(2) */
	    {"nofail", 0, false},
	};
	int i;
	bool result = false;

	for (i = 0; i < ARRAY_SIZE(options); i++) {
		if (strcmp(options[i].name, opt) == 0) {
			if (options[i].negated) {
				*flags &= options[i].flag;
			} else {
				*flags |= options[i].flag;
			}
			result = true;
			break;
		}
	}

	return result;
}

static bool add_uknown_option(char **unknow_opts, char *option)
{
	char *tmp = NULL;
	bool result = false;

	if (*unknow_opts == NULL) {
		tmp = strdup(option);
		result = tmp == NULL ? false : true;
	} else {
		/* TODO: check if we could go GNU_SOURCE and use asprintf */
		/* +2 below: one for `,` and one for `\0` */
		tmp = calloc(1, strlen(*unknow_opts) + strlen(option) + 2);
		if (tmp != NULL) {
			/* sprintf is fine as we know size of final string and
			 * allocated for that */
			int r = sprintf(tmp, "%s,%s", *unknow_opts, option);
			result = r < 0 ? false : true;
		}
	}

	if (result) {
		free(*unknow_opts);
		*unknow_opts = tmp;
	}

	return result;
}

static bool parse_fstab_mnt_options(const char *mnt_options,
				    unsigned long *flags, char **unknow_opts)
{
	struct lexer_data lexer;
	enum token_result tr;
	char *dup_options = NULL;

	assert(unknow_opts);
	assert(flags);

	*unknow_opts = NULL;

	*flags = 0;

	/* fstab(5) implies that options can't be empty */
	if (mnt_options[0] == '\0') {
		log_message(
		    "Could not parse fstab: missing mount options field\n");
		goto err;
	}

	/* As lexer destroys argument, send it a copy */
	dup_options = strdup(mnt_options);
	if (dup_options == NULL) {
		log_message("Could not parse fstab: %m\n");
		goto err;
	}

	init_lexer(&lexer, dup_options, strlen(dup_options) + 1);

	while (true) {
		char *opt = NULL;

		tr = next_token(&lexer, &opt, ',', true);
		if (tr == TOKEN_END) {
			/* Ok, no more tokens */
			break;
		} else if (tr == TOKEN_OK) {
			if (!add_option_flag(opt, flags)) {
				/* This option is unknown. Let's add to the ones
				 * sent to fs */
				if (!add_uknown_option(unknow_opts, opt)) {
					log_message(
					    "Could not parse fstab: %m\n");
					goto err;
				}
			}
		} else if (tr == TOKEN_UNFINISHED_QUOTE) {
			log_message(
			    "Could not parse fstab: unfinished quote at '%s'\n",
			    opt);
			goto err;
		} else {
			/* TOKEN_BLANK, just ignore it */
		}
	}

	free(dup_options);
	return true;

err:
	free(*unknow_opts);
	free(dup_options);

	*unknow_opts = NULL;

	return false;
}

static bool mount_system_filesystems(void)
{
	bool result = true;
	const struct mount_table *mnt;

	for (mnt = mount_table; mnt < mount_table + ARRAY_SIZE(mount_table);
	     mnt++) {
		const char *source =
		    (mnt->source != NULL) ? mnt->source : "none";
		int err;

		err = mkdir(mnt->target,
			    S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
		if (err < 0) {
			if ((errno != EEXIST) && mnt->fatal) {
				log_message("could not mkdir '%s': %m\n",
					    mnt->target);
				result = false;
				goto end;
			}
		}

		log_message("mounting '%s' from '%s' to '%s', options=%s\n",
			    mnt->fstype, source, mnt->target,
			    (mnt->options != NULL) ? mnt->options : "(none)");
		err = mount(source, mnt->target, mnt->fstype, mnt->flags,
			    mnt->options);
		if (err < 0) {
			if (errno == EBUSY || !mnt->fatal) {
				log_message("could not mount '%s' from '%s' to "
					    "'%s', options=%s: %m\n",
					    mnt->fstype, source, mnt->target,
					    (mnt->options != NULL)
						? mnt->options
						: "(none)");
			} else {
				log_message("could not mount '%s' from '%s' to "
					    "'%s', options=%s: %m\n",
					    mnt->fstype, source, mnt->target,
					    (mnt->options != NULL)
						? mnt->options
						: "(none)");
				result = false;
				goto end;
			}
		}
	}

end:
	return result;
}

static bool mount_fstab_filesystems(void)
{
	bool result = true;
	struct mntent *ent;
	char *unknow_opts = NULL;
	FILE *fstab;

	fstab = setmntent("/etc/fstab", "re");
	if (fstab == NULL) {
		log_message("Could not open fstab file. No user filesystem "
			    "will be mounted!\n");
		/* This is not necessarily a problem, hence no `result = false`
		 */
		goto end;
	}

	while (true) {
		unsigned long flags = 0;
		bool nofail = false;
		int err;

		errno = 0;
		ent = getmntent(fstab);

		if (!ent) {
			/* No more entries. Just leave. */
			break;
		}

		/* TODO should we care about swap partitions? */

		if (hasmntopt(ent, "noauto")) {
			/* We shall not mount it */
			continue;
		}

		if (hasmntopt(ent, "nofail")) {
			nofail = true;
		}

		if (!parse_fstab_mnt_options(ent->mnt_opts, &flags,
					     &unknow_opts)) {
			result = false;
			goto err;
		}

		err = mkdir(ent->mnt_dir,
			    S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
		if (err < 0) {
			if ((errno != EEXIST)) {
				log_message("Could not mkdir '%s': %m\n",
					    ent->mnt_dir);
				if (nofail) {
					free(unknow_opts);
					unknow_opts = NULL;
					continue;
				}

				result = false;
				goto err;
			}
		}

		log_message("Mounting '%s' from '%s' to "
			    "'%s', options='%s'\n",
			    ent->mnt_type, ent->mnt_fsname, ent->mnt_dir,
			    (ent->mnt_opts[0] != '\0') ? ent->mnt_opts
						       : "(none)");
		log_message("Parsed flags: %ld\nRemaining options: '%s'\n",
			    flags, unknow_opts);
		err = mount(ent->mnt_fsname, ent->mnt_dir, ent->mnt_type, flags,
			    unknow_opts);
		if (err < 0) {
			log_message("Could not mount '%s' from '%s' to "
				    "'%s', options='%s': %m\n",
				    ent->mnt_type, ent->mnt_fsname,
				    ent->mnt_dir,
				    (ent->mnt_opts[0] != '\0') ? ent->mnt_opts
							       : "(none)");

			/* TODO check if nofail is ok for every fail reason */
			if (nofail) {
				free(unknow_opts);
				unknow_opts = NULL;
				continue;
			}

			result = false;
			goto err;
		}

		free(unknow_opts);
		unknow_opts = NULL;
	}

err:
	free(unknow_opts);
	(void)endmntent(fstab);
end:
	return result;
}

bool mount_mount_filesystems(void)
{
	bool result = true;

	if (!mount_system_filesystems() || !mount_fstab_filesystems()) {
		result = false;
	}

	return result;
}

static struct mount_point *get_mountpoints(void)
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
			   "%*s " /* mount id */
			   "%*s " /* parent id*/
			   "%*s " /* major: minor*/
			   "%*s " /* root */
			   "%ms " /* mount point path. This is what we want */
			   "%*[^\n]", /* Discard everything else */
			   &path);
		if (r != 1) {
			if (r == EOF) {
				break;
			}
			continue;
		}

		/* No need to umount these 'system' mountpoints */
		for (mnt = mount_table;
		     mnt < (mount_table + ARRAY_SIZE(mount_table)); mnt++) {
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

static void remove_mountpoint(struct mount_point **list, struct mount_point *mp)
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

void mount_umount_filesystems(void)
{
	struct mount_point *mp, *mp_list;
	bool changed;

	mp_list = get_mountpoints();
	changed = false;
	do {
		/* We keep umounting filesystems as long as we can. Some
		 * filesystems maybe mounted on top of others, so the 'bottom'
		 * ones will only umount when there's nothing left on top of
		 * them. After one iteration without being able to umount
		 * anything, we give up. */
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
				log_message("Could not umount: %s: %m\n",
					    mp->path);
			}
			mp = next;
		}
	} while (changed);

	while (mp_list != NULL) {
		remove_mountpoint(&mp_list, mp_list);
	}
}
