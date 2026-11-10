#!/bin/sh
# Exercise actual EACCES as an unprivileged user inside a disposable container.
set -eu
[ -f /.dockerenv ] || { echo 'Refusing to change a real host'; exit 1; }
app=$1
test ! -e /etc/ubuntu-reset/state.json
test ! -e /var/lib/ubuntu-reset/request
test ! -e /etc/ubuntu-reset/boot.conf
fixture=$(mktemp -d)
chmod 777 "$fixture"
mkdir -p /etc/ubuntu-reset
old_mode=$(stat -c %a /etc/ubuntu-reset)
cleanup() { chmod "$old_mode" /etc/ubuntu-reset; rm -f /etc/ubuntu-reset/state.json; rm -rf "$fixture"; }
trap cleanup EXIT HUP INT TERM
printf '{"private_note":"DO-NOT-EXPORT-personal-secret"}\n' >/etc/ubuntu-reset/state.json
chmod 700 /etc/ubuntu-reset
runuser -u nobody -- "$app" diagnose >"$fixture/stdout.json"
grep -q '"code": "invalid-state"' "$fixture/stdout.json"
if grep -E 'DO-NOT-EXPORT|/etc/ubuntu-reset|1111-2222' "$fixture/stdout.json"; then exit 1; fi
if runuser -u nobody -- "$app" status >"$fixture/status" 2>&1; then exit 1; else code=$?; fi
test "$code" -eq 1
grep -q 'Run linux-factory-reset diagnose' "$fixture/status"
runuser -u nobody -- "$app" diagnose --output "$fixture/report.json"
test "$(stat -c %a "$fixture/report.json")" = 600
cp "$fixture/report.json" "$fixture/original.json"
if runuser -u nobody -- "$app" diagnose --output "$fixture/report.json"; then exit 1; fi
ln -s "$fixture/report.json" "$fixture/report-link"
if runuser -u nobody -- "$app" diagnose --output "$fixture/report-link"; then exit 1; fi
cmp "$fixture/report.json" "$fixture/original.json"
grep -q 'DO-NOT-EXPORT-personal-secret' /etc/ubuntu-reset/state.json
test ! -e /var/lib/ubuntu-reset/request
test ! -e /etc/ubuntu-reset/boot.conf
printf 'Unprivileged diagnostics handled EACCES, kept reports private and refused overwrite/symlinks.\n'
