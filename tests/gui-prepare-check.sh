#!/bin/sh
# Disposable CI container only: verify automatic preparation stops BEFORE network/download selection.
set -eu
[ -f /.dockerenv ] || exit 1
app=$1
# Containers have no UEFI and therefore no offered automatic source. The dialog
# double deliberately supplies that selection to exercise the refusal branch;
# the real backend must still reject the layout before any network operation.
test ! -e /etc/ubuntu-reset/state.json
test ! -e /var/lib/ubuntu-reset/request
fixture=$(mktemp -d)
chmod 755 "$fixture"
cp /usr/bin/zenity "$fixture/zenity.original"
cp -p /usr/bin/pkexec "$fixture/pkexec.original"
cleanup() { cp "$fixture/zenity.original" /usr/bin/zenity; cp -p "$fixture/pkexec.original" /usr/bin/pkexec; rm -rf "$fixture"; }
trap cleanup EXIT HUP INT TERM
mkdir "$fixture/state"
chown gui-smoke:gui-smoke "$fixture/state"
cat >/usr/bin/zenity <<'DIALOG'
#!/bin/sh
set -eu
case "$1" in
  --list)
    case "$*" in
      *--column=状态*)
        test ! -e "$GUI_FLOW_DIR/main-selected" || exit 1
        touch "$GUI_FLOW_DIR/main-selected"; printf 'prepare\n' ;;
      *--column=来源*) printf 'automatic\n' ;;
      *) exit 88 ;;
    esac ;;
  --text-info)
    for arg in "$@"; do
      case "$arg" in --filename=*) cat "${arg#--filename=}" >>"$GUI_FLOW_DIR/messages" ;; esac
    done ;;
  --file-selection)
    test ! -e "$GUI_FLOW_DIR/storage-selected" || exit 89
    touch "$GUI_FLOW_DIR/storage-selected"; printf '%s\n' "$GUI_FLOW_DIR" ;;
  --progress) exit 0 ;;
  *) exit 90 ;;
esac
DIALOG
cat >/usr/bin/pkexec <<'CHECK'
#!/bin/sh
set -eu
test "$#" -eq 7
test "$6" = --language
test "$7" = zh
test "$1" = --disable-internal-agent
test "$3" = check
test "$4" = --storage
printf '%s\n' "$3" >>"$GUI_FLOW_DIR/backend-actions"
shift
exec "$@"
CHECK
chmod 755 /usr/bin/zenity /usr/bin/pkexec
runuser -u gui-smoke -- env GUI_FLOW_DIR="$fixture/state" "$app" gui --language zh
test "$(cat "$fixture/state/backend-actions")" = check
grep -q '"compatible": false' "$fixture/state/messages"
test ! -e "$fixture/state/ubuntu-reset"
test ! -e /etc/ubuntu-reset/state.json
test ! -e /var/lib/ubuntu-reset/request
printf 'Actual GUI controller stopped automatic preparation at the real read-only check, before catalog lookup or download selection.\n'
