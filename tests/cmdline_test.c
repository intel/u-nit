/*
 * Copyright (C) 2017 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <cmdline.h>
#include <macros.h>

struct test_data {
    const char *name;
    const char *cmdline;
    bool expected_success;
    const char *expected_env_args[]; /* Env and args arrays merged. Env ends on first NULL, args on second */
} test_data;


struct test_data test1 = {
    .name = "test1",
    .cmdline = "ENV1=aa /blah arg1",
    .expected_success = true,
    .expected_env_args = {
        "ENV1=aa", NULL,
        "/blah", "arg1", NULL
    }
};

struct test_data test2 = {
    .name = "test1",
    .cmdline = "/blah",
    .expected_success = true,
    .expected_env_args = {
        NULL,
        "/blah", NULL
    }
};

struct test_data test3 = {
    .name = "test3",
    .cmdline = "A=a B=\"bbb b\" C='ccc c'c D\"D\"=dd'dd\" \"dd'dd /blah aaa bbb 'aa bb' \"cc'dd ee' ff\"",
    .expected_success = true,
    .expected_env_args = {
        "A=a", "B=bbb b", "C=ccc cc", "DD=dddd\" \"dddd", NULL,
        "/blah", "aaa", "bbb", "aa bb", "cc'dd ee' ff", NULL
    }
};

struct test_data test4 = {
    .name = "test4",
    .cmdline = "/blah 'args",
    .expected_success = false,
    .expected_env_args = {
        NULL,
    }
};

struct test_data test5 = {
    .name = "test5",
    .cmdline = "E=\"aaa /blah arg",
    .expected_success = false,
    .expected_env_args = {
        NULL,
    }
};

struct test_data test6 = {
    .name = "test6",
    .cmdline = "A='this test has more than 128 env vars' A1=1 A2=2 A3=3 A4=4 A5=5 A6=6 A7=7 A8=8 A9=9 A10=10 A11=11 A12=12 A13=13 A14=14 A15=15 A16=16 A17=17 A18=18 A19=19 A20=20 A21=21 A22=22 A23=23 A24=24 A25=25 A26=26 A27=27 A28=28 A29=29 A30=30 A31=31 A32=32 A33=33 A34=34 A35=35 A36=36 A37=37 A38=38 A39=39 A40=40 A41=41 A42=42 A43=43 A44=44 A45=45 A46=46 A47=47 A48=48 A49=49 A50=50 A51=51 A52=52 A53=53 A54=54 A55=55 A56=56 A57=57 A58=58 A59=59 A60=60 A61=61 A62=62 A63=63 A64=64 A65=65 A66=66 A67=67 A68=68 A69=69 A70=70 A71=71 A72=72 A73=73 A74=74 A75=75 A76=76 A77=77 A78=78 A79=79 A80=80 A81=81 A82=82 A83=83 A84=84 A85=85 A86=86 A87=87 A88=88 A89=89 A90=90 A91=91 A92=92 A93=93 A94=94 A95=95 A96=96 A97=97 A98=98 A99=99 A100=100 A101=101 A102=102 A103=103 A104=104 A105=105 A106=106 A107=107 A108=108 A109=109 A110=110 A111=111 A112=112 A113=113 A114=114 A115=115 A116=116 A117=117 A118=118 A119=119 A120=120 A121=121 A122=122 A123=123 A124=124 A125=125 A126=126 A127=127 A128=128 A129=129 A130=130 /blah",
    .expected_success = false,
    .expected_env_args = {
        NULL,
    }
};

struct test_data test7 = {
    .name = "test7",
    .cmdline = "A='this test has more than 128 args' /blah A1=1 A2=2 A3=3 A4=4 A5=5 A6=6 A7=7 A8=8 A9=9 A10=10 A11=11 A12=12 A13=13 A14=14 A15=15 A16=16 A17=17 A18=18 A19=19 A20=20 A21=21 A22=22 A23=23 A24=24 A25=25 A26=26 A27=27 A28=28 A29=29 A30=30 A31=31 A32=32 A33=33 A34=34 A35=35 A36=36 A37=37 A38=38 A39=39 A40=40 A41=41 A42=42 A43=43 A44=44 A45=45 A46=46 A47=47 A48=48 A49=49 A50=50 A51=51 A52=52 A53=53 A54=54 A55=55 A56=56 A57=57 A58=58 A59=59 A60=60 A61=61 A62=62 A63=63 A64=64 A65=65 A66=66 A67=67 A68=68 A69=69 A70=70 A71=71 A72=72 A73=73 A74=74 A75=75 A76=76 A77=77 A78=78 A79=79 A80=80 A81=81 A82=82 A83=83 A84=84 A85=85 A86=86 A87=87 A88=88 A89=89 A90=90 A91=91 A92=92 A93=93 A94=94 A95=95 A96=96 A97=97 A98=98 A99=99 A100=100 A101=101 A102=102 A103=103 A104=104 A105=105 A106=106 A107=107 A108=108 A109=109 A110=110 A111=111 A112=112 A113=113 A114=114 A115=115 A116=116 A117=117 A118=118 A119=119 A120=120 A121=121 A122=122 A123=123 A124=124 A125=125 A126=126 A127=127 A128=128 A129=129 A130=130",
    .expected_success = false,
    .expected_env_args = {
        NULL,
    }
};

struct test_data test8 = {
    .name = "test8",
    .cmdline = "/blah aaa bbb 'aa bb' \"cc'dd ee' ff\"",
    .expected_success = true,
    .expected_env_args = {
        NULL,
        "/blah", "aaa", "bbb", "aa bb", "cc'dd ee' ff", NULL
    }
};

static bool not_equal(const char *a, const char *b)
{
    if (a == b) {
        return false;
    }

    if (a != NULL && b != NULL) {
        return strcmp(a, b) != 0;
    }

    return true;
}

static bool perform_test(struct test_data *td)
{
    struct cmdline_contents cmd_contents = { };
    bool result = true, parse_success;

    parse_success = parse_cmdline(td->cmdline, &cmd_contents);
    if ((parse_success != td->expected_success)) {
        printf("TEST %s: Unexpected return from `parse_cmdline`. Expected %d, got %d\n",
                td->name, td->expected_success, parse_success);
        result = false;

    } else if (parse_success) {
        int i = 0;
        /* Check env vars (end on first NULL in env_args field) */
        do {
            if (not_equal(td->expected_env_args[i], cmd_contents.env[i])) {
                printf("TEST %s: Unexpected env[%d]. Expected [%s], got [%s]\n",
                        td->name, i, td->expected_env_args[i], cmd_contents.env[i]);
                result = false;
                break;
            }
        } while (td->expected_env_args[i++] != NULL);

        /* Check args (only if env vars didn't fail) */
        if (result) {
            int j = 0;
            do {
                if (not_equal(td->expected_env_args[i], cmd_contents.args[j])) {
                    printf("TEST %s: Unexpected arg[%d]. Expected [%s], got [%s]\n",
                            td->name, j, td->expected_env_args[i], cmd_contents.args[j]);
                    result = false;
                    break;
                }
                j++;
            } while (td->expected_env_args[i++] != NULL);
        }

        free_cmdline_contents(&cmd_contents);
    } else {
        /* OK */
    }

    return result;
}

int main(void)
{
    bool success = true;

    success &= perform_test(&test1);
    success &= perform_test(&test2);
    success &= perform_test(&test3);
    success &= perform_test(&test4);
    success &= perform_test(&test5);
    success &= perform_test(&test6);
    success &= perform_test(&test7);

    if (success) {
        printf("All tests OK\n");
    } else {
        printf("Some tests FAIL\n");
    }

    return success ? 0 : 1;
}
