#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
    printf("START: %s - %s - %s\n", argv[0], argv[1], argv[2]);
    if (argc == 3) {
        sleep(atoi(argv[2]));
    }

    return 0;
}
