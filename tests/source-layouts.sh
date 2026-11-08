#!/bin/bash
set -euo pipefail
[[ ${GITHUB_ACTIONS:-} == true ]] || exit 1
mkdir -p evidence
work=$(mktemp -d /var/tmp/lfr-layout.XXXXXX)
for distro in ubuntu fedora; do
    if [[ $distro == ubuntu ]]; then
        base=https://cloud-images.ubuntu.com/releases/26.04/release/
        file=ubuntu-26.04-server-cloudimg-amd64.img
        curl -fLsS "$base/SHA256SUMS" -o "$work/sums"
        hash=$(awk -v f="$file" 'substr($0,67)==f {print substr($0,1,64)}' "$work/sums")
        url=$base$file
    else
        curl -fLsS https://fedoraproject.org/releases.json -o "$work/releases.json"
        row=$(jq -c '[.[]|select(.arch=="x86_64" and .variant=="Cloud" and (.version|test("^[0-9]+$")) and (.link|test("Cloud-Base-Generic-.*qcow2$")))]|max_by(.version|tonumber)' "$work/releases.json")
        url=$(jq -r .link <<<"$row"); hash=$(jq -r .sha256 <<<"$row")
    fi
    sleep 6
    curl -fL --retry 2 --retry-delay 6 "$url" -o "$work/source.qcow2"
    printf '%s  %s\n' "$hash" "$work/source.qcow2" | sha256sum -c
    qemu-img convert -f qcow2 -O raw "$work/source.qcow2" "$work/disk.raw"
    loop=$(losetup --find --show --read-only --partscan "$work/disk.raw")
    udevadm settle
    lsblk -o PATH,FSTYPE,UUID,PARTLABEL "$loop" >"evidence/$distro.txt"
    mkdir -p "$work/mount"
    while read -r dev type; do
        case "$type" in ext4) opts=ro,noload ;; btrfs) opts=ro ;; vfat) opts=ro ;; *) continue ;; esac
        mount -t "$type" -o "$opts" "$dev" "$work/mount"
        {
            echo "=== $dev $type ==="
            ls -la "$work/mount"
            for prefix in '' root; do
                for path in usr/lib/os-release etc/fstab boot/grub/grub.cfg boot/grub2/grub.cfg etc/default/grub; do
                    p="$work/mount/$prefix/$path"; [[ ! -f $p ]] || { echo "=== $prefix/$path ==="; cat "$p"; }
                done
                find "$work/mount/$prefix/boot" -maxdepth 4 -type f 2>/dev/null | head -80 || :
                [[ ! -d "$work/mount/$prefix/etc/cloud" ]] || find "$work/mount/$prefix/etc/cloud" -maxdepth 2 -type f
            done
            find "$work/mount/EFI" "$work/mount/loader" -maxdepth 4 -type f 2>/dev/null | while read -r p; do
                echo "$p"; case "$p" in *.cfg|*.conf) cat "$p" ;; esac
            done || :
        } >>"evidence/$distro.txt"
        umount "$work/mount"
    done < <(lsblk -nrpo PATH,FSTYPE "$loop")
    losetup -d "$loop"
done
