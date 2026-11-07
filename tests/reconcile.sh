#!/bin/sh
set -eu
library=$1
fixture=$(mktemp -d)
trap 'rm -rf "$fixture"' EXIT HUP INT TERM
# shellcheck source=boot/reconcile.sh
. "$library"
source_tree="$fixture/source"
target_tree="$fixture/target"
boot_source="$fixture/boot-source"
boot_target="$fixture/boot-target"
outside="$fixture/outside"
mkdir -p "$source_tree/etc" "$source_tree/usr/bin" "$source_tree/var/lib/dpkg" "$source_tree/home" "$source_tree/root" \
    "$target_tree/etc" "$target_tree/usr/bin" "$target_tree/var/lib/dpkg" "$target_tree/home/alice" \
    "$target_tree/root" "$target_tree/var/lib/ubuntu-reset" "$boot_source" "$boot_target" "$outside"
printf 'root:x:0:0:root:/root:/bin/bash\n' >"$source_tree/etc/passwd"
printf 'root:!:1:0:99999:7:::\n' >"$source_tree/etc/shadow"
printf 'factory config\n' >"$source_tree/etc/example.conf"
printf 'original system bytes\n' >"$source_tree/usr/bin/system-file"
printf 'identical bytes\n' >"$source_tree/usr/bin/unchanged"
printf 'Package: base-files\n' >"$source_tree/var/lib/dpkg/status"
cp -p "$source_tree/usr/bin/unchanged" "$target_tree/usr/bin/unchanged"
before=$(stat -c '%i:%Y' "$target_tree/usr/bin/unchanged")
printf 'modified\n' >"$target_tree/usr/bin/system-file"
printf 'alice:x:1000:1000:Alice:/home/alice:/bin/bash\n' >"$target_tree/etc/passwd"
printf 'personal secret\n' >"$target_tree/home/alice/secret"
printf 'root secret\n' >"$target_tree/root/secret"
printf 'extra app\n' >"$target_tree/usr/bin/extra"
printf 'pending\n' >"$target_tree/var/lib/ubuntu-reset/request"
printf 'original firmware\n' >"$boot_source/config.txt"
printf 'custom firmware\n' >"$boot_target/config.txt"
printf 'custom addon\n' >"$boot_target/addon"
uf_reconcile "$source_tree" "$target_tree" "$boot_source" "$boot_target"
cmp "$source_tree/etc/passwd" "$target_tree/etc/passwd"
cmp "$source_tree/etc/shadow" "$target_tree/etc/shadow"
cmp "$source_tree/etc/example.conf" "$target_tree/etc/example.conf"
cmp "$source_tree/usr/bin/system-file" "$target_tree/usr/bin/system-file"
cmp "$source_tree/var/lib/dpkg/status" "$target_tree/var/lib/dpkg/status"
[ ! -e "$target_tree/home/alice" ]
[ ! -e "$target_tree/root/secret" ]
[ ! -e "$target_tree/usr/bin/extra" ]
[ -f "$target_tree/var/lib/ubuntu-reset/request" ]
cmp "$boot_source/config.txt" "$boot_target/config.txt"
[ ! -e "$boot_target/addon" ]
[ "$(stat -c '%i:%Y' "$target_tree/usr/bin/unchanged")" = "$before" ]
# A repeated restore has the same result and retains unchanged file data/inodes.
uf_reconcile "$source_tree" "$target_tree" "$boot_source" "$boot_target"
[ "$(stat -c '%i:%Y' "$target_tree/usr/bin/unchanged")" = "$before" ]
# Replacing a malicious target directory symlink must not write through it.
mkdir -p "$source_tree/opt/app"
printf 'safe\n' >"$source_tree/opt/app/file"
mkdir -p "$target_tree/opt"
ln -s "$outside" "$target_tree/opt/app"
uf_reconcile "$source_tree" "$target_tree" "$boot_source" "$boot_target"
[ ! -e "$outside/file" ]
[ ! -L "$target_tree/opt/app" ]
cmp "$source_tree/opt/app/file" "$target_tree/opt/app/file"
# Refuse a missing reference before writing anything.
if uf_reconcile "$fixture/missing" "$target_tree" "$boot_source" "$boot_target"; then exit 1; fi
# A failed root copy must not go on to rewrite the boot partition.
printf 'boot must survive failed root copy\n' >"$boot_target/config.txt"
rsync() { return 1; }
if uf_reconcile "$source_tree" "$target_tree" "$boot_source" "$boot_target"; then exit 1; fi
unset -f rsync
[ "$(cat "$boot_target/config.txt")" = 'boot must survive failed root copy' ]
[ -f "$target_tree/var/lib/ubuntu-reset/request" ]
echo 'Offline reconciliation integration checks passed (temporary directories only).'
