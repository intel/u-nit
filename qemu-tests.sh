#!/bin/sh

# Copyright (C) 2017 Intel Corporation
# SPDX-License-Identifier: MIT

TESTDIR=tests/data
QEMUDIR=tests/init-qemu
SRCDIR=src

LOG_FILE=qemu-tests.log

INIT_EXEC=init
SLEEP_TEST_EXEC=tests/sleep_test
SLEEP_CRASH_TEST_EXEC=tests/sleep_crash_test
IS_RUNNING_EXEC=tests/is_running
IS_NOT_RUNNING_EXEC=tests/is_not_running

ROOT_FS=rootfs.ext2
GCOV_FS=gcov.ext4

EXEC_TIMEOUT=60
LOG_QEMU=
LOG_CONTAINER=

RUNNING_COVERAGE=false
CHECK_VALGRIND=false

function log_message() {
    echo "$@" >> $LOG_FILE
}

function run_qemu() {
    LOG_INIT=$1
    LOG_QEMU=$(timeout -s TERM --foreground ${EXEC_TIMEOUT}  \
        qemu-system-x86_64 -machine q35,kernel_irqchip=split -m 512M -enable-kvm \
          -smp 4 -device intel-iommu,intremap=on,x-buggy-eim=on \
          -s -kernel $QEMUDIR/bzImage  \
          -cpu kvm64,-kvm_pv_eoi,-kvm_steal_time,-kvm_asyncpf,-kvmclock,+vmx \
          -hda $QEMUDIR/$ROOT_FS \
          -hdb $QEMUDIR/$GCOV_FS \
          -serial mon:stdio \
          -chardev file,id=char0,path=${LOG_INIT} -serial chardev:char0 \
          -append "root=/dev/sda rw console=ttyS0 iip=dhcp" \
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
    sudo cp $IS_RUNNING_EXEC $QEMUDIR/mnt/usr/bin/
    sudo cp $IS_NOT_RUNNING_EXEC $QEMUDIR/mnt/usr/bin/
    umount_test_fs

    if [ "$RUNNING_COVERAGE" = true ]; then
        mount_test_fs $GCOV_FS
        sudo rm -rf $QEMUDIR/mnt/src
        umount_test_fs
    fi
}

function update_inittab() {
    sudo cp $1 $QEMUDIR/mnt/etc/inittab
}

function inspect() {
    INSPECT=$1
    LOG=$2
    INITTAB=$3
    QEMU_EXITCODE=$4
    RESULT=0

    log_message "-------------------------------------------------------------"
    log_message "Inspecting log $LOG using $INSPECT for $INITTAB"
    log_message "-------------------------------------------------------------"

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
    INSPECT=${INITTAB/-inittab/-inspect}

    echo "Testing inittab $INITTAB"

    mount_test_fs $ROOT_FS
    update_inittab $INITTAB
    umount_test_fs

    TMPFILE=$(mktemp -u)

    run_qemu $TMPFILE
    inspect $INSPECT $TMPFILE $INITTAB $?
    return $?
}

function inspect_valgrind() {
    INITTAB=$1
    RESULT=0
    VALGRIND_OK="==1== All heap blocks were freed -- no leaks are possible"

    log_message "-------------------------------------------------------------"
    log_message "Inspecting valgrind output for $INITTAB"
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

function run_valgrind_test() {
    INITTAB=$1

    echo "Testing inittab $INITTAB with valgrind"

    mount_test_fs $ROOT_FS
    update_inittab $INITTAB
    umount_test_fs

    TMPFILE=$(mktemp)

    run_valgrind_container $TMPFILE
    inspect_valgrind $INITTAB
    return $?
}

function run_tests() {
    ERRORS_FOUND=0
    RUN_TEST=$1

    echo "Setting environment up"
    setup_environment
    truncate -s 0 $LOG_FILE

    echo "Running tests..."
    TESTS=$(ls $TESTDIR/qemu/*-inittab)
    for TEST in $TESTS; do
        $RUN_TEST $TEST
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

    cp $QEMUDIR/mnt/src/*.gcda $SRCDIR

    umount_test_fs
}

# TODO this can be a getopt
if [ "$1" = "--extract-coverage-information" ]; then
    RUNNING_COVERAGE=true
elif [ "$1" = "--run-and-check-valgrind" ]; then
    CHECK_VALGRIND=true
fi

if [ "$CHECK_VALGRIND" = true ]; then
    run_tests run_valgrind_test
else
    run_tests run_ordinary_test
fi

if [ "$RUNNING_COVERAGE" = true ]; then
    extract_coverage_information
fi
