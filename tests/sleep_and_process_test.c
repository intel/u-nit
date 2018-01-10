/*
 * Copyright (C) 2017 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* This test sleeps for some time, but divides that time in
 * ten parts and do some computation. This is so that it wakes
 * up and gets a chance to change the core in which is being run
 * */
int
main(int argc, char *argv[])
{
    printf("START: %s - %s - %s\n", argv[0], argv[1], argv[2]);
    if (argc == 3) {
        int sleep_time, i, j = 2;
        sleep_time = atoi(argv[2]) / 10;
        if (sleep_time == 0) {
            sleep_time = 1;
        }

        for (i = 0; i < 10; i++) {
            sleep(atoi(argv[2]));
            j *= sleep_time;
        }
    }

    return 0;
}
