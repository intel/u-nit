CC ?= gcc
CFLAGS += -std=c99 -Wall -D_DEFAULT_SOURCE
LDFLAGS +=

AFL_CC ?= afl-gcc

TESTS_CFLAGS += $(CFLAGS) "-Isrc/"

ifeq ($(DEBUG),1)
	CFLAGS += -g -O0
endif

.PHONY: clean

ALL: init

SOURCE = \
	src/inittab.c \
	src/lexer.c \
	src/log.c \
	src/main.c \
	src/mainloop.c \
	src/mount.c \
	src/watchdog.c

OBJS = $(SOURCE:.c=.o)

AUX_QEMU_TESTS=tests/sleep_crash_test tests/sleep_test

*.o: *.c
	$(CC) $(CFLAGS) $< -o $@

init: $(OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)

clean:
	rm -rf init $(OBJS) $(TESTS) $(AFL_TESTS) $(AUX_QEMU_TESTS)

TESTS = parser_test

AFL_TESTS = afl_parser_test

parser_test: src/lexer.o src/log.o tests/parser_test.c
	$(CC) $(TESTS_CFLAGS) $^ -o $@ $(LDFLAGS)

afl_parser_test: src/lexer.o src/log.o src/inittab.o tests/afl_parser_test.c
	$(AFL_CC) $(TESTS_CFLAGS) $^ -o $@ $(LDFLAGS)

tests: $(TESTS)

afl_tests: $(AFL_TESTS)

tests/sleep_crash_test: tests/sleep_crash_test.c
	$(CC) $(CFLAGS) $< -o $@

tests/sleep_test: tests/sleep_test.c
	$(CC) $(CFLAGS) $< -o $@

.PHONY:
run-qemu-tests: init $(AUX_QEMU_TESTS)
	./qemu-tests.sh

.PHONY:
format-code:
	clang-format -i -style=file src/*.c src/*.h
