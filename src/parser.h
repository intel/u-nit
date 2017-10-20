#ifndef PARSER_HEADER_
#define PARSER_HEADER_

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

enum inittab_entry_type {ONE_SHOT, SAFE_ONE_SHOT, SERVICE, SAFE_SERVICE,
    SHUTDOWN, SAFE_SHUTDOWN, SAFE_MODE};
enum inittab_parse_result {RESULT_OK, RESULT_ERROR, RESULT_DONE};

struct inittab_entry {
    char process_name[4096];
    int32_t order;
    int32_t core_id;
    enum inittab_entry_type type;
};

enum inittab_parse_result
inittab_parse_entry(FILE *fp, struct inittab_entry *entry);

static inline bool
is_safe_entry(const struct inittab_entry *entry)
{
    return
        (entry->type == SAFE_SERVICE) ||
        (entry->type == SAFE_ONE_SHOT) ||
        (entry->type == SAFE_MODE) ||
        (entry->type == SAFE_SHUTDOWN);
}

static inline bool
is_startup_entry(const struct inittab_entry *entry)
{
    return
        (entry->type == ONE_SHOT) ||
        (entry->type == SAFE_ONE_SHOT) ||
        (entry->type == SERVICE) ||
        (entry->type == SAFE_SERVICE);
}

static inline bool
is_service_entry(const struct inittab_entry *entry)
{
    return
        (entry->type == SERVICE) ||
        (entry->type == SAFE_SERVICE);
}

static inline bool
is_shutdown_entry(const struct inittab_entry *entry)
{
    return
        (entry->type == SHUTDOWN) ||
        (entry->type == SAFE_SHUTDOWN);
}

static inline bool
is_one_shot_entry(const struct inittab_entry *entry)
{
    return
        (entry->type == ONE_SHOT) ||
        (entry->type == SAFE_ONE_SHOT) ||
        (entry->type == SHUTDOWN) ||
        (entry->type == SAFE_SHUTDOWN);
}
#endif
