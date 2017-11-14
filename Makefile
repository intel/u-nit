CC ?= gcc
CFLAGS += -std=c99 -Wall -D_DEFAULT_SOURCE
LDFLAGS +=

AFL_CC ?= afl-gcc

CFLAGS_COVERAGE ?= $(CFLAGS) -fprofile-arcs -ftest-coverage -fprofile-dir=/gcov -DCOMPILING_COVERAGE
LDFLAGS_COVERAGE ?= $(LDFLAGS) -fprofile-arcs

CFLAGS_ASAN ?= $(CFLAGS) -fsanitize=address
LDFLAGS_ASAN ?= $(LDFLAGS) -fsanitize=address

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
GCOV_GCNO = $(SOURCE:.c=.gcno)
GCOV_GCDA = $(SOURCE:.c=.gcda)
LCOV_FILES = lcov.info lcov-out

AUX_QEMU_TESTS=tests/sleep_crash_test tests/sleep_test

*.o: *.c
	$(CC) $(CFLAGS) $< -o $@

init: $(OBJS)
	$(CC) $^ -o $@ $(LDFLAGS)

.PHONY:
init-asan:
	$(MAKE) CFLAGS="$(CFLAGS_ASAN)" LDFLAGS="$(LDFLAGS_ASAN)"

clean:
	rm -rf init $(OBJS) $(TESTS) $(AFL_TESTS) $(AUX_QEMU_TESTS) $(GCOV_GCNO) $(GCOV_GCDA) $(LCOV_FILES)

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
coverage:
	$(MAKE) CFLAGS="$(CFLAGS_COVERAGE)" LDFLAGS="$(LDFLAGS_COVERAGE)"

.PHONY:
run-qemu-tests-coverage: coverage $(AUX_QEMU_TESTS)
	./qemu-tests.sh --extract-coverage-information
	lcov -d . -c -o lcov.info
	genhtml lcov.info --output-directory lcov-out
	xdg-open lcov-out/index.html

.PHONY:
format-code:
	clang-format -i -style=file src/*.c src/*.h

.PHONY:
run-valgrind-tests: init $(AUX_QEMU_TESTS)
	./qemu-tests.sh --run-and-check-valgrind

.PHONY:
run-asan-tests: clean init-asan $(AUX_QEMU_TESTS)
	./qemu-tests.sh --check-asan
