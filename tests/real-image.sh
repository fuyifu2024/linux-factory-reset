#!/bin/sh
# MANUAL CI ONLY. Mounts a verified official image read-only on a disposable remote runner.
# This script must never be run on the developer/user's real machine.
set -eu
app=$1
evidence=$2
assets=$3
mkdir -p "$evidence"
work=$(mktemp -d /var/lib/linux-reset-ci.XXXXXX)
loop=''
cleanup() {
    umount "$work/root" 2>/dev/null || :
    umount "$work/boot" 2>/dev/null || :
    [ -z "$loop" ] || losetup --detach "$loop" 2>/dev/null || :
    rm -rf "$work"
}
trap cleanup EXIT HUP INT TERM
base=https://cloud.debian.org/images/cloud/trixie/latest/
curl --fail --location --proto '=https' --proto-redir '=https' --max-filesize 4194304 --output "$work/SHA512SUMS" "${base}SHA512SUMS"
sleep 6
source_arch=${5:-amd64}
case "$source_arch" in amd64) platform=x86_64-uefi ;; arm64) platform=arm64-uefi ;; *) exit 1 ;; esac
filename="debian-13-nocloud-$source_arch.qcow2"
expected=$(awk -v file="$filename" '$2 == file {print $1}' "$work/SHA512SUMS")
[ "${#expected}" -eq 128 ] || { echo 'No unambiguous official checksum'; exit 1; }
printf 'Source: %s%s\nSHA512: %s\nTrust: official HTTPS checksum; detached signature not checked by this probe\n' "$base" "$filename" "$expected" >"$evidence/source.txt"
curl --fail --location --proto '=https' --proto-redir '=https' --max-filesize 2147483648 --output "$work/source.qcow2" "$base$filename"
printf '%s  %s\n' "$expected" "$work/source.qcow2" | sha512sum --check
qemu-img info --output=json -f qcow2 "$work/source.qcow2" >"$evidence/image-info.txt"
qemu-img convert -f qcow2 -O raw "$work/source.qcow2" "$work/disk.raw"
loop=$(losetup --find --show --read-only --partscan "$work/disk.raw")
udevadm settle
lsblk --json -o PATH,FSTYPE,PARTLABEL,UUID "$loop" >"$evidence/partitions.txt"
roots=$(lsblk -nrpo PATH,FSTYPE "$loop" | awk '$2 == "ext4" {print $1}')
boots=$(lsblk -nrpo PATH,FSTYPE "$loop" | awk '$2 == "vfat" {print $1}')
[ "$(printf '%s\n' "$roots" | wc -l)" -eq 1 ] && [ -n "$roots" ] || { echo 'Source does not have one ext4 root'; exit 1; }
[ "$(printf '%s\n' "$boots" | wc -l)" -eq 1 ] && [ -n "$boots" ] || { echo 'Source does not have one FAT ESP'; exit 1; }
mkdir "$work/root" "$work/boot"
mount -t ext4 -o ro,noload,nosuid,nodev,noexec "$roots" "$work/root"
mount -t vfat -o ro,nosuid,nodev,noexec "$boots" "$work/boot"
# Public image metadata only. Never include /etc/shadow or other password material.
cat "$work/root/usr/lib/os-release" "$work/root/etc/fstab" >"$evidence/layout.txt"
find "$work/boot" -maxdepth 4 -type f >>"$evidence/layout.txt"
ls -l "$work/root/boot" >>"$evidence/layout.txt"
for config in "$work/root/boot/grub/grub.cfg" "$work/boot/EFI/debian/grub.cfg"; do
    [ ! -f "$config" ] || { printf '\nConfig: %s\n' "$config"; cat "$config"; }
done >"$evidence/grub.txt"
mkdir -p /usr/lib/ubuntu-reset
cp "$assets/firstboot.sh" "$assets/ubuntu-reset-firstboot.service" /usr/lib/ubuntu-reset/
set +e
"$app" make-profile --root "$work/root" --boot "$work/boot" --platform "$platform" --output "$work/profile.squashfs" >"$evidence/profile-validation.txt" 2>&1
result=$?
set -e
cat "$evidence/profile-validation.txt"
printf '\nExit: %s\nNo reset was armed or executed.\n' "$result" >>"$evidence/profile-validation.txt"
if [ "$result" -eq 0 ] && [ -n "${4:-}" ]; then
    umount "$work/root"
    umount "$work/boot"
    losetup --detach "$loop"; loop=''
    sh tests/real-boot.sh "$work/disk.raw" "$4" "$evidence"
fi
exit "$result"
