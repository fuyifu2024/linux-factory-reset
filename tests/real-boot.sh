#!/bin/sh
# GitHub-hosted disposable runner ONLY. Boots an expendable VM and actually erases its test users.
set -eu
[ "${GITHUB_ACTIONS:-}" = true ] || { echo 'CI-only VM test'; exit 1; }
source=$1
package=$2
evidence=$3
mode=${4:-local}
distro=${5:-debian}
fault=${6:-none}
case "$fault" in none|failure|powercut) ;; *) exit 1 ;; esac
case "$mode" in local|automatic) ;; *) exit 1 ;; esac
work=$(mktemp -d /var/lib/linux-reset-vm.XXXXXX)
target_loop='' recovery_loop='' vm_pid=''
cleanup() {
    if [ -n "$vm_pid" ]; then kill "$vm_pid" 2>/dev/null || :; wait "$vm_pid" 2>/dev/null || :; fi
    umount "$work/target-boot" 2>/dev/null || :
    umount "$work/target" 2>/dev/null || :
    umount "$work/recovery" 2>/dev/null || :
    [ -z "$target_loop" ] || losetup --detach "$target_loop" 2>/dev/null || :
    [ -z "$recovery_loop" ] || losetup --detach "$recovery_loop" 2>/dev/null || :
    rm -rf "$work"
}
trap cleanup EXIT HUP INT TERM
arch=${SOURCE_ARCH:-amd64}
if [ "$arch" = amd64 ]; then [ -c /dev/kvm ] || { echo 'KVM unavailable'; exit 1; }; fi
cp --sparse=always "$source" "$work/target.raw"
truncate -s 32G "$work/recovery.raw"
recovery_loop=$(losetup --find --show "$work/recovery.raw")
mkfs.ext4 -q "$recovery_loop"
recovery_uuid=$(blkid -s UUID -o value "$recovery_loop")
mkdir "$work/recovery" "$work/target"
mount "$recovery_loop" "$work/recovery"
if [ "$mode" = local ]; then cp --sparse=always "$source" "$work/recovery/pristine.raw"; fi
if [ "$distro" = fedora ]; then cp "$package" "$work/recovery/tool.rpm"; else cp "$package" "$work/recovery/tool.deb"; fi
cp tests/vm-guest.sh tests/vm-observer.sh tests/drive-gui.mjs "$work/recovery/"
printf 'REC_UUID=%s\nREC_MODE=%s\nREC_DISTRO=%s\nREC_FAULT=%s\nREC_CYCLES=%s\nREC_GUI=%s\n' "$recovery_uuid" "$mode" "$distro" "$fault" "${RESET_CYCLES:-1}" "${GUI_ACCEPTANCE:-no}" >"$work/recovery/lfr-qa.conf"
umount "$work/recovery"
losetup --detach "$recovery_loop"; recovery_loop=''
target_loop=$(losetup --find --show --partscan "$work/target.raw")
udevadm settle
old_fs_uuid=$(lsblk -nrpo FSTYPE,UUID "$target_loop" | awk '$1 == "ext4" {print $2}')
old_root_partuuid=$(lsblk -nrpo FSTYPE,PARTUUID "$target_loop" | awk '$1 == "ext4" {print $2}')
old_boot_partuuid=$(lsblk -nrpo FSTYPE,PARTUUID "$target_loop" | awk '$1 == "vfat" {print $2}')
sgdisk -G "$target_loop"
partprobe "$target_loop"
udevadm settle
target_root=$(lsblk -nrpo PATH,FSTYPE "$target_loop" | awk '$2 == "ext4" {print $1}')
[ -n "$target_root" ] && [ "$(printf '%s\n' "$target_root" | wc -l)" -eq 1 ]
e2fsck -fy "$target_root" || [ "$?" -eq 1 ]
tune2fs -U random "$target_root"
new_fs_uuid=$(blkid -s UUID -o value "$target_root")
new_root_partuuid=$(blkid -s PARTUUID -o value "$target_root")
target_boot=$(lsblk -nrpo PATH,FSTYPE "$target_loop" | awk '$2 == "vfat" {print $1}')
new_boot_partuuid=$(blkid -s PARTUUID -o value "$target_boot")
[ "$new_fs_uuid" != "$old_fs_uuid" ] && [ "$new_root_partuuid" != "$old_root_partuuid" ]
mount -t ext4 "$target_root" "$work/target"
mkdir -p "$work/target-boot"
mount -t vfat "$target_boot" "$work/target-boot"
for file in "$work/target/etc/fstab" "$work/target/boot/grub/grub.cfg" "$work/target/boot/grub2/grub.cfg" "$work/target-boot/EFI/BOOT/grub.cfg" "$work/target-boot/EFI/$distro/grub.cfg"; do
    [ -f "$file" ] || continue
    sed -i "s/$old_fs_uuid/$new_fs_uuid/g; s/$old_root_partuuid/$new_root_partuuid/g; s/$old_boot_partuuid/$new_boot_partuuid/g" "$file"
done
umount "$work/target-boot"
mkdir -p "$work/target/usr/local/sbin" "$work/target/etc/systemd/system/multi-user.target.wants"
ln -sf /dev/null "$work/target/etc/systemd/system/systemd-firstboot.service"
printf 'REC_UUID=%s\nREC_MODE=%s\nREC_DISTRO=%s\nREC_FAULT=%s\nREC_CYCLES=%s\nREC_GUI=%s\n' "$recovery_uuid" "$mode" "$distro" "$fault" "${RESET_CYCLES:-1}" "${GUI_ACCEPTANCE:-no}" >"$work/target/etc/lfr-qa.conf"
cp tests/vm-guest.sh "$work/target/usr/local/sbin/lfr-qa"
if [ "$arch" = arm64 ]; then sed -i s/ttyS0/ttyAMA0/g "$work/target/usr/local/sbin/lfr-qa"; fi
chmod 755 "$work/target/usr/local/sbin/lfr-qa"
cat >"$work/target/etc/systemd/system/lfr-qa.service" <<'UNIT'
[Unit]
Description=Disposable CI VM reset driver
Wants=network-online.target
After=network-online.target
[Service]
Type=oneshot
ExecStart=/usr/local/sbin/lfr-qa
StandardOutput=journal+console
StandardError=journal+console
TimeoutStartSec=900
[Install]
WantedBy=multi-user.target
UNIT
ln -s ../lfr-qa.service "$work/target/etc/systemd/system/multi-user.target.wants/lfr-qa.service"
sync
umount "$work/target"
losetup --detach "$target_loop"; target_loop=''
if [ "$arch" = arm64 ]; then cp /usr/share/AAVMF/AAVMF_VARS.fd "$work/vars.fd"; else cp /usr/share/OVMF/OVMF_VARS_4M.fd "$work/vars.fd"; fi
start_vm() {
if [ "$arch" = arm64 ]; then
    accel=tcg; cpu=max
    if [ -c /dev/kvm ]; then accel=kvm; cpu=host; fi
    set -- qemu-system-aarch64 -machine "virt,accel=$accel,gic-version=3" -cpu "$cpu" -device virtio-gpu-pci -device qemu-xhci -device usb-kbd
    code=/usr/share/AAVMF/AAVMF_CODE.fd
else
    set -- qemu-system-x86_64 -machine q35,accel=kvm -cpu host -vga std
    code=/usr/share/OVMF/OVMF_CODE_4M.fd
fi
"$@" -m 3072 -smp 2 -display none \
    -drive if=pflash,format=raw,readonly=on,file="$code" \
    -drive if=pflash,format=raw,file="$work/vars.fd" \
    -drive if=virtio,format=raw,file="$work/target.raw" -drive if=virtio,format=raw,file="$work/recovery.raw" \
    -netdev user,id=n -device virtio-net-pci,netdev=n -serial file:"$evidence/vm-serial.txt" \
    -qmp unix:"$work/qmp.sock",server=on,wait=off -monitor none >"$evidence/qemu.txt" 2>&1 &
vm_pid=$!
}
start_vm
set +e
node tests/drive-firstboot.mjs "$work/qmp.sock" "$evidence/vm-serial.txt" "$fault"
status=$?
set -e
if [ "$status" -eq 42 ] && [ "$fault" = powercut ]; then
    kill -KILL "$vm_pid"; wait "$vm_pid" 2>/dev/null || :; vm_pid=''
    cp "$evidence/vm-serial.txt" "$evidence/before-power-cut.txt"
    : >"$evidence/vm-serial.txt"
    start_vm
    node tests/drive-firstboot.mjs "$work/qmp.sock" "$evidence/vm-serial.txt" rollback
else
    [ "$status" -eq 0 ]
fi
# The serial getty can reset its tty during shutdown; verify a synced result on disk too.
kill "$vm_pid" 2>/dev/null || :
wait "$vm_pid" 2>/dev/null || :
vm_pid=''
target_loop=$(losetup --find --show --read-only --partscan "$work/target.raw")
udevadm settle
target_root=$(lsblk -nrpo PATH,FSTYPE "$target_loop" | awk '$2 == "ext4" {print $1}')
mount -t ext4 -o ro,noload "$target_root" "$work/target"
cp "$work/target/var/lib/lfr-qa-result" "$evidence/guest-result.txt"
if [ -f "$work/target/var/lib/lfr-qa-diagnostics" ]; then cp "$work/target/var/lib/lfr-qa-diagnostics" "$evidence/guest-diagnostics.txt"; fi
grep -qx PASS "$evidence/guest-result.txt"
if [ "$fault" = none ]; then
grep -q '^lfrqatest:' "$work/target/etc/passwd"
if grep -q '^lfr_old_user:' "$work/target/etc/passwd"; then exit 1; fi
test ! -e "$work/target/home/lfr_old_user"
test ! -e "$work/target/root/lfr-private"
test ! -e "$work/target/opt/lfr-extra/unwanted"
test ! -e "$work/target/var/lib/ubuntu-reset/request"
else
    grep -q '^lfr_old_user:' "$work/target/etc/passwd"
    grep -qx 'old private data' "$work/target/root/lfr-private"
    grep -qx 'extra software' "$work/target/opt/lfr-extra/unwanted"
fi
# No temporary personal backup may remain after either successful outcome.
recovery_loop=$(losetup --find --show --read-only "$work/recovery.raw")
mount -t ext4 -o ro,noload "$recovery_loop" "$work/recovery"
for rescue in "$work/recovery/ubuntu-reset"/rescue-*; do
    test ! -e "$rescue/previous"; test ! -e "$rescue/active"
    test ! -e "$rescue/initrd"; test ! -e "$rescue/vmlinuz"
done
for cycle in $(seq 1 "${RESET_CYCLES:-1}"); do
    if [ "$fault" = none ]; then
        cp "$work/recovery/cycle-$cycle-result" "$evidence/cycle-$cycle-result.txt"
        cp "$work/recovery/cycle-$cycle-diagnostics" "$evidence/cycle-$cycle-diagnostics.txt"
        grep -qx PASS "$evidence/cycle-$cycle-result.txt"
    fi
done
if [ -d "$work/recovery/gui-evidence" ]; then cp -a "$work/recovery/gui-evidence" "$evidence/"; fi
echo 'Persistent restored-state verification passed.'
