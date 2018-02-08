#!/usr/bin/env bash

# Copyright (C) 2017 Intel Corporation
# SPDX-License-Identifier: MIT

QEMUDIR=tests/init-qemu
IMAGE="$QEMUDIR/imagetmp"

if ! which mkosi &> /dev/null; then
    echo "Couldn't find mkosi. It is necessary to install mkosi to generate test image"
    exit 1
fi

if [ ! -d "$QEMUDIR/mnt" ]; then
    mkdir -p "$QEMUDIR/mnt"
fi

sudo mkosi -d debian -o "$IMAGE" -p valgrind,gcc,libfiu-dev,linux-image-amd64,dracut,vim,psmisc --password root

if [ $? -ne 0 ]; then
    echo "Could not create image, check previous errors"
    exit 1
fi

sudo chown "$USER" "$IMAGE"

if [ $? -ne 0 ]; then
    echo "Could not set image permission, check previous errors"
    exit 1
fi

OFFSET=$(partx -g -r --nr 1 -o START $IMAGE)
sudo mount -o loop,offset=$((OFFSET*512)) "$IMAGE" "$QEMUDIR/mnt"
KERNEL=$(ls "$QEMUDIR"/mnt/boot/vmlinuz-*-amd64)
INITRD=$(ls "$QEMUDIR"/mnt/boot/initrd.img-*-amd64)
sudo cp "$KERNEL" "$INITRD" "$QEMUDIR/"
sudo umount "$QEMUDIR/mnt"
sudo chown "$USER" $QEMUDIR/$(basename $INITRD) $QEMUDIR/$(basename $KERNEL)

mv "$IMAGE" "$QEMUDIR/rootfs.raw"

echo "Generating auxiliary filesystems"
dd if=/dev/zero of=$QEMUDIR/gcov.ext4 count=40960
mkfs.ext4 $QEMUDIR/gcov.ext4
cp $QEMUDIR/gcov.ext4 $QEMUDIR/test-fs.ext4
cp $QEMUDIR/gcov.ext4 $QEMUDIR/test-fs2.ext4

echo "Extracted following kernel and initrd, please update"
echo "qemu_tests.sh variables accordingly:"
echo "ROOT_FS=$(basename $KERNEL)"
echo "INITRD_FILE=$(basename $INITRD)"
