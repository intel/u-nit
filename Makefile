CC ?= gcc
CFLAGS += -std=c99 -Wall -D_DEFAULT_SOURCE
LDFLAGS +=

AFL_CC ?= afl-gcc

CFLAGS_COVERAGE ?= $(CFLAGS) -fprofile-arcs -ftest-coverage -fprofile-dir=/gcov -DCOMPILING_COVERAGE
LDFLAGS_COVERAGE ?= $(LDFLAGS) -fprofile-arcs

CFLAGS_ASAN ?= $(CFLAGS) -fsanitize=address
LDFLAGS_ASAN ?= $(LDFLAGS) -fsanitize=address

TESTS_CFLAGS += $(CFLAGS) "-Isrc/"

DESTDIR ?=
PREFIX ?= "/usr/bin"

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

clean:
	rm -rf init $(OBJS) $(TESTS) $(AFL_TESTS) $(AUX_QEMU_TESTS) $(GCOV_GCNO) $(GCOV_GCDA) $(LCOV_FILES)

install: init
	install -D init "$(DESTDIR)/$(PREFIX)/init"

TESTS = inittab_test lexer_test

AFL_TESTS = afl_inittab_test

inittab_test: src/lexer.o src/log.o tests/inittab_test.c
	$(CC) $(TESTS_CFLAGS) $^ -o $@ $(LDFLAGS)

afl_inittab_test: src/lexer.o src/log.o src/inittab.o tests/afl_inittab_test.c
	$(AFL_CC) $(TESTS_CFLAGS) $^ -o $@ $(LDFLAGS)

lexer_test: src/lexer.o tests/lexer_test.c
	$(CC) $(TESTS_CFLAGS) $^ -o $@ $(LDFLAGS)

tests: $(TESTS)

afl_tests: $(AFL_TESTS)

tests/sleep_crash_test: tests/sleep_crash_test.c
	$(CC) $(CFLAGS) $< -o $@

tests/sleep_test: tests/sleep_test.c
	$(CC) $(CFLAGS) $< -o $@

.PHONY:
format-code:
	clang-format -i -style=file src/*.c src/*.h

.PHONY:
coverage: clean
	$(MAKE) CFLAGS="$(CFLAGS_COVERAGE)" LDFLAGS="$(LDFLAGS_COVERAGE)"

.PHONY:
init-asan:
	$(MAKE) CFLAGS="$(CFLAGS_ASAN)" LDFLAGS="$(LDFLAGS_ASAN)"

.PHONY:
run-qemu-tests: $(AUX_QEMU_TESTS)
	./qemu-tests.sh ordinary

.PHONY:
run-qemu-fault: $(AUX_QEMU_TESTS)
	./qemu-tests.sh fault-injection

.PHONY:
run-valgrind-tests: $(AUX_QEMU_TESTS)
	./qemu-tests.sh valgrind

.PHONY:
run-asan-tests: $(AUX_QEMU_TESTS)
	./qemu-tests.sh asan

.PHONY:
extract-coverage:
	./qemu-tests.sh --extract-coverage-information
	lcov -d . -c -o lcov.info  --rc lcov_branch_coverage=1
	genhtml lcov.info --output-directory lcov-out --branch-coverage

.PHONY:
show-coverage-report:
	xdg-open lcov-out/index.html

.PHONY:
clean-coverage:
	./qemu-tests.sh --clean-coverage-information
