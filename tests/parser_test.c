/*
 * Copyright (C) 2017 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * This is a whitebox testing of an internal function of `inittab.c`
 * (inittab_parse_entry).
 * That's why we include it directly here. This whitebox test is useful
 * because allows a more granular (line by line) checking of parser.
 */
#include <inittab.c>

struct test_data {
    const char *file_name;
    struct expected_data {
        enum inittab_parse_result result;
        struct inittab_entry entry;
    } expected_data[];
};

#define EXPECTED_END { .result = -1U, .entry = {} }

static struct test_data parse_assorted_errors = {
    .file_name = "tests/data/parser/parse_assorted_errors",
    .expected_data = {
        {
            .result = RESULT_OK,
            .entry = {
                .process_name = "/usr/bin/foo --bar baz",
                .ctty_path = "/dev/tty1",
                .type = ONE_SHOT,
                .order = 1,
                .core_id = 2
            }
        },
        {
            .result = RESULT_OK,
            .entry = {
                .process_name = "/usr/bin/bar --baz foo",
                .ctty_path = "/dev/console",
                .type = SAFE_ONE_SHOT,
                .order = 4,
                .core_id = 0
            }
        },
        {
            .result = RESULT_ERROR,
            .entry = { }
        },
        {
            .result = RESULT_ERROR,
            .entry = { }
        },
        {
            .result = RESULT_ERROR,
            .entry = { }
        },
        {
            .result = RESULT_OK,
            .entry = {
                .process_name = "/usr/bin/baz --bar foo",
                .type = SAFE_MODE,
                .order = -1,
                .core_id = -1
            }
        },
        {
            .result = RESULT_ERROR,
            .entry = { }
        },
        {
            .result = RESULT_ERROR,
            .entry = { }
        },
        {
            .result = RESULT_ERROR,
            .entry = { }
        },
        {
            .result = RESULT_ERROR,
            .entry = { }
        },
        {
            .result = RESULT_ERROR,
            .entry = { }
        },
        {
            .result = RESULT_ERROR,
            .entry = { }
        },
        {
            .result = RESULT_ERROR,
            .entry = { }
        },
        {
            .result = RESULT_DONE,
            .entry = { }
        },
        EXPECTED_END
    }
};

static struct test_data parse_ok = {
    .file_name = "tests/data/parser/parse_ok",
    .expected_data = {
        {
            .result = RESULT_OK,
            .entry = {
                .process_name = "/usr/bin/foo --bar baz",
                .ctty_path = "",
                .type = ONE_SHOT,
                .order = 1,
                .core_id = 2
            }
        },
        {
            .result = RESULT_OK,
            .entry = {
                .process_name = "/usr/bin/bar --baz foo",
                .ctty_path = "/dev/console",
                .type = SAFE_ONE_SHOT,
                .order = 2,
                .core_id = 0
            }
        },
        {
            .result = RESULT_OK,
            .entry = {
                .process_name = "/usr/bin/foo --baz bar",
                .type = SERVICE,
                .order = 1,
                .core_id = -1
            }
        },
        {
            .result = RESULT_OK,
            .entry = {
                .process_name = "/usr/bin/bar --foo baz",
                .type = SAFE_MODE,
                .order = -1,
                .core_id = -1
            }
        },
        {
            .result = RESULT_OK,
            .entry = {
                .process_name = "/usr/bin/baz --foo baz",
                .type = SHUTDOWN,
                .order = 1,
                .core_id = 2
            }
        },
        {
            .result = RESULT_OK,
            .entry = {
                .process_name = "/usr/bin/baz --bar foo",
                .type = SAFE_SHUTDOWN,
                .order = 0,
                .core_id = 1
            }
        },
        {
            .result = RESULT_OK,
            .entry = {
                .process_name = "/usr/bin/safe --wut wat",
                .type = SAFE_MODE,
                .order = 0,
                .core_id = 0
            }
        },
        {
            .result = RESULT_DONE,
            .entry = { }
        },
        EXPECTED_END
    }
};

static struct test_data parse_comment_too_big = {
    .file_name = "tests/data/parser/parse_comment_too_big",
    .expected_data = {
        {
            .result = RESULT_ERROR,
            .entry = { }
        },
        {
            .result = RESULT_DONE,
            .entry = { }
        },
        EXPECTED_END
    }
};

static struct test_data parse_line_too_big = {
    .file_name = "tests/data/parser/parse_line_too_big",
    .expected_data = {
        {
            .result = RESULT_OK,
            .entry = {
                .process_name = "/usr/bin/foo --bar baz",
                .type = ONE_SHOT,
                .order = 1,
                .core_id = 2
            }
        },
        {
            .result = RESULT_OK,
            .entry = {
                .process_name = "/usr/bin/bar --baz foo",
                .type = SAFE_MODE,
                .order = -1,
                .core_id = 0
            }
        },
        {
            .result = RESULT_ERROR,
            .entry = { }
        },
        {
            .result = RESULT_OK,
            .entry = {
                .process_name = "/usr/bin/bar --foo baz",
                .type = SAFE_SERVICE,
                .order = 5,
                .core_id = -1
            }
        },
        {
            .result = RESULT_OK,
            .entry = {
                .process_name = "/usr/bin/baz --foo baz",
                .type = SHUTDOWN,
                .order = 1,
                .core_id = 2
            }
        },
        {
            .result = RESULT_OK,
            .entry = {
                .process_name = "/usr/bin/baz --bar foo",
                .type = SAFE_SHUTDOWN,
                .order = 0,
                .core_id = 1
            }
        },
        {
            .result = RESULT_OK,
            .entry = {
                .process_name = "/usr/bin/safe --wut wat",
                .type = SAFE_MODE,
                .order = 0,
                .core_id = 0
            }
        },
        {
            .result = RESULT_DONE,
            .entry = { }
        },
        EXPECTED_END
    }
};

static struct test_data parse_empty = {
    .file_name = "tests/data/parser/parse_empty",
    .expected_data = {
        {
            .result = RESULT_DONE,
            .entry = { }
        },
        EXPECTED_END
    }
};

static bool
entry_equal(struct inittab_entry *a, struct inittab_entry *b)
{
    return (strncmp(a->process_name, b->process_name, sizeof(a->process_name)) == 0)
        && (strncmp(a->ctty_path, b->ctty_path, sizeof(a->ctty_path)) == 0)
        && (a->type == b->type)
        && (a->order == b->order)
        && (a->core_id == b->core_id);
}

static bool
perform_test(struct test_data *td)
{
    assert(td);

    int i = 0;
    bool result = true;
    FILE *fp = fopen(td->file_name, "re");
    assert(fp);

    while (td->expected_data[i].result != -1U) {
        struct inittab_entry entry = { };
        enum inittab_parse_result r = inittab_parse_entry(fp, &entry);

        if (r != td->expected_data[i].result) {
            printf("TEST %s: Unexpected return from `inittab_parse_entry`: %d for entry %d. Expected %d\n",
                    td->file_name, r, i, td->expected_data[i].result);
            result = false;
        } else if (td->expected_data[i].result == RESULT_OK) {
            if (!entry_equal(&td->expected_data[i].entry, &entry)) {
                printf("TEST %s: Mismatch for entry %d\n", td->file_name, i);
                result = false;
            }
        } else {
            /* OK */
        }

        i++;
    }

    return result;
}

int main(void)
{
    bool success = true;

    success &= perform_test(&parse_ok);
    success &= perform_test(&parse_assorted_errors);
    success &= perform_test(&parse_comment_too_big);
    success &= perform_test(&parse_line_too_big);
    success &= perform_test(&parse_empty);

    if (success) {
        printf("All tests OK\n");
    } else {
        printf("Some tests FAIL\n");
    }

    return success ? 0 : 1;
}
