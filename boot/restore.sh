#!/bin/sh
# Runs before switch_root, while no processes from the installed OS are running.
set -u
# Prefer the explicitly bundled tools over a distro's earlier BusyBox applets.
PATH=/usr/lib/ubuntu-reset/bin:/usr/bin:/usr/sbin:/bin:/sbin
export PATH
[ -f /conf/ubuntu-reset.conf ] || exit 0
. /conf/ubuntu-reset.conf
. /usr/lib/ubuntu-reset/reconcile.sh
. /usr/lib/ubuntu-reset/relocate.sh
UF_TARGET=${1:-/root}
UF_RESCUE="/run/uf-store/ubuntu-reset/rescue-$UF_ID"
UF_RESCUE_MODE=0
case " $(cat /proc/cmdline) " in *" lfr.rescue=$UF_ID "*) UF_RESCUE_MODE=1 ;; esac
if [ "$UF_RESCUE_MODE" -eq 0 ] && [ ! -e "$UF_TARGET/var/lib/ubuntu-reset/request" ]; then exit 0; fi
UF_WRITING=0

uf_phase() {
    printf '%s\n' "$1" >"$UF_RESCUE/phase.new" || return 1
    sync
    mv "$UF_RESCUE/phase.new" "$UF_RESCUE/phase" || return 1
    sync
}
uf_normal_order() {
    [ -f "$UF_RESCUE/original-order" ] || return 0
    uf_order=$(cat "$UF_RESCUE/original-order") || return 1
    case "$uf_order" in ''|*[!0-9A-Fa-f,]*) return 1 ;; esac
    mkdir -p /sys/firmware/efi/efivars
    mount -t efivarfs efivarfs /sys/firmware/efi/efivars 2>/dev/null || :
    efibootmgr --bootorder "$uf_order" || return 1
}
uf_finish() {
    mount -t ext4 -o remount,rw "$UF_ROOT_DEV" "$UF_TARGET" || return 1
    if [ "$UF_PLATFORM" != raspi-arm64 ]; then
        if [ "$1" = ROLLED_BACK ]; then
            # The old files are durable. A second interruption during cleanup
            # must finish cleanup rather than read a partly deleted backup.
            UF_WRITING=0
            uf_phase ROLLED_BACK || return 1
        fi
        if [ "$1" = ROLLED_BACK ] || [ "$1" = CANCELLED ]; then
            rm -rf "$UF_RESCUE/previous" "$UF_RESCUE/backup-ready" || return 1
            sync
        fi
    fi
    rm -rf "$UF_TARGET/var/lib/ubuntu-reset" || return 1
    sync
    if [ "$UF_PLATFORM" != raspi-arm64 ]; then
        # Retain the independent rescue loader until the transaction is durable.
        # If restoring BootOrder fails, its inactive path chainloads the normal loader.
        printf '%s\n' "$1" >"$UF_RESCUE/result" || return 1
        rm -f "$UF_RESCUE/active" || return 1
        sync
        if uf_normal_order; then
            uf_entry=$(cat "$UF_RESCUE/entry") || return 1
            case "$uf_entry" in ''|*[!0-9A-Fa-f]*) return 1 ;; esac
            if efibootmgr --bootnum "$uf_entry" --delete-bootnum; then
                rm -rf /run/uf-boot/EFI/LinuxFactoryReset || return 1
            fi
        else
            echo 'Select the normal OS entry if firmware ignores BootOrder' >/dev/console
        fi
        # A host-generated initramfs can contain host-specific configuration.
        # Its independent copy is temporary and must not survive a completed reset.
        rm -f "$UF_RESCUE/vmlinuz" "$UF_RESCUE/initrd" || return 1
        sync
    fi
    return 0
}
uf_rollback() {
    [ -f "$UF_RESCUE/backup-ready" ] || return 1
    echo 'Linux Factory Reset: restoring previous system after interruption/failure.' >/dev/console
    mount -t ext4 -o remount,rw "$UF_ROOT_DEV" "$UF_TARGET" || return 1
    uf_reconcile "$UF_RESCUE/previous/root" "$UF_TARGET" "$UF_RESCUE/previous/boot" /run/uf-boot || return 1
    sync
    uf_finish ROLLED_BACK || return 1
    umount /run/uf-boot || return 1
    mount -t ext4 -o remount,ro "$UF_ROOT_DEV" "$UF_TARGET" || return 1
    echo 'LFR_ROLLBACK_COMPLETE' >/dev/console
    reboot -f
    return 1
}
uf_commit() {
    # A durable commit decision precedes deletion of the temporary personal backup.
    # An interrupted cleanup resumes cleanup, never a half-deleted rollback copy.
    UF_WRITING=0
    if [ "$UF_PLATFORM" != raspi-arm64 ]; then
        uf_phase COMMITTED || return 1
        rm -rf "$UF_RESCUE/previous" "$UF_RESCUE/backup-ready" || return 1
        sync
    fi
    uf_finish RESTORED || return 1
    sync
    umount /run/uf-boot || return 1
    mount -t ext4 -o remount,ro "$UF_ROOT_DEV" "$UF_TARGET" || return 1
    reboot -f
    return 1
}
uf_fail() {
    echo "Linux Factory Reset STOPPED: $*" >/dev/console
    if [ "$UF_WRITING" -eq 1 ] && [ "$UF_PLATFORM" != raspi-arm64 ]; then
        uf_rollback || echo 'Rollback did not finish; keep the recovery disk attached and use its rescue entry.' >/dev/console
    fi
    # Never continue booting partially restored files, or create an automatic reboot loop.
    while :; do
        echo 'Recovery shell. Independent UEFI rescue remains available after restarting.' >/dev/console
        sh </dev/console >/dev/console 2>&1
        sleep 1
    done
}

case "$UF_TARGET" in /root|/sysroot) ;; *) uf_fail 'Unexpected root mount point' ;; esac
UF_REQUEST="$UF_TARGET/var/lib/ubuntu-reset/request"
UF_DISTRO=${UF_DISTRO:-ubuntu}
case "$UF_DISTRO" in ubuntu|debian|fedora) ;; *) uf_fail 'Unknown target distribution' ;; esac
for component in "$UF_TARGET/var" "$UF_TARGET/var/lib" "$UF_TARGET/var/lib/ubuntu-reset" "$UF_REQUEST"; do
    [ ! -L "$component" ] || uf_fail 'Symlink in reset request path'
done
if [ "$UF_RESCUE_MODE" -eq 0 ]; then
[ -f "$UF_REQUEST" ] || uf_fail 'Invalid reset request'
[ "$(stat -c '%u:%a' "$UF_REQUEST")" = '0:600' ] || uf_fail 'Unsafe reset request permissions'
[ "$(cat "$UF_REQUEST")" = "$UF_ID" ] || uf_fail 'Request does not match the recovery initramfs'
fi

UF_ROOT_DEV=$(uf_device "$UF_ROOT") || uf_fail 'Ambiguous/missing target root UUID'
UF_BOOT_DEV=$(uf_device "$UF_BOOT") || uf_fail 'Ambiguous/missing boot UUID'
if [ "$UF_ROOT" = "$UF_BOOT" ] || [ "$UF_ROOT" = "$UF_STORE" ] || [ "$UF_BOOT" = "$UF_STORE" ]; then
    uf_fail 'Overlapping devices'
fi
UF_MOUNTED_ROOT=$(awk -v target="$UF_TARGET" '$2 == target {print $1}' /proc/mounts)
[ "$(readlink -f "$UF_MOUNTED_ROOT")" = "$(readlink -f "$UF_ROOT_DEV")" ] || uf_fail 'Running root UUID does not match the target'
[ "$(blkid -s TYPE -o value "$UF_ROOT_DEV")" = ext4 ] || uf_fail 'Root is not ext4'
[ "$(blkid -s TYPE -o value "$UF_BOOT_DEV")" = vfat ] || uf_fail 'Boot is not FAT'
[ -z "$(awk -v prefix="$UF_TARGET/" 'index($2,prefix) == 1 {print $2}' /proc/mounts)" ] || uf_fail 'Unexpected mounts inside target root'

echo 'Ubuntu Reset: verifying recovery media; do not remove power.' >/dev/console
UF_STORE_DEV=''
uf_tries=0
while [ "$uf_tries" -lt 30 ]; do
    UF_STORE_DEV=$(uf_device "$UF_STORE") && break
    uf_tries=$((uf_tries + 1))
    sleep 1
done
[ -n "$UF_STORE_DEV" ] || uf_fail 'Recovery partition is unavailable'
[ "$(blkid -s TYPE -o value "$UF_STORE_DEV")" = ext4 ] || uf_fail 'Recovery partition is not ext4'
mkdir -p /run/uf-store /run/uf-source /run/uf-boot
mount -t ext4 -o rw "$UF_STORE_DEV" /run/uf-store || uf_fail 'Cannot mount recovery partition'
if [ "$UF_PLATFORM" != raspi-arm64 ]; then
    if [ ! -d "$UF_RESCUE" ] || [ -L "$UF_RESCUE" ]; then uf_fail 'Missing independent rescue directory'; fi
    [ "$(stat -c '%u:%a' "$UF_RESCUE")" = 0:700 ] || uf_fail 'Unsafe rescue directory'
    [ "$(cat "$UF_RESCUE/active")" = "$UF_ID" ] || uf_fail 'Recovery transaction is not active'
fi
mount -t vfat -o iocharset=ascii,utf8=1,codepage=437 "$UF_BOOT_DEV" /run/uf-boot || uf_fail 'Cannot mount boot partition'
if [ -f "$UF_RESCUE/phase" ] && [ "$(cat "$UF_RESCUE/phase")" = COMMITTED ]; then
    uf_commit || uf_fail 'Cannot complete committed recovery cleanup'
fi
if [ -f "$UF_RESCUE/phase" ] && [ "$(cat "$UF_RESCUE/phase")" = ROLLED_BACK ]; then
    uf_finish ROLLED_BACK || uf_fail 'Cannot complete rollback cleanup'
    sync
    umount /run/uf-boot || uf_fail 'Cannot flush rollback boot filesystem'
    mount -t ext4 -o remount,ro "$UF_ROOT_DEV" "$UF_TARGET" || uf_fail 'Cannot flush rollback root'
    reboot -f
    uf_fail 'Reboot after rollback cleanup failed'
fi
if [ -f "$UF_RESCUE/phase" ] && [ "$(cat "$UF_RESCUE/phase")" = WRITING ]; then
    UF_WRITING=1
    uf_rollback || uf_fail 'Interrupted restoration could not be rolled back'
fi
case " $(cat /proc/cmdline) " in *" lfr.rollback=1 "*)
    if [ -f "$UF_RESCUE/backup-ready" ]; then uf_rollback || uf_fail 'Manual rollback failed'; fi
    uf_finish CANCELLED || uf_fail 'Cannot cancel before restoration'
    reboot -f; uf_fail 'Reboot failed'
;; esac
UF_IMAGE="/run/uf-store/$UF_FILE"
if [ ! -f "$UF_IMAGE" ] || [ -L "$UF_IMAGE" ]; then uf_fail 'Missing recovery profile'; fi
UF_ACTUAL=$(sha256sum "$UF_IMAGE") || uf_fail 'Cannot read recovery profile'
[ "${UF_ACTUAL%% *}" = "$UF_SHA" ] || uf_fail 'Recovery checksum mismatch'
modprobe loop || uf_fail 'Cannot load loop driver'
UF_LOOP=$(losetup --find --show --read-only "$UF_IMAGE") || uf_fail 'Cannot create read-only recovery loop'
mount -t squashfs -o ro "$UF_LOOP" /run/uf-source || uf_fail 'Cannot mount verified recovery profile'
if [ ! -f /run/uf-source/root/usr/lib/os-release ] || [ ! -f /run/uf-source/root/etc/passwd ]; then uf_fail 'Invalid root profile'; fi
uf_validate_profile "$UF_PLATFORM" "$UF_DISTRO" /run/uf-source/root /run/uf-source/boot || uf_fail 'Profile boot pointers are not safely relocatable'

# Check capacity before the first deletion.
uf_root_report=$(du -sk --apparent-size /run/uf-source/root) || uf_fail 'Cannot size profile'
uf_root_size=${uf_root_report%%[[:space:]]*}
uf_root_df=$(df -Pk "$UF_TARGET") || uf_fail 'Cannot size root partition'
uf_root_capacity=$(printf '%s\n' "$uf_root_df" | awk 'END {print $2}')
uf_boot_report=$(du -sk --apparent-size /run/uf-source/boot) || uf_fail 'Cannot size boot profile'
uf_boot_size=${uf_boot_report%%[[:space:]]*}
uf_boot_df=$(df -Pk /run/uf-boot) || uf_fail 'Cannot size boot partition'
uf_boot_capacity=$(printf '%s\n' "$uf_boot_df" | awk 'END {print $2}')
for uf_number in "$uf_root_size" "$uf_root_capacity" "$uf_boot_size" "$uf_boot_capacity"; do
    case "$uf_number" in ''|*[!0-9]*) uf_fail 'Invalid capacity report' ;; esac
done
[ "$uf_root_capacity" -gt "$((uf_root_size + 262144))" ] || uf_fail 'Root partition too small'
[ "$uf_boot_capacity" -gt "$((uf_boot_size + 16384))" ] || uf_fail 'Boot partition too small'
if [ "$UF_PLATFORM" != raspi-arm64 ]; then
    echo 'Linux Factory Reset: saving temporary rollback copy.' >/dev/console
    uf_size_report=$(du -sxk "$UF_TARGET") || uf_fail 'Cannot size previous system'
    uf_size=${uf_size_report%%[[:space:]]*}
    uf_free_report=$(df -Pk /run/uf-store) || uf_fail 'Cannot size recovery free space'
    uf_free=$(printf '%s\n' "$uf_free_report" | awk 'END {print $4}')
    case "$uf_size:$uf_free" in *[!0-9:]*|:*) uf_fail 'Invalid rollback capacity' ;; esac
    [ "$uf_free" -gt "$((uf_size + uf_boot_size + 1048576))" ] || uf_fail 'Recovery partition needs more space for a temporary rollback copy'
    mkdir -p "$UF_RESCUE/previous/root" "$UF_RESCUE/previous/boot" || uf_fail 'Cannot create rollback copy'
    uf_reconcile "$UF_TARGET" "$UF_RESCUE/previous/root" /run/uf-boot "$UF_RESCUE/previous/boot" || uf_fail 'Cannot complete rollback backup; target untouched'
    sync
    printf 'READY\n' >"$UF_RESCUE/backup-ready" || uf_fail 'Cannot seal backup'
    sync
    uf_phase WRITING || uf_fail 'Cannot record transaction phase'
fi
UF_WRITING=1
mount -t ext4 -o remount,rw "$UF_ROOT_DEV" "$UF_TARGET" || uf_fail 'Cannot make target writable'
printf 'RESTORING\n' >"$UF_TARGET/var/lib/ubuntu-reset/in-progress" || uf_fail 'Cannot record recovery phase'
sync
echo 'Ubuntu Reset: removing users/data and restoring initial system files.' >/dev/console
uf_reconcile /run/uf-source/root "$UF_TARGET" /run/uf-source/boot /run/uf-boot || uf_fail 'Filesystem restore failed'
uf_relocate "$UF_PLATFORM" "$UF_DISTRO" "$UF_TARGET" /run/uf-boot "$UF_ROOT" "$UF_BOOT" || uf_fail 'Cannot relocate root/EFI boot pointers'
sync
echo 'Ubuntu Reset complete. Restarting into initial setup.' >/dev/console
uf_commit || uf_fail 'Cannot finalize recovery; use independent rescue to finish cleanup'
