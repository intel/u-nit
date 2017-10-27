#ifndef INITTAB_HEADER_
#define INITTAB_HEADER_

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

enum inittab_entry_type {ONE_SHOT, SAFE_ONE_SHOT, SERVICE, SAFE_SERVICE,
    SHUTDOWN, SAFE_SHUTDOWN, SAFE_MODE};

struct inittab_entry {
    struct inittab_entry *next;
    char process_name[4096];
    char ctty_path[256];
    int32_t order;
    int32_t core_id;
    enum inittab_entry_type type;
};

struct inittab {
    struct inittab_entry *startup_list;
    struct inittab_entry *shutdown_list;
    struct inittab_entry *safe_mode_entry;
};

bool read_inittab(const char *filename, struct inittab *inittab_entries);
void free_inittab_entry_list(struct inittab_entry *list);

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
