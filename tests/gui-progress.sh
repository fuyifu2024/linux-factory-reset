#!/bin/sh
# Disposable container only: exercise the actual GUI's progress stream and heartbeat.
set -eu
[ -f /.dockerenv ] || exit 1
app=$1
test ! -e /var/lib/ubuntu-reset/request
fixture=$(mktemp -d)
chmod 755 "$fixture"
cp -p /usr/bin/zenity "$fixture/zenity.original"
cp -p /usr/bin/pkexec "$fixture/pkexec.original"
cleanup() { cp -p "$fixture/zenity.original" /usr/bin/zenity; cp -p "$fixture/pkexec.original" /usr/bin/pkexec; rm -rf "$fixture"; }
trap cleanup EXIT HUP INT TERM
mkdir "$fixture/state"
chown gui-smoke:gui-smoke "$fixture/state"
cat >/usr/bin/zenity <<'DIALOG'
#!/bin/sh
set -eu
case "$1" in
  --list)
    test ! -e "$GUI_FLOW_DIR/selected" || exit 1
    touch "$GUI_FLOW_DIR/selected"; printf 'cancel\n' ;;
  --progress) exec cat >"$GUI_FLOW_DIR/progress" ;;
  --text-info)
    for arg in "$@"; do case "$arg" in --filename=*) cat "${arg#--filename=}" >"$GUI_FLOW_DIR/result" ;; esac; done ;;
  *) exit 88 ;;
esac
DIALOG
cat >/usr/bin/pkexec <<'BACKEND'
#!/bin/sh
set -eu
test "$#" -eq 3
test "$3" = cancel
printf '\nLFR_STAGE {"stage":"verifying"}\n'
sleep 1.3
printf '\nLFR_STAGE {"stage":"<b>untrusted-display-text</b>"}\n'
printf '\nLFR_STAGE {"stage":"packing"}\n'
sleep 1.3
printf 'Fixture backend finished without performing a reset.\n'
BACKEND
chmod 755 /usr/bin/zenity /usr/bin/pkexec
runuser -u gui-smoke -- env GUI_FLOW_DIR="$fixture/state" "$app" gui
grep -q '校验镜像完整性' "$fixture/state/progress"
grep -q '生成压缩恢复资料' "$fixture/state/progress"
grep -q '阶段已用 1 秒' "$fixture/state/progress"
grep -q '总用时' "$fixture/state/progress"
if grep -q 'untrusted-display-text' "$fixture/state/progress"; then exit 1; fi
grep -q 'Fixture backend finished' "$fixture/state/result"
test ! -e /var/lib/ubuntu-reset/request
printf 'Actual GUI rendered stage changes and elapsed-time heartbeats; unknown stage markup was ignored.\n'
