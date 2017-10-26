#include "parser.h"

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "log.h"

#define LINE_SIZE 4095
#define BUFFER_LEN LINE_SIZE + 1

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

enum token_result {TOKEN_OK, TOKEN_BLANK, TOKEN_ERROR};
enum next_line_result {NEXT_LINE_OK, NEXT_LINE_TOO_BIG, NEXT_LINE_EOF, NEXT_LINE_ERROR};

struct lexer_data {
    char *buf;
    size_t size;
    size_t pos;
};

static bool
safe_strtoi32_t(const char *s, int32_t *ret)
{
    char *end_ptr = NULL;
    long int l;
    bool result = true;

    errno = 0;
    l = strtol(s, &end_ptr, 10);

    if (((errno != 0) || (*end_ptr != '\0')) || ((l > INT32_MAX) || (l < INT32_MIN))) {
        result = false;
    }

    if (result) {
        *ret = (int32_t)l;
    }

    return result;
}

static void
init_lexer(struct lexer_data *lexer, char *buf, size_t size)
{
    lexer->buf = buf;
    lexer->size = size;
    lexer->pos = 0U;
};

static enum token_result
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

static enum next_line_result
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

enum inittab_parse_result
inittab_parse_entry(FILE *fp, struct inittab_entry *entry)
{
    char buf[BUFFER_LEN] = { };
    struct lexer_data lexer = { };
    enum inittab_parse_result result = RESULT_OK;
    enum next_line_result next;
    enum token_result tr;

    char *order_str = NULL, *core_id_str = NULL, *type_str = NULL, *process_str = NULL;

    if ((fp == NULL) || (feof(fp) != 0)) {
        result = RESULT_ERROR;
        goto end;
    }

    next = inittab_next_line(fp, buf);

    if (next == NEXT_LINE_TOO_BIG) {
        log_message("Line too big: '%.20s(...)'\n", buf);
        result = RESULT_ERROR;
        goto end;
    } else if (next == NEXT_LINE_ERROR) {
        log_message("Couldn't read inittab file\n");
        result = RESULT_ERROR;
        goto end;
    } else if (next == NEXT_LINE_EOF){
        result = RESULT_DONE;
        goto end;
    } else {
        /* Everything is fine */
    }

    /* Set lexer up */
    init_lexer(&lexer, buf, sizeof(buf));

    /* Get <order> */
    tr = next_token(&lexer, &order_str, ':');
    if (tr == TOKEN_BLANK) {
        entry->order = -1;
    } else if (tr == TOKEN_ERROR) {
        log_message("Invalid 'order' field on inittab entry\n");
        result = RESULT_ERROR;
        goto end;
    } else {
        if (!safe_strtoi32_t(order_str, &entry->order)) {
            log_message("Invalid 'order' field on inittab entry\n");
            result = RESULT_ERROR;
            goto end;
        }
        if (entry->order < 0) {
            log_message("Invalid order 'field' on inittab entry\n");
            result = RESULT_ERROR;
            goto end;
        }
    }

    /* Get <core_id> */
    tr = next_token(&lexer, &core_id_str, ':');
    if (tr == TOKEN_BLANK) {
        entry->core_id = -1;
    } else if (tr == TOKEN_ERROR) {
        log_message("Invalid 'core_id' field on inittab entry\n");
        result = RESULT_ERROR;
        goto end;
    } else {
        if (!safe_strtoi32_t(core_id_str, &entry->core_id)) {
            log_message("Invalid 'core_id' field on inittab entry\n");
            result = RESULT_ERROR;
            goto end;
        }
        if (entry->core_id < 0) {
            log_message("Invalid 'core_id' field on inittab entry\n");
            result = RESULT_ERROR;
            goto end;
        }
    }

    /*Get <type> */
    tr = next_token(&lexer, &type_str, ':');
    if (tr != TOKEN_OK) {
        log_message("Expected 'type' field on inittab entry\n");
        result = RESULT_ERROR;
        goto end;
    } else {
        if (strcmp(type_str, "<one-shot>") == 0) {
            entry->type = ONE_SHOT;
        } else if (strcmp(type_str, "<safe-one-shot>") == 0) {
            entry->type = SAFE_ONE_SHOT;
        } else if (strcmp(type_str, "<service>") == 0) {
            entry->type = SERVICE;
        } else if (strcmp(type_str, "<safe-service>") == 0) {
            entry->type = SAFE_SERVICE;
        } else if (strcmp(type_str, "<shutdown>") == 0) {
            entry->type = SHUTDOWN;
        } else if (strcmp(type_str, "<safe-shutdown>") == 0) {
            entry->type = SAFE_SHUTDOWN;
        } else if (strcmp(type_str, "<safe-mode>") == 0) {
            entry->type = SAFE_MODE;
        } else {
            log_message("Invalid 'type' field on inittab entry: %s\n", type_str);
            result = RESULT_ERROR;
            goto end;
        }
    }

    /* Now that we know entry type, check if it has a valid order */
    if ((entry->order == -1) && (entry->type != SAFE_MODE)) {
        log_message("Expected 'order' field on entry with type different of '<safe-mode>'\n");
        result = RESULT_ERROR;
        goto end;
    }

    /*Get <process> */
    tr = next_token(&lexer, &process_str, '\0');
    if (tr != TOKEN_OK) {
        log_message("Expected 'process' field on inittab entry\n");
        result = RESULT_ERROR;
        goto end;
    } else if (strlen(process_str) < sizeof(entry->process_name)){
        (void)strcpy(entry->process_name, process_str);
    } else {
        log_message("Invalid 'process' field on inittab entry\n");
        result = RESULT_ERROR;
        goto end;
    }

end:
    return result;
}
