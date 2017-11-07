#include <assert.h>
#include <stdio.h>

#include <inittab.h>

int
main(int argc, char *argv[])
{
    bool b;
    struct inittab inittab_entries = { };

    assert(argc == 2);

    b = read_inittab(argv[1], &inittab_entries);
    printf("Read %s\n", b ? "OK": "FAIL");

    if (b) {
        free_inittab_entry_list(inittab_entries.startup_list);
        free_inittab_entry_list(inittab_entries.shutdown_list);
        free_inittab_entry_list(inittab_entries.safe_mode_entry);
    }

    return 0;
}
