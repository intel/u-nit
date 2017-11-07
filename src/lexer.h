/*
 * Copyright (C) 2017 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#ifndef LEXER_HEADER_
#define LEXER_HEADER_

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

#define LINE_SIZE 4095
#define BUFFER_LEN LINE_SIZE + 1

enum token_result {TOKEN_OK, TOKEN_BLANK, TOKEN_ERROR};
enum next_line_result {NEXT_LINE_OK, NEXT_LINE_TOO_BIG, NEXT_LINE_EOF, NEXT_LINE_ERROR};

struct lexer_data {
    char *buf;
    size_t size;
    size_t pos;
};

void init_lexer(struct lexer_data *lexer, char *buf, size_t size);
enum next_line_result inittab_next_line(FILE *fp, char buf[]);
enum token_result next_token(struct lexer_data *lexer, char **token, char delim);

#endif
