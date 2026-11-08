#!/bin/bash
# Destructive operations are confined to expendable files and their remote CI guests.
set -euo pipefail
[[ ${GITHUB_ACTIONS:-} == true ]] || exit 1
distro=$1 fault=$2
source_arch=${SOURCE_ARCH:-amd64}
case "$source_arch" in amd64) platform=x86_64-uefi; fedora_arch=x86_64 ;; arm64) platform=arm64-uefi; fedora_arch=aarch64 ;; *) exit 1 ;; esac
mkdir -p evidence
evidence=$PWD/evidence
work=$(mktemp -d /var/lib/lfr-engine.XXXXXX)
loop=''
cleanup() {
    umount "$work/root" 2>/dev/null || :
    umount "$work/esp" 2>/dev/null || :
    [[ -z $loop ]] || losetup -d "$loop" || :
}
trap cleanup EXIT
if [[ $distro == ubuntu ]]; then
    base=https://cloud-images.ubuntu.com/releases/26.04/release/
    file=ubuntu-26.04-server-cloudimg-$source_arch.img
    curl -fLsS "$base/SHA256SUMS" -o "$work/sums"
    hash=$(awk -v f="$file" 'substr($0,67)==f {print substr($0,1,64)}' "$work/sums")
    url=$base$file
    package=''
    if [[ ${PROFILE_ONLY:-false} != true ]]; then package=$(realpath build-native/*.deb); fi
else
    curl -fLsS https://fedoraproject.org/releases.json -o "$work/releases.json"
    row=$(jq -c --arg arch "$fedora_arch" '[.[]|select(.arch==$arch and .variant=="Cloud" and .version=="45" and (.link|test("Cloud-Base-Generic-.*qcow2$")))]|if length==1 then .[0] else error("ambiguous source") end' "$work/releases.json")
    url=$(jq -r .link <<<"$row"); hash=$(jq -r .sha256 <<<"$row")
    package=''
    if [[ ${PROFILE_ONLY:-false} != true ]]; then package=$(realpath build-native/*.rpm); fi
fi
sleep 6
curl -fL --retry 2 --retry-delay 6 "$url" -o "$work/source.qcow2"
printf '%s  %s\n' "$hash" "$work/source.qcow2" | sha256sum -c
printf 'URL: %s\nSHA256: %s\n' "$url" "$hash" >"$evidence/source.txt"
qemu-img convert -f qcow2 -O raw "$work/source.qcow2" "$work/source.raw"
mkdir -p /usr/lib/ubuntu-reset
cp firstboot/* /usr/lib/ubuntu-reset/
build/linux-factory-reset make-profile-image --file "$work/source.raw" --platform "$platform" --distro "$distro" --output "$work/factory.squashfs" 2>&1 | tee "$evidence/conversion.txt"
if [[ ${PROFILE_ONLY:-false} == true ]]; then
    unsquashfs -cat "$work/factory.squashfs" profile.json >"$evidence/profile.json"
    echo 'Source conversion passed. ARM guest boot was NOT tested.' >"$evidence/result.txt"
    exit 0
fi
truncate -s 10G "$work/flat.raw"
sgdisk -n 1:2048:+512M -t 1:ef00 -n 2:0:0 -t 2:8300 "$work/flat.raw"
loop=$(losetup --find --show --partscan "$work/flat.raw"); udevadm settle
mkfs.vfat -F32 "${loop}p1"; mkfs.ext4 -q "${loop}p2"
root_uuid=$(blkid -s UUID -o value "${loop}p2"); boot_uuid=$(blkid -s UUID -o value "${loop}p1")
mkdir "$work/root" "$work/esp"
mount "${loop}p2" "$work/root"; mount "${loop}p1" "$work/esp"
unsquashfs -no-progress -d "$work/profile" "$work/factory.squashfs"
rsync -aHAX "$work/profile/root/" "$work/root/"
rsync -rt "$work/profile/boot/" "$work/esp/"
. boot/relocate.sh
uf_relocate x86_64-uefi "$distro" "$work/root" "$work/esp" "$root_uuid" "$boot_uuid"
# Only the starting QA guest skips the account wizard. Its recovery profile uses it normally.
rm -f "$work/root/etc/systemd/system/multi-user.target.wants/ubuntu-reset-firstboot.service"
sync; umount "$work/root"; umount "$work/esp"; losetup -d "$loop"; loop=''
# Release intermediate image space before the guest downloads its own real official source.
rm -rf "$work/source.raw" "$work/source.qcow2" "$work/profile" "$work/factory.squashfs"
sh tests/real-boot.sh "$work/flat.raw" "$package" "$evidence" automatic "$distro" "$fault"
