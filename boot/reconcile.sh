#!/bin/sh
# Shared by initramfs and directory-fixture integration tests.
# Callers must establish target identity and offline state BEFORE calling this function.
uf_reconcile() {
    uf_src=$1
    uf_dst=$2
    uf_boot_src=$3
    uf_boot_dst=$4
    if [ "$uf_src" = / ] || [ "$uf_dst" = / ]; then return 90; fi
    if [ ! -d "$uf_src/etc" ] || [ ! -f "$uf_src/etc/passwd" ]; then return 91; fi
    if [ ! -d "$uf_dst" ] || [ ! -d "$uf_boot_src" ] || [ ! -d "$uf_boot_dst" ]; then return 92; fi
    # Keep the pending request through a failed root restore, so the old recovery
    # initramfs can retry. Remove it only after both filesystems finish successfully.
    rsync -aHAXx --numeric-ids --checksum --delete-before \
        --exclude='/dev/***' --exclude='/proc/***' --exclude='/sys/***' \
        --exclude='/run/***' --exclude='/boot/firmware/***' --exclude='/boot/efi/***' \
        --exclude='/var/lib/ubuntu-reset/***' \
        "$uf_src/" "$uf_dst/" || return 93
    rsync -rt --checksum --delete-delay --delay-updates \
        "$uf_boot_src/" "$uf_boot_dst/" || return 94
}
