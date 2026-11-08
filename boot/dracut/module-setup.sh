#!/bin/bash
check() { [[ -f /etc/ubuntu-reset/boot.conf ]]; }
depends() { echo 'rootfs-block'; }
installkernel() { instmods ext4 squashfs loop vfat nls_utf8 nls_ascii nls_cp437; }
install() {
    inst_simple /etc/ubuntu-reset/boot.conf /conf/ubuntu-reset.conf
    for asset in restore.sh reconcile.sh relocate.sh; do inst_simple "/usr/lib/ubuntu-reset/$asset" "/usr/lib/ubuntu-reset/$asset"; done
    inst_multiple rsync sha256sum du df stat find cat readlink grep sed awk mkdir rm sync sleep blkid losetup efibootmgr
    inst_hook pre-pivot 01 "$moddir/pre-pivot.sh"
}
