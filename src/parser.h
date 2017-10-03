#ifndef PARSER_HEADER_
#define PARSER_HEADER_

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
#endif
