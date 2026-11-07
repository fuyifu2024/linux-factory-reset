#!/bin/sh
# GitHub-hosted disposable runner ONLY. Boots an expendable VM and actually erases its test users.
set -eu
[ "${GITHUB_ACTIONS:-}" = true ] || { echo 'CI-only VM test'; exit 1; }
source=$1
package=$2
evidence=$3
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
[ -c /dev/kvm ] || { echo 'KVM unavailable: real boot NOT validated'; exit 1; }
cp --sparse=always "$source" "$work/target.raw"
truncate -s 32G "$work/recovery.raw"
recovery_loop=$(losetup --find --show "$work/recovery.raw")
mkfs.ext4 -q "$recovery_loop"
recovery_uuid=$(blkid -s UUID -o value "$recovery_loop")
mkdir "$work/recovery" "$work/target"
mount "$recovery_loop" "$work/recovery"
cp --sparse=always "$source" "$work/recovery/pristine.raw"
cp "$package" "$work/recovery/tool.deb"
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
for file in "$work/target/etc/fstab" "$work/target/boot/grub/grub.cfg" "$work/target-boot/EFI/debian/grub.cfg"; do
    sed -i "s/$old_fs_uuid/$new_fs_uuid/g; s/$old_root_partuuid/$new_root_partuuid/g; s/$old_boot_partuuid/$new_boot_partuuid/g" "$file"
done
umount "$work/target-boot"
mkdir -p "$work/target/usr/local/sbin" "$work/target/etc/systemd/system/multi-user.target.wants"
ln -sf /dev/null "$work/target/etc/systemd/system/systemd-firstboot.service"
printf 'REC_UUID=%s\n' "$recovery_uuid" >"$work/target/etc/lfr-qa.conf"
cat >"$work/target/usr/local/sbin/lfr-qa" <<'GUEST'
#!/bin/bash
set -Eeuo pipefail
trap 'echo LFR_VM_PREPARATION_FAIL >/dev/ttyS0; systemctl --no-block poweroff' ERR
source /etc/lfr-qa.conf
mkdir -p /mnt/recovery
mount -t ext4 "UUID=$REC_UUID" /mnt/recovery
apt-get update
DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends /mnt/recovery/tool.deb
useradd --create-home --shell /bin/bash lfr_old_user
mkdir -p /opt/lfr-extra
printf 'old private data\n' >/root/lfr-private
printf 'old home\n' >/home/lfr_old_user/private
printf 'extra software\n' >/opt/lfr-extra/unwanted
# Observe the actual production first-user body without altering it; only the test profile gets this wrapper.
{
cat <<'OBSERVER'
#!/bin/bash
set -euo pipefail
exec > >(tee /dev/ttyS0) 2>&1
if getent passwd lfr_old_user || test -e /home/lfr_old_user || test -e /root/lfr-private || test -e /opt/lfr-extra/unwanted; then
    echo LFR_VM_OLD_DATA_SURVIVED; systemctl --no-block poweroff; exit 1
fi
systemd-run --unit=lfr-qa-observer --property=StandardOutput=tty --property=TTYPath=/dev/ttyS0 /bin/bash -c '
    for ((i=0;i<180;i++)); do
        if test -f /var/lib/ubuntu-reset-firstboot-complete; then
            if getent passwd lfrqatest && id -nG lfrqatest | grep -qw sudo && passwd -S root | grep -q " L "; then echo PASS >/var/lib/lfr-qa-result; sync; echo LFR_VM_RESTORE_PASS; else echo FAIL >/var/lib/lfr-qa-result; sync; echo LFR_VM_NEW_USER_FAIL; fi
            systemctl --no-block poweroff; exit
        fi
        sleep 1
    done
    echo LFR_VM_SETUP_TIMEOUT; systemctl --no-block poweroff; exit 1'
echo LFR_VM_AWAITING_NEW_USER
OBSERVER
sed '1d' /usr/lib/ubuntu-reset/firstboot.sh
} >/tmp/lfr-observed-firstboot
install -m 755 /tmp/lfr-observed-firstboot /usr/lib/ubuntu-reset/firstboot.sh
linux-factory-reset prepare-image --file /mnt/recovery/pristine.raw --storage /mnt/recovery
udevadm settle
echo LFR_VM_READY_TO_ARM >/dev/ttyS0
linux-factory-reset arm --confirm ERASE-ALL-USERS --reboot
GUEST
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
cp /usr/share/OVMF/OVMF_VARS_4M.fd "$work/vars.fd"
qemu-system-x86_64 -machine q35,accel=kvm -cpu host -m 2048 -smp 2 -display none -vga std \
    -drive if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd \
    -drive if=pflash,format=raw,file="$work/vars.fd" \
    -drive if=virtio,format=raw,file="$work/target.raw" -drive if=virtio,format=raw,file="$work/recovery.raw" \
    -netdev user,id=n -device virtio-net-pci,netdev=n -serial file:"$evidence/vm-serial.txt" \
    -qmp unix:"$work/qmp.sock",server=on,wait=off -monitor none >"$evidence/qemu.txt" 2>&1 &
vm_pid=$!
node tests/drive-firstboot.mjs "$work/qmp.sock" "$evidence/vm-serial.txt"
# The serial getty can reset its tty during shutdown; verify a synced result on disk too.
kill "$vm_pid" 2>/dev/null || :
wait "$vm_pid" 2>/dev/null || :
vm_pid=''
target_loop=$(losetup --find --show --read-only --partscan "$work/target.raw")
udevadm settle
target_root=$(lsblk -nrpo PATH,FSTYPE "$target_loop" | awk '$2 == "ext4" {print $1}')
mount -t ext4 -o ro,noload "$target_root" "$work/target"
cp "$work/target/var/lib/lfr-qa-result" "$evidence/guest-result.txt"
grep -qx PASS "$evidence/guest-result.txt"
grep -q '^lfrqatest:' "$work/target/etc/passwd"
if grep -q '^lfr_old_user:' "$work/target/etc/passwd"; then exit 1; fi
test ! -e "$work/target/home/lfr_old_user"
test ! -e "$work/target/root/lfr-private"
test ! -e "$work/target/opt/lfr-extra/unwanted"
test ! -e "$work/target/var/lib/ubuntu-reset/request"
echo 'Persistent restored-state verification passed.'
