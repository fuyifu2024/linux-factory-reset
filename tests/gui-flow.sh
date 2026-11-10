#!/bin/sh
# Disposable CI container only. A deterministic dialog double drives the real GUI controller.
set -eu
[ -f /.dockerenv ] || { echo "This fixture mutates a disposable CI container; refusing a real host"; exit 1; }
app=$1
fixture=$(mktemp -d)
cp /usr/bin/zenity "$fixture/zenity.original"
cleanup() {
    cp "$fixture/zenity.original" /usr/bin/zenity
    if [ -n "${old_mode:-}" ]; then chmod "$old_mode" /etc/ubuntu-reset; rm -f /etc/ubuntu-reset/state.json; fi
    rm -rf "$fixture"
}
trap cleanup EXIT HUP INT TERM
chmod 755 "$fixture"
mkdir "$fixture/state"
chown gui-smoke:gui-smoke "$fixture/state"
cat >/usr/bin/zenity <<'DIALOG'
#!/bin/sh
set -eu
case "$1" in
    --list)
        if [ -e "$GUI_FLOW_DIR/selected" ]; then exit 1; fi
        touch "$GUI_FLOW_DIR/selected"
        printf '%s\n' "${GUI_FLOW_CHOICE:-plan}"
        ;;
    --text-info)
        for arg in "$@"; do
            case "$arg" in --filename=*) cat "${arg#--filename=}" >"$GUI_FLOW_DIR/message" ;; esac
        done
        ;;
    --file-selection) printf '%s/report.json\n' "$GUI_FLOW_DIR" ;;
    *) exit 88 ;;
esac
DIALOG
chmod 755 /usr/bin/zenity
runuser -u gui-smoke -- env GUI_FLOW_DIR="$fixture/state" "$app" gui
grep -q '请先选择并准备恢复镜像' "$fixture/state/message"
test ! -e /etc/ubuntu-reset/state.json
test ! -e /var/lib/ubuntu-reset/request
rm "$fixture/state/selected"
runuser -u gui-smoke -- env GUI_FLOW_CHOICE=diagnose GUI_FLOW_DIR="$fixture/state" "$app" gui
test "$(stat -c %a "$fixture/state/report.json")" = 600
grep -q '"report_schema": 1' "$fixture/state/report.json"
grep -q '诊断报告已保存到本机' "$fixture/state/message"
mkdir -p /etc/ubuntu-reset
old_mode=$(stat -c %a /etc/ubuntu-reset)
printf '{}\n' >/etc/ubuntu-reset/state.json
chmod 700 /etc/ubuntu-reset
rm "$fixture/state/selected"
runuser -u gui-smoke -- env GUI_FLOW_DIR="$fixture/state" "$app" gui
grep -q '恢复资料记录无法读取' "$fixture/state/message"
chmod "$old_mode" /etc/ubuntu-reset
rm /etc/ubuntu-reset/state.json
test ! -e /etc/ubuntu-reset/state.json
test ! -e /var/lib/ubuntu-reset/request
printf 'GUI guided missing/inaccessible state and exported diagnostics without privilege escalation or reset.\n'
