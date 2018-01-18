/*
 * Copyright (C) 2017 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <lexer.h>

struct test_data {
    const char *name;
    const char *str;
    const char delim;
    bool quoted;
    bool remove_quotes;
    struct {
        const char* token;
        const enum token_result result;
    } expected[];
};

static struct test_data test1 =
{
    .name = "test1",
    .str = "The quick brown fox jumps over the lazy dog",
    .delim = ' ',
    .quoted = false,
    .remove_quotes = false,
    .expected = {
        {
            .token = "The",
            .result = TOKEN_OK
        },
        {
            .token = "quick",
            .result = TOKEN_OK
        },
        {
            .token = "brown",
            .result = TOKEN_OK
        },
        {
            .token = "fox",
            .result = TOKEN_OK
        },
        {
            .token = "jumps",
            .result = TOKEN_OK
        },
        {
            .token = "over",
            .result = TOKEN_OK
        },
        {
            .token = "the",
            .result = TOKEN_OK
        },
        {
            .token = "lazy",
            .result = TOKEN_OK
        },
        {
            .token = "dog",
            .result = TOKEN_OK
        },
        {
            .token = NULL,
            .result = TOKEN_END
        },
    }
};

static struct test_data test2 =
{
    .name = "test2",
    .str = "The quick brown fox jumps over the lazy dog",
    .delim = ' ',
    .quoted = true,
    .remove_quotes = false,
    .expected = {
        {
            .token = "The",
            .result = TOKEN_OK
        },
        {
            .token = "quick",
            .result = TOKEN_OK
        },
        {
            .token = "brown",
            .result = TOKEN_OK
        },
        {
            .token = "fox",
            .result = TOKEN_OK
        },
        {
            .token = "jumps",
            .result = TOKEN_OK
        },
        {
            .token = "over",
            .result = TOKEN_OK
        },
        {
            .token = "the",
            .result = TOKEN_OK
        },
        {
            .token = "lazy",
            .result = TOKEN_OK
        },
        {
            .token = "dog",
            .result = TOKEN_OK
        },
        {
            .token = NULL,
            .result = TOKEN_END
        },
    }
};

static struct test_data test3 =
{
    .name = "test3",
    .str = "The,quick,,brown,",
    .delim = ',',
    .quoted = false,
    .remove_quotes = false,
    .expected = {
        {
            .token = "The",
            .result = TOKEN_OK
        },
        {
            .token = "quick",
            .result = TOKEN_OK
        },
        {
            .token = NULL,
            .result = TOKEN_BLANK
        },
        {
            .token = "brown",
            .result = TOKEN_OK
        },
        {
            .token = NULL,
            .result = TOKEN_BLANK
        },
        {
            .token = NULL,
            .result = TOKEN_END
        },
    }
};

static struct test_data test4 =
{
    .name = "test4",
    .str = "The,quick,,brown,",
    .delim = ',',
    .quoted = true,
    .remove_quotes = false,
    .expected = {
        {
            .token = "The",
            .result = TOKEN_OK
        },
        {
            .token = "quick",
            .result = TOKEN_OK
        },
        {
            .token = NULL,
            .result = TOKEN_BLANK
        },
        {
            .token = "brown",
            .result = TOKEN_OK
        },
        {
            .token = NULL,
            .result = TOKEN_BLANK
        },
        {
            .token = NULL,
            .result = TOKEN_END
        },
    }
};

static struct test_data test5 =
{
    .name = "test5",
    .str = "The,quick\",,\"brown,fox,\"\"jumps,\"over,the\"",
    .delim = ',',
    .quoted = false,
    .remove_quotes = false,
    .expected = {
        {
            .token = "The",
            .result = TOKEN_OK
        },
        {
            .token = "quick\"",
            .result = TOKEN_OK
        },
        {
            .token = NULL,
            .result = TOKEN_BLANK
        },
        {
            .token = "\"brown",
            .result = TOKEN_OK
        },
        {
            .token = "fox",
            .result = TOKEN_OK
        },
        {
            .token = "\"\"jumps",
            .result = TOKEN_OK
        },
        {
            .token = "\"over",
            .result = TOKEN_OK
        },
        {
            .token = "the\"",
            .result = TOKEN_OK
        },
        {
            .token = NULL,
            .result = TOKEN_END
        },
    }
};

static struct test_data test6 =
{
    .name = "test6",
    .str = "The,quick\",,\"brown,fox,\"\"jumps,\"over,the\"",
    .delim = ',',
    .quoted = true,
    .remove_quotes = false,
    .expected = {
        {
            .token = "The",
            .result = TOKEN_OK
        },
        {
            .token = "quick\",,\"brown",
            .result = TOKEN_OK
        },
        {
            .token = "fox",
            .result = TOKEN_OK
        },
        {
            .token = "\"\"jumps",
            .result = TOKEN_OK
        },
        {
            .token = "\"over,the\"",
            .result = TOKEN_OK
        },
        {
            .token = NULL,
            .result = TOKEN_END
        },
    }
};

static struct test_data test7 =
{
    .name = "test7",
    .str = "",
    .delim = ',',
    .quoted = false,
    .remove_quotes = false,
    .expected = {
        {
            .token = NULL,
            .result = TOKEN_BLANK
        },
        {
            .token = NULL,
            .result = TOKEN_END
        },
    }
};

static struct test_data test8 =
{
    .name = "test8",
    .str = "",
    .delim = ',',
    .quoted = true,
    .remove_quotes = false,
    .expected = {
        {
            .token = NULL,
            .result = TOKEN_BLANK
        },
        {
            .token = NULL,
            .result = TOKEN_END
        },
    }
};

static struct test_data test9 =
{
    .name = "test9",
    .str = "The,\"quick,brown",
    .delim = ',',
    .quoted = true,
    .remove_quotes = false,
    .expected = {
        {
            .token = "The",
            .result = TOKEN_OK
        },
        {
            .token = "\"quick,brown",
            .result = TOKEN_UNFINISHED_QUOTE
        },
        {
            .token = NULL,
            .result = TOKEN_END
        },
    }
};

static struct test_data test10 =
{
    .name = "test10",
    .str = "\"",
    .delim = ',',
    .quoted = true,
    .remove_quotes = false,
    .expected = {
        {
            .token = "\"",
            .result = TOKEN_UNFINISHED_QUOTE
        },
        {
            .token = NULL,
            .result = TOKEN_END
        },
    }
};

static struct test_data test11 =
{
    .name = "test11",
    .str = "The,quick\",,\"brown,fox,\"\"jumps,\"over,the\"",
    .delim = ',',
    .quoted = true,
    .remove_quotes = true,
    .expected = {
        {
            .token = "The",
            .result = TOKEN_OK
        },
        {
            .token = "quick,,brown",
            .result = TOKEN_OK
        },
        {
            .token = "fox",
            .result = TOKEN_OK
        },
        {
            .token = "jumps",
            .result = TOKEN_OK
        },
        {
            .token = "over,the",
            .result = TOKEN_OK
        },
        {
            .token = NULL,
            .result = TOKEN_END
        },
    }
};

static struct test_data test12 =
{
    .name = "test12",
    .str = "The,'quick\",,\"brown',fox,\"\"jumps,\"over,the\"",
    .delim = ',',
    .quoted = true,
    .remove_quotes = true,
    .expected = {
        {
            .token = "The",
            .result = TOKEN_OK
        },
        {
            .token = "quick\",,\"brown",
            .result = TOKEN_OK
        },
        {
            .token = "fox",
            .result = TOKEN_OK
        },
        {
            .token = "jumps",
            .result = TOKEN_OK
        },
        {
            .token = "over,the",
            .result = TOKEN_OK
        },
        {
            .token = NULL,
            .result = TOKEN_END
        },
    }
};

static struct test_data test13 =
{
    .name = "test13",
    .str = "The,\"quick',,'brown\",fox,jumps','over\",\"the,lazy\"",
    .delim = ',',
    .quoted = true,
    .remove_quotes = true,
    .expected = {
        {
            .token = "The",
            .result = TOKEN_OK
        },
        {
            .token = "quick',,'brown",
            .result = TOKEN_OK
        },
        {
            .token = "fox",
            .result = TOKEN_OK
        },
        {
            .token = "jumps,over,the",
            .result = TOKEN_OK
        },
        {
            .token = "lazy\"",
            .result = TOKEN_UNFINISHED_QUOTE
        },
        {
            .token = NULL,
            .result = TOKEN_END
        },
    }
};

static bool perform_test(struct test_data *td)
{
    assert(td);

    struct lexer_data lexer;
    char *str;
    bool result = true;
    int i = 0;

    str = strdup(td->str);
    assert(str);

    init_lexer(&lexer, str, strlen(str) + 1);

    while (true) {
        enum token_result tr;
        char *token;

        tr = next_token(&lexer, &token, td->delim, td->quoted, td->remove_quotes);
        if (tr != td->expected[i].result) {
            printf("TEST lexer (%s): Unexpected result for `next_token`: %d for expected token [%s]. Expected result %d\n",
                    td->name, tr, td->expected[i].token, td->expected[i].result);
            result = false;
        } else if ((tr == TOKEN_OK) && (strcmp(td->expected[i].token, token) != 0)) {
            printf("TEST lexer (%s): Unexpected token: [%s]. Expected [%s]\n",
                    td->name, token, td->expected[i].token);
            result = false;
        }

        if (td->expected[i].result == TOKEN_END) {
            break;
        }

        i++;
    }

    free(str);

    return result;
}

int main(void)
{
    bool success = true;

    success &= perform_test(&test1);
    success &= perform_test(&test2);
    success &= perform_test(&test3);
    success &= perform_test(&test4);
    success &= perform_test(&test5);
    success &= perform_test(&test6);
    success &= perform_test(&test7);
    success &= perform_test(&test8);
    success &= perform_test(&test9);
    success &= perform_test(&test10);
    success &= perform_test(&test11);
    success &= perform_test(&test12);
    success &= perform_test(&test13);

    if (success) {
        printf("All tests OK\n");
    } else {
        printf("Some tests FAIL\n");
    }

    return success ? 0 : 1;
}
