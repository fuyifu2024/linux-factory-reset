#!/bin/sh
# Real GTK/Zenity rendering only; no preparation, download or restore is selected.
set -eu
[ -f /.dockerenv ] || exit 1
app=$1
probe=$2
out=$3
mkdir -p "$out"
chown gui-smoke:gui-smoke "$out"
for code in en zh hi es ar; do
  runuser -u gui-smoke -- env LFR_TEST_LANG="$code" LFR_TEST_APP="$app" LFR_TEST_PROBE="$probe" LFR_TEST_OUT="$out" GSK_RENDERER=cairo LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a sh <<'UI'
set -eu
"$LFR_TEST_PROBE" "$LFR_TEST_LANG" >"$LFR_TEST_OUT/$LFR_TEST_LANG-direction.txt"
"$LFR_TEST_APP" gui --language "$LFR_TEST_LANG" >"$LFR_TEST_OUT/$LFR_TEST_LANG.log" 2>&1 &
app_pid=$!
cleanup() { kill "$app_pid" 2>/dev/null || :; }
trap cleanup EXIT HUP INT TERM
zenity_pid=''
for attempt in $(seq 1 60); do
  zenity_pid=$(pgrep -P "$app_pid" -x zenity || :)
  test -z "$zenity_pid" || break
  sleep 0.2
done
test -n "$zenity_pid"
window=$(timeout 12 xdotool search --sync --onlyvisible --pid "$zenity_pid" | head -1)
sleep 0.5
scrot --overwrite "$LFR_TEST_OUT/$LFR_TEST_LANG.png"
xdotool windowclose "$window"
wait "$app_pid"
trap - EXIT HUP INT TERM
UI
done
if grep -E 'MESA: error|ZINK:|libEGL warning|failed to create dri2' "$out/"*.log; then exit 1; fi
grep -q 'ar GTK[34] rtl' "$out/ar-direction.txt"
test ! -e /var/lib/ubuntu-reset/request
printf 'Real five-language Zenity rendering and actual GTK RTL/LTR direction checks passed.\n'
