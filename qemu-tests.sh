#!/bin/sh

# Copyright (C) 2017 Intel Corporation
# SPDX-License-Identifier: MIT

TESTDIR=tests/data
QEMUDIR=tests/init-qemu
SRCDIR=src
FAULT_DEFINITIONS=$TESTDIR/qemu/fault-definitions

LOG_FILE=qemu-tests.log

INIT_EXEC=init
SLEEP_TEST_EXEC=tests/sleep_test
SLEEP_CRASH_TEST_EXEC=tests/sleep_crash_test
SLEEP_AND_PROCESS_TEST_EXEC=tests/sleep_and_process_test
IS_RUNNING_EXEC=tests/is_running
IS_NOT_RUNNING_EXEC=tests/is_not_running
INSPECT_MOUNT=tests/inspect-mount
CHECK_CPU_AFFINITY_EXEC=tests/check_cpu_affinity
SHOW_ARGS_ENV_EXEC=tests/show_args_env
SAFE_MODE_EXEC=tests/safe-mode

ROOT_FS=rootfs.ext2
GCOV_FS=gcov.ext4
TEST_FS_1=test-fs.ext4
TEST_FS_2=test-fs2.ext4

EXEC_TIMEOUT=60
LOG_QEMU=
LOG_CONTAINER=

EXTRACT_COVERAGE=false
CLEAN_COVERAGE=false
CHECK_VALGRIND=false
CHECK_ASAN=false
CHECK_ORDINARY=false
FAULT_INJECTION=false

DO_SETUP=false

ASAN_EXIT_ERROR=129

DEFAULT_KERNEL_CMDLINE="root=/dev/sda rw console=ttyS0 iip=dhcp panic=-1"
DEFAULT_FAULT_INJECTION_REPEATS=5

LIBFIU_PRELOAD="LD_PRELOAD=\"/usr/lib/fiu_run_preload.so /usr/lib/fiu_posix_preload.so\""

KERNEL_PANIC_OK="Kernel panic - not syncing: Attempted to kill init! exitcode=0x00000100"

function log_message() {
    echo "$@" >> $LOG_FILE
}

function run_qemu() {
    LOG_INIT=$1
    KERNEL_CMDLINE="$DEFAULT_KERNEL_CMDLINE $2"

    LOG_QEMU=$(timeout -s TERM --foreground ${EXEC_TIMEOUT}  \
        qemu-system-x86_64 -machine q35,kernel_irqchip=split -m 512M -enable-kvm \
          -no-reboot \
          -smp 4 -device intel-iommu,intremap=on,x-buggy-eim=on \
          -s -kernel $QEMUDIR/bzImage  \
          -cpu kvm64,-kvm_pv_eoi,-kvm_steal_time,-kvm_asyncpf,-kvmclock,+vmx \
          -hda $QEMUDIR/$ROOT_FS \
          -hdb $QEMUDIR/$GCOV_FS \
          -hdc $QEMUDIR/$TEST_FS_1 \
          -hdd $QEMUDIR/$TEST_FS_2 \
          -serial mon:stdio \
          -chardev file,id=char0,path=${LOG_INIT} -serial chardev:char0 \
          -append "$KERNEL_CMDLINE" \
          -netdev user,id=net -device e1000e,addr=2.0,netdev=net \
          -device ib700,id=watchdog0 \
          -device intel-hda,addr=1b.0 -nographic -device hda-duplex 2>&1)
}

function run_valgrind_container() {
    LOG_INIT=$1
    LOG_CONTAINER=$(sudo timeout -s KILL --foreground ${EXEC_TIMEOUT} \
        systemd-nspawn -i $QEMUDIR/$ROOT_FS \
          --bind=${LOG_INIT}:/dev/ttyS1 \
          -q \
          /usr/bin/valgrind --leak-check=full --show-leak-kinds=all /sbin/init 2>&1)
}

function run_asan_container() {
    LOG_INIT=$1
    LOG_CONTAINER=$(sudo timeout -s KILL --foreground ${EXEC_TIMEOUT} \
        systemd-nspawn -i $QEMUDIR/$ROOT_FS \
          --bind=${LOG_INIT}:/dev/ttyS1 \
          -q \
          -E ASAN_OPTIONS=replace_str=1,detect_invalid_pointer_pairs=2,intercept_strlen=1,exitcode=${ASAN_EXIT_ERROR},verbosity=2 \
          /sbin/init 2>&1)
}

function mount_test_fs() {
    if [ ! -d "$QEMUDIR/mnt" ]; then
        mkdir $QEMUDIR/mnt
    fi

    sudo mount $QEMUDIR/$1 $QEMUDIR/mnt/
}

function umount_test_fs() {
    sudo umount $QEMUDIR/mnt
}

function setup_environment() {
    mount_test_fs $ROOT_FS
    sudo cp $INIT_EXEC $QEMUDIR/mnt/usr/sbin/init
    sudo cp $SLEEP_TEST_EXEC $QEMUDIR/mnt/usr/bin/
    sudo cp $SLEEP_CRASH_TEST_EXEC $QEMUDIR/mnt/usr/bin/
    sudo cp $SLEEP_AND_PROCESS_TEST_EXEC $QEMUDIR/mnt/usr/bin/
    sudo cp $IS_RUNNING_EXEC $QEMUDIR/mnt/usr/bin/
    sudo cp $IS_NOT_RUNNING_EXEC $QEMUDIR/mnt/usr/bin/
    sudo cp $INSPECT_MOUNT $QEMUDIR/mnt/usr/bin/
    sudo cp $CHECK_CPU_AFFINITY_EXEC $QEMUDIR/mnt/usr/bin/
    sudo cp $SHOW_ARGS_ENV_EXEC $QEMUDIR/mnt/usr/bin/
    sudo cp $SAFE_MODE_EXEC $QEMUDIR/mnt/usr/bin/
    umount_test_fs
}

function update_inittab() {
    sudo cp $1 $QEMUDIR/mnt/etc/inittab
}

function update_fstab() {
    sudo cp $1 $QEMUDIR/mnt/etc/fstab

    if [ -n "${1##$TESTDIR/qemu/*/fail-*}" ]; then
        sudo cp "$1-inspect" $QEMUDIR/mnt/usr/share/expected_mounts
    fi
}

function inspect() {
    INSPECT=$1
    LOG=$2
    INITTAB=$3
    FSTAB=$4
    QEMU_EXITCODE=$5
    RESULT=0

    log_message "-------------------------------------------------------------"
    log_message "Inspecting log $LOG using $INSPECT for $INITTAB and $FSTAB"
    log_message "-------------------------------------------------------------"

    # Tests named as 'fail-*' should not be inspected. They should fail
    # and panic, but they must have the right exit code.
    if [ -z "${INITTAB##$TESTDIR/qemu/*/fail-*}" -o -z "${FSTAB##$TESTDIR/qemu/*/fail-*}" ]; then
        KERNEL_PANIC=$(echo "$LOG_QEMU" | grep "Kernel panic")
        echo "$KERNEL_PANIC" | grep "$KERNEL_PANIC_OK" &> /dev/null
        if [ $QEMU_EXITCODE -ne 0 -o $? -ne 0 ]; then
            RESULT=1
            log_message "QEMU_EXITCODE $QEMU_EXITCODE"
            log_message "Unexpected exit code for failure test. QEMU output copied below:"
            log_message "-------------------------------------------------------------"
            log_message "$LOG_QEMU"
            log_message "-------------------------------------------------------------"
            log_message ""
            log_message "init log:"
            log_message "-------------------------------------------------------------"
            log_message "$(cat "$LOG")"
        else
            log_message "Failure test OK"
        fi

        log_message "-------------------------------------------------------------"
        return $RESULT
    fi

    # Clean variables that maybe set (or not) on $INSPECT
    EXPECT_IN_ORDER=()
    EXPECT=()
    NOT_EXPECT=()
    source $INSPECT

    # If qemu finished with timeout, this test is also a failure
    if [ $QEMU_EXITCODE -eq 124 ]; then
        log_message "FAIL: QEMU finished by timeout"
        RESULT=1
    elif [ $QEMU_EXITCODE -ne 0 ]; then
        log_message "QEMU exited abnormaly. QEMU output copied below:"
        log_message "-------------------------------------------------------------"
        log_message "$LOG_QEMU"
        log_message "-------------------------------------------------------------"
        RESULT=1
        return $RESULT
    fi

    # Ensure entries on `$EXPECT_IN_ORDER` variable appear in order
    # on log file
    LAST_LINE=0
    for CHECK in "${EXPECT_IN_ORDER[@]}"; do
        STR=$(grep -n "$CHECK" "$LOG")

        if [ $? -ne 0 ]; then
            log_message "FAIL: Could not find '$CHECK' on '$LOG'"
            RESULT=1
        else
            LINE=$(echo "$STR" | cut -d ':' -f 1)

            if [ $LINE -le $LAST_LINE ]; then
                log_message "FAIL: Occurence of '$CHECK' before expected"
                RESULT=1
            fi
            LAST_LINE="$LINE"
        fi
    done

    # Ensure entries on `$EXPECT` variable simply appear, no order expected
    for CHECK in "${EXPECT[@]}"; do
        STR=$(grep -n "$CHECK" "$LOG")

        if [ $? -ne 0 ]; then
            log_message "FAIL: Could not find '$CHECK' on '$LOG'"
            RESULT=1
        fi
    done

    # Ensure entries on `$NOT_EXPECT` variable do not appear
    for CHECK in "${NOT_EXPECT[@]}"; do
        STR=$(grep -n "$CHECK" "$LOG")

        if [ $? -eq 0 ]; then
            log_message "FAIL: Found unexepected '$CHECK' on '$LOG'"
            RESULT=1
        fi
    done


    # Ensure no IS_RUNNING failed
    FAILURES=$(grep -n "IS RUNNING FAIL" "$LOG")
    if [ -n "$FAILURES" ]; then
        for STR in "${FAILURES[@]}"; do
            log_message "Process that should be running wasn't: $STR"
            RESULT=1
        done
    fi

    # Ensure no IS_NOT_RUNNING failed
    FAILURES=$(grep -n "IS NOT RUNNING FAIL" "$LOG")
    if [ -n "$FAILURES" ]; then
        for STR in "${FAILURES[@]}"; do
            log_message "Process that shouldn't be running was: $STR"
            RESULT=1
        done
    fi

    log_message ""

    if [ $RESULT -ne 0 ]; then
        log_message "Errors found during inpection. See previous messages for detail."
        log_message "Log file '$LOG' copied below:"
        log_message "-------------------------------------------------------------"
        log_message "$(cat "$LOG")"
    else
        log_message "Tests OK"
    fi

    log_message "-------------------------------------------------------------"
    log_message ""

    return $RESULT
}

function run_ordinary_test() {
    INITTAB=$1
    FSTAB=$2
    INSPECT="$INITTAB-inspect"

    echo "Testing inittab $INITTAB and $FSTAB"

    mount_test_fs $ROOT_FS
    update_inittab $INITTAB
    update_fstab $FSTAB
    umount_test_fs

    TMPFILE=$(mktemp -u)

    run_qemu $TMPFILE
    inspect $INSPECT $TMPFILE $INITTAB $FSTAB $?
    return $?
}

function inspect_valgrind() {
    INITTAB=$1
    FSTAB=$2
    RESULT=0
    VALGRIND_OK="==1== All heap blocks were freed -- no leaks are possible"

    log_message "-------------------------------------------------------------"
    log_message "Inspecting valgrind output for $INITTAB and $FSTAB"
    log_message "-------------------------------------------------------------"

    echo "$LOG_CONTAINER" | grep "$VALGRIND_OK" &> /dev/null
    if [ $? -eq 1 ]; then
        RESULT=1
        log_message ""
        log_message "Valgrind check not clean. Details below:"
        log_message "-------------------------------------------------------------"
        log_message "$LOG_CONTAINER"
    else
        log_message "Valgrind check OK"
    fi

    log_message "-------------------------------------------------------------"
    log_message ""

    return $RESULT
}

function inspect_asan() {
    INITTAB=$1
    FSTAB=$2
    ASAN_EXITCODE=$3
    RESULT=0

    log_message "-------------------------------------------------------------"
    log_message "Inspecting ASAN output for $INITTAB and $FSTAB"
    log_message "-------------------------------------------------------------"

    # Normal exitcode when running ASAN is 1 - any other exitcode
    # means error, not only ASAN_EXIT_ERROR. Even 0 - it means that
    # ASAN didn't run at all. With ASAN enabled, reboot() never happens
    # so init exits with 1.
    if [ "$ASAN_EXITCODE" -ne 1 ]; then
        RESULT=1
        log_message ""
        log_message "ASAN check not clean. Details below:"
        log_message "-------------------------------------------------------------"
        log_message "$LOG_CONTAINER"
    else
        log_message "ASAN check OK"
    fi

    log_message "-------------------------------------------------------------"
    log_message ""

    return $RESULT
}

function inspect_fault_injection() {
    LOG=$1
    QEMU_EXITCODE=$2
    FAULT=$3
    INITTAB=$4
    FSTAB=$5
    RESULT=0

    # No need to inject faults on tests for specific faults
    case $INITTAB in
        $TESTDIR/qemu/fail-*)
        return $RESULT
        ;;
    esac

    log_message "-------------------------------------------------------------"
    log_message "Inspecting fault injection output for $INITTAB and $FSTAB"
    log_message "Faults: $FAULT"
    log_message "-------------------------------------------------------------"

    # Kernel panics should be fine if init couldn't start or end, but
    # only if init gracefuly exited with exit code 0x100 (EXIT_FAILURE).
    # Segfaults, for instance, are errors.
    KERNEL_PANIC=$(echo "$LOG_QEMU" | grep "Kernel panic")
    if [ $? -eq 0 ]; then
        echo "$KERNEL_PANIC" | grep "$KERNEL_PANIC_OK" &> /dev/null
        if [ $? -ne 0 ]; then
            log_message "FAIL: Unexpected cause for Kernel panic"
            RESULT=1
        fi
    fi

    # Timeout without kernel panic may indicate infinite loop,
    # that timeout is too short or maybe init couldn't start process
    # that shuts machine down. The last case should go once init has
    # a safe mode well defined, until there, we'll treat as error,
    # but with a WARNING message since it may be ok.
    if [ -z "$KERNEL_PANIC" ]; then
        if [ $QEMU_EXITCODE -eq 124 ]; then
            log_message "WARNING: QEMU finished by timeout."
            RESULT=1
        elif [ $QEMU_EXITCODE -ne 0 ]; then
            log_message "FAIL: QEMU exited abnormaly."
            RESULT=1
        fi
    fi

    if [ "$RESULT" -ne 0 ]; then
        log_message ""
        log_message "Fault injection test FAIL. QEMU log copied below: "
        log_message "-------------------------------------------------------------"
        log_message "$LOG_QEMU"
        log_message "-------------------------------------------------------------"
        log_message ""
        log_message "init log:"
        log_message "-------------------------------------------------------------"
        log_message "$(cat "$LOG")"
        log_message "-------------------------------------------------------------"
    else
        log_message "Fault injection test OK"
    fi

    log_message "-------------------------------------------------------------"
    log_message ""

    return $RESULT
}

function run_valgrind_test() {
    INITTAB=$1
    FSTAB=$2

    # FSTAB tests are (currently) not possible on container environment
    # (Couldn't find an easy way to replace qemu -hdX on systemd-nspawn)
    if [ "$INITTAB" == "$TESTDIR/qemu/fstab/default-inittab" ]; then
        echo "Skiping FSTAB test on container ($FSTAB)"
        return 0
    fi
    echo "Testing inittab $INITTAB and fstab $FSTAB with valgrind"

    mount_test_fs $ROOT_FS
    update_inittab $INITTAB
    update_fstab $FSTAB
    umount_test_fs

    TMPFILE=$(mktemp)

    run_valgrind_container $TMPFILE
    inspect_valgrind $INITTAB $FSTAB
    return $?
}

function run_asan_test() {
    INITTAB=$1
    FSTAB=$2

    # FSTAB tests are (currently) not possible on container environment
    # (Couldn't find an easy way to replace qemu -hdX on systemd-nspawn)
    if [ "$INITTAB" == "$TESTDIR/qemu/fstab/default-inittab" ]; then
        echo "Skiping FSTAB test on container ($FSTAB)"
        return 0
    fi
    echo "Testing inittab $INITTAB and $FSTAB with ASAN"

    mount_test_fs $ROOT_FS
    update_inittab $INITTAB
    update_fstab $FSTAB
    umount_test_fs

    TMPFILE=$(mktemp)

    run_asan_container $TMPFILE
    inspect_asan $INITTAB $FSTAB $?
    return $?
}

function run_fault_injection_test() {
    INITTAB=$1
    FSTAB=$2
    FAIL=0

    echo "Testing inittab $INITTAB and $FSTAB with fault injection"

    mount_test_fs $ROOT_FS
    update_inittab $INITTAB
    update_fstab $FSTAB
    umount_test_fs

    source $FAULT_DEFINITIONS
    for i in $(seq 1 $FAULT_INJECTION_REPEATS); do
        for FAULT in "${FAULTS[@]}"; do
            TMPFILE=$(mktemp -u)

            echo "  Testing with fault: $FAULT"

            run_qemu $TMPFILE "$LIBFIU_PRELOAD FIU_ENABLE=\"$FAULT\""
            inspect_fault_injection $TMPFILE $? "$FAULT" $INITTAB $FSTAB
            if [ $? -eq 1 ]; then
                FAIL=1
            fi
        done
    done

    return $FAIL
}

function run_tests() {
    ERRORS_FOUND=0
    RUN_TEST=$1

    echo "Running tests..."
    # First, run tests under $TESTDIR/qemu/inittab. Those
    # tests target inittab, so there are many of them, for a single
    # 'default-fstab'
    TESTS=$(ls $TESTDIR/qemu/inittab/*-inittab)
    for TEST in $TESTS; do
        $RUN_TEST $TEST "$TESTDIR/qemu/inittab/default-fstab"
        if [ $? -ne 0 ]; then
            ERRORS_FOUND=1
        fi
    done

    # Then, run tests under $TESTDIR/qemu/fstab. Those
    # tests target fstab, so there are many of them, for a single
    # 'default-inittab'
    TESTS=$(ls $TESTDIR/qemu/fstab/*-fstab)
    for TEST in $TESTS; do
        $RUN_TEST "$TESTDIR/qemu/fstab/default-inittab" $TEST
        if [ $? -ne 0 ]; then
            ERRORS_FOUND=1
        fi
    done

    if [ "$ERRORS_FOUND" -ne 0 ]; then
        echo "Some tests FAIL"
        echo "See $LOG_FILE for more details"
    else
        echo "All tests OK"
    fi

    return $ERRORS_FOUND
}

function extract_coverage_information() {
    echo "Extracting coverage information..."
    mount_test_fs $GCOV_FS

    # TODO during fault injection tests, mount may fail.
    # This can prevent 'gcov' partition from being mounted on target.
    # When this happens, gcov info will live on '/gcov/src' inside
    # rootfs.ext2. Ideally, we should merge them when generating
    # coverage report
    cp $QEMUDIR/mnt/src/*.gcda $SRCDIR

    umount_test_fs
}

function clean_coverage_information() {
    mount_test_fs $GCOV_FS
    sudo rm -rf $QEMUDIR/mnt/src
    umount_test_fs
}

for i in "$@"; do
    case $i in
        --extract-coverage-information)
            EXTRACT_COVERAGE=true
            shift
            ;;
        --clean-coverage-information)
            CLEAN_COVERAGE=true
            shift
            ;;
        valgrind)
            CHECK_VALGRIND=true
            DO_SETUP=true
            shift
            ;;
        asan)
            CHECK_ASAN=true
            DO_SETUP=true
            shift
            ;;
        ordinary)
            CHECK_ORDINARY=true
            DO_SETUP=true
            shift
            ;;
        fault-injection)
            FAULT_INJECTION=true
            DO_SETUP=true
            shift
            ;;
        *)
            echo "Unknown option \"$i\""
            exit 1
            ;;
    esac
done

if [ "$DO_SETUP" = true ]; then
    echo "Setting environment up"
    setup_environment
    truncate -s 0 $LOG_FILE
fi

if [ "$CHECK_ORDINARY" = true ]; then
    run_tests run_ordinary_test
fi

if [ "$CHECK_VALGRIND" = true ]; then
    run_tests run_valgrind_test
fi

if [ "$CHECK_ASAN" = true ]; then
    run_tests run_asan_test
fi

if [ "$FAULT_INJECTION" = true ]; then
    run_tests run_fault_injection_test
fi

if [ "$EXTRACT_COVERAGE" = true ]; then
    extract_coverage_information
fi

if [ "$CLEAN_COVERAGE" = true ]; then
    clean_coverage_information
fi
