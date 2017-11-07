/*
 * Copyright (C) 2017 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#include "lexer.h"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

void
init_lexer(struct lexer_data *lexer, char *buf, size_t size)
{
    lexer->buf = buf;
    lexer->size = size;
    lexer->pos = 0U;
};

enum token_result
next_token(struct lexer_data *lexer, char **token, char delim)
{
    size_t start_pos = lexer->pos;
    enum token_result ret;

    while ((lexer->buf[lexer->pos] != delim) && (lexer->buf[lexer->pos] != '\0') && (lexer->pos < lexer->size)) {
        lexer->pos++;
    }

    if (lexer->pos == start_pos) {
        /* No content on token before next delimiter, so it's a blank one */
        ret = TOKEN_BLANK;
    } else if (lexer->pos >= lexer->size) {
        /* No token and buffer finished - raise error */
        ret = TOKEN_ERROR;
    } else {
        /* Delimiter or '\0' was found. Happiness */
        ret = TOKEN_OK;
    }

    if (ret != TOKEN_ERROR) {
        /* Adjust delimiter to '\0', so token returned ends properly */
        lexer->buf[lexer->pos] = '\0';
        *token = &lexer->buf[start_pos];

        /* Advance lexer current position so next token starts properly */
        lexer->pos++;
    }

    return ret;
}

static void
discard_line(FILE *fp)
{
    char buf[BUFFER_LEN];

    while (true) {
        int c;

        (void)fscanf(fp, "%"STR(LINE_SIZE)"[^\n]", buf);
        c = fgetc(fp); /* Discards any left '\n' */

        if ((c == (int)'\n') || (c == EOF)) {
            break;
        }
    }
}

enum next_line_result
inittab_next_line(FILE *fp, char buf[])
{
    int r = 0, c = 0;
    enum next_line_result result = NEXT_LINE_ERROR;

    while (true) {
        bool end_loop = false;

        r = fscanf(fp, "%"STR(LINE_SIZE)"[^\n]", buf);
        c = fgetc(fp); /* Discards any left '\n' */

        if (r == EOF) {
            end_loop = true; /* Read error or nothing more to read */
            if (feof(fp) != 0) {
                result = NEXT_LINE_EOF;
            } else {
                result = NEXT_LINE_ERROR;
            }
        } else if ((buf[0] == '#') && ((c == (int)'\n') || (c == EOF))) {
            /* Read a small comment, move to next line*/
        } else if (buf[0] == '\0') {
            /* Read empty line, move to next line*/
        } else if ((c != (int)'\n') && (c != EOF)) {
            end_loop = true; /* Line too big */
            result = NEXT_LINE_TOO_BIG;
            discard_line(fp);
        } else {
            end_loop = true;
            result = NEXT_LINE_OK;
        }

        if (end_loop) {
            break;
        }
    }

    return result;
}
