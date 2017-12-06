/*
 * Copyright (C) 2017 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>

#include <macros.h>

/* This test is a white-box one to test mount.c function parse_fstab_mnt_options,
 * hence this include of C file */
#include <mount.c>

struct test_data {
    const char *name;
    const char *mnt_options;
    struct {
        const char *remaining;
        unsigned long flags;
        bool success;
    } expected;
} test_data[] = {
    {
        .name = "test1",
        .mnt_options = "defaults",
        .expected = {
            .remaining = NULL,
            .flags = MS_NOUSER,
            .success = true
        }
    },
    {
        .name = "test2",
        .mnt_options = "ro,fdata=dummy",
        .expected = {
            .remaining = "fdata=dummy",
            .flags = MS_RDONLY,
            .success = true
        }
    },
    {
        .name = "test3",
        .mnt_options = "rw,fdata=dummy,sync,fdata2=dummy2",
        .expected = {
            .remaining = "fdata=dummy,fdata2=dummy2",
            .flags = MS_SYNCHRONOUS,
            .success = true
        }
    },
    {
        .name = "test4",
        .mnt_options = "rw,fdata=dummy,sync,fdata2=dummy2,noatime",
        .expected = {
            .remaining = "fdata=dummy,fdata2=dummy2",
            .flags = MS_SYNCHRONOUS | MS_NOATIME,
            .success = true
        }
    },
    {
        .name = "test5",
        .mnt_options = "defaults,rw,fdata=dummy,sync,fdata2=dummy2",
        .expected = {
            .remaining = "fdata=dummy,fdata2=dummy2",
            .flags = MS_NOUSER | MS_SYNCHRONOUS,
            .success = true
        }
    },
    {
        .name = "test6",
        .mnt_options = "",
        .expected = {
            .remaining = NULL,
            .flags = 0,
            .success = false
        }
    },
    {
        .name = "test7",
        .mnt_options = "rw",
        .expected = {
            .remaining = NULL,
            .flags = 0,
            .success = true
        }
    },
    {
        .name = "test8",
        .mnt_options = "rw,noiversion,nofail",
        .expected = {
            .remaining = NULL,
            .flags = 0,
            .success = true
        }
    },
};

static bool not_equal(const char *a, const char *b)
{
    if (a == b) {
        return false;
    }

    if (a != NULL && b != NULL) {
        return strcmp(a, b) != 0;
    }

    return true;
}

static bool perform_test(struct test_data *td)
{
    assert(td);

    bool b, result = true;
    unsigned long flags;
    char *remaining = NULL;

    b = parse_fstab_mnt_options(td->mnt_options, &flags, &remaining);

    if (b != td->expected.success) {
        printf("TEST fstab (%s): Unexpected result for `parse_fstab_mnt_options`: %d Expected result %d\n",
                    td->name, b, td->expected.success);
        result = false;
    } else if (td->expected.success) {
        if (td->expected.flags != flags) {
            printf("TEST fstab (%s): Unexpected flags %ld Expected %ld\n",
                    td->name, flags, td->expected.flags);
        result = false;
        }
        if (not_equal(td->expected.remaining, remaining)) {
            printf("TEST fstab (%s): Unexpected remaining options [%s] Expected [%s]\n",
                    td->name, remaining, td->expected.remaining);
        result = false;
        }
    }

    free(remaining);

    return result;
}

int main(void)
{
    int i;
    bool success = true;

    for (i = 0; i < ARRAY_SIZE(test_data); i++) {
        success &= perform_test(&test_data[i]);
    }

    if (success) {
        printf("All tests OK\n");
    } else {
        printf("Some tests FAIL\n");
    }

    return success ? 0 : 1;
}
