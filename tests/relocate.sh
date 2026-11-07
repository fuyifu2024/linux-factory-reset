#!/bin/sh
set -eu
# shellcheck source=boot/relocate.sh
. "$1"
blkid() { printf '%s\n' "$device_reply"; return "$device_status"; }
device_status=0
device_reply=/dev/vda1
[ "$(uf_device root-1111)" = /dev/vda1 ]
device_reply=''
if uf_device root-1111; then exit 1; fi
device_reply='/dev/vda1
/dev/vdb1'
if uf_device root-1111; then exit 1; fi
device_reply=/dev/vda1
device_status=2
if uf_device root-1111; then exit 1; fi
unset -f blkid
fixture=$(mktemp -d)
trap 'rm -rf "$fixture"' EXIT HUP INT TERM
for distro in ubuntu debian; do
    for platform in arm64-uefi x86_64-uefi; do
        root="$fixture/$distro-$platform/root"
        boot="$fixture/$distro-$platform/boot"
        mkdir -p "$root/etc" "$root/boot/grub" "$boot/EFI/$distro"
        printf 'UUID=@ROOT_UUID@ / ext4 defaults 0 1\nUUID=@BOOT_UUID@ /boot/efi vfat defaults 0 2\n' >"$root/etc/fstab"
        printf 'linux /boot/vmlinuz root=UUID=@ROOT_UUID@ ro\n' >"$root/boot/grub/grub.cfg"
        printf 'search.fs_uuid @ROOT_UUID@ root\n' >"$boot/EFI/$distro/grub.cfg"
        uf_relocate "$platform" "$distro" "$root" "$boot" 1111-2222 ABCD-1234
        grep -q 'UUID=1111-2222 /' "$root/etc/fstab"
        grep -q 'UUID=ABCD-1234 /boot/efi' "$root/etc/fstab"
        grep -q 'root=UUID=1111-2222' "$root/boot/grub/grub.cfg"
        grep -q '1111-2222' "$boot/EFI/$distro/grub.cfg"
        if uf_relocate "$platform" '../escape' "$root" "$boot" 1111-2222 ABCD-1234; then exit 1; fi
        if uf_relocate "$platform" "$distro" "$root" "$boot" 'bad/root' ABCD-1234; then exit 1; fi
    done
done
root="$fixture/debian-arm64-uefi/root"
boot="$fixture/debian-arm64-uefi/boot"
rm "$boot/EFI/debian/grub.cfg"
mkdir -p "$boot/EFI/ubuntu"
printf 'wrong distributor\n' >"$boot/EFI/ubuntu/grub.cfg"
if uf_relocate arm64-uefi debian "$root" "$boot" 1111-2222 ABCD-1234; then exit 1; fi
root="$fixture/pi/root"
boot="$fixture/pi/firmware"
mkdir -p "$root/etc" "$boot/current"
printf 'UUID=@ROOT_UUID@ / ext4 defaults 0 1\nUUID=@BOOT_UUID@ /boot/firmware vfat defaults 0 2\n' >"$root/etc/fstab"
printf 'root=UUID=@ROOT_UUID@ rootwait\n' >"$boot/current/cmdline.txt"
uf_relocate raspi-arm64 ubuntu "$root" "$boot" 1111-2222 ABCD-1234
grep -q 'root=UUID=1111-2222' "$boot/current/cmdline.txt"
if uf_relocate raspi-arm64 debian "$root" "$boot" 1111-2222 ABCD-1234; then exit 1; fi
echo 'UEFI relocation passed for Ubuntu/Debian on ARM64/x86_64 (temporary directories only).'
