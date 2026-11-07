#!/bin/sh
# Disposable CI container only. A deterministic dialog double drives the real GUI controller.
set -eu
[ -f /.dockerenv ] || { echo "This fixture mutates a disposable CI container; refusing a real host"; exit 1; }
app=$1
fixture=$(mktemp -d)
cp /usr/bin/zenity "$fixture/zenity.original"
cleanup() { cp "$fixture/zenity.original" /usr/bin/zenity; rm -rf "$fixture"; }
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
        printf 'plan\n'
        ;;
    --text-info)
        for arg in "$@"; do
            case "$arg" in --filename=*) cat "${arg#--filename=}" >"$GUI_FLOW_DIR/message" ;; esac
        done
        ;;
    *) exit 88 ;;
esac
DIALOG
chmod 755 /usr/bin/zenity
runuser -u gui-smoke -- env GUI_FLOW_DIR="$fixture/state" "$app" gui
grep -q '请先选择并准备恢复镜像' "$fixture/state/message"
test ! -e /etc/ubuntu-reset/state.json
test ! -e /var/lib/ubuntu-reset/request
printf 'Unprepared preview guided the user without privilege escalation or reset.\n'
