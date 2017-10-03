#include <errno.h>
#include <stdio.h>

#include "parser.h"

static void
usage(const char *invocation_name)
{
    printf("Usage: \n"
            " %s <inittab-file>\n"
            "\n"
            "System and service manager [early development]\n",
            invocation_name);
}

int main(int argc, char *argv[])
{
    FILE *fp = NULL;
    struct inittab_entry entry = { };
    enum inittab_parse_result r;

    if (argc != 2) {
        usage(argv[0]);
        goto end;
    }

    errno = 0;
    fp = fopen(argv[1], "re");
    if (fp == NULL) {
        perror("Couldn't open inittab file");
        goto end;
    }

    printf("Reading inittab entries...\n");
    while ((r = inittab_parse_entry(fp, &entry)) == RESULT_OK) {
        printf("[Entry] order: %d, core_id: %d, type: %d, process: '%s'\n",
                entry.order, entry.core_id, entry.type, entry.process_name);
    }

    printf("Reading result: %s\n", r == RESULT_DONE ? "Ok" : "Error");

    /* If fclose fails, we can't do much about it */
    errno = 0;
    if (fclose(fp) != 0) {
        perror("Error closing inittab file");
    }

end:
    return 0;
}
