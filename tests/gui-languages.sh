#!/bin/sh
# Disposable container only. Dialog/controller fixtures never perform a real reset.
set -eu
[ -f /.dockerenv ] || exit 1
app=$1
test ! -e /etc/ubuntu-reset/state.json
test ! -e /var/lib/ubuntu-reset/request
fixture=$(mktemp -d)
chmod 755 "$fixture"
cp -p /usr/bin/zenity "$fixture/zenity.original"
cp -p /usr/bin/pkexec "$fixture/pkexec.original"
cleanup() { cp -p "$fixture/zenity.original" /usr/bin/zenity; cp -p "$fixture/pkexec.original" /usr/bin/pkexec; rm -f /etc/ubuntu-reset/state.json; rm -rf "$fixture"; }
trap cleanup EXIT HUP INT TERM
mkdir -p /etc/ubuntu-reset
cat >/etc/ubuntu-reset/state.json <<'STATE'
{"schema":2,"distro":"ubuntu","platform":"x86_64-uefi","root_uuid":"1111-2222","boot_uuid":"3333-4444","store_uuid":"5555-6666","id":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","sha256":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","file":"ubuntu-reset/factory-aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.squashfs"}
STATE
chmod 644 /etc/ubuntu-reset/state.json
cat >/usr/bin/zenity <<'DIALOG'
#!/bin/sh
set -eu
case "$1" in
  --list)
    if [ "$GUI_CASE" = language ]; then
      if [ ! -e "$GUI_FLOW_DIR/first" ]; then touch "$GUI_FLOW_DIR/first"; printf 'language\n'
      elif [ ! -e "$GUI_FLOW_DIR/second" ]; then touch "$GUI_FLOW_DIR/second"; printf 'es\n'
      else printf '%s\n' "$@" >"$GUI_FLOW_DIR/menu"; exit 1; fi
    elif [ "$GUI_CASE" = inspect ]; then printf '%s\n' "$@" >"$GUI_FLOW_DIR/menu"; exit 1
    else test ! -e "$GUI_FLOW_DIR/selected" || exit 1; touch "$GUI_FLOW_DIR/selected"; printf 'restore\n'; fi ;;
  --entry) printf '%s\n' "$@" >"$GUI_FLOW_DIR/prompt"; printf '%s\n' "$GUI_CONFIRM" ;;
  --progress) exec cat >/dev/null ;;
  --text-info) exit 0 ;;
  *) exit 88 ;;
esac
DIALOG
cat >/usr/bin/pkexec <<'BACKEND'
#!/bin/sh
set -eu
test "$#" -eq 8
test "$1" = --disable-internal-agent
test "$3" = arm
test "$4" = --confirm
test "$5" = ERASE-ALL-USERS
test "$6" = --reboot
test "$7" = --language
test "$8" = "$GUI_TEST_LANG"
printf '%s\n' "$@" >"$GUI_FLOW_DIR/backend"
BACKEND
chmod 755 /usr/bin/zenity /usr/bin/pkexec
for code in en zh hi es ar; do
  for confirmation in wrong ERASE-ALL-USERS; do
    state="$fixture/$code-$confirmation"
    mkdir "$state"; chown gui-smoke:gui-smoke "$state"
    runuser -u gui-smoke -- env -u LFR_LANGUAGE GUI_FLOW_DIR="$state" XDG_CONFIG_HOME="$state/config" GUI_CASE=confirm GUI_CONFIRM="$confirmation" GUI_TEST_LANG="$code" "$app" gui --language "$code"
    grep -q ERASE-ALL-USERS "$state/prompt"
    if [ "$confirmation" = wrong ]; then test ! -e "$state/backend"; else test -f "$state/backend"; fi
  done
done
state="$fixture/preference"; mkdir "$state"; chown gui-smoke:gui-smoke "$state"
runuser -u gui-smoke -- env -u LFR_LANGUAGE LC_ALL=C GUI_FLOW_DIR="$state" XDG_CONFIG_HOME="$state/config" GUI_CASE=language "$app" gui --language en
test "$(cat "$state/config/linux-factory-reset/language")" = es
test "$(stat -c %a "$state/config/linux-factory-reset/language")" = 600
grep -q -- '--column=Estado' "$state/menu"
runuser -u gui-smoke -- env -u LFR_LANGUAGE LC_ALL=C GUI_FLOW_DIR="$state" XDG_CONFIG_HOME="$state/config" GUI_CASE=inspect "$app" gui
grep -q -- '--column=Estado' "$state/menu"
printf '../invalid\n' >"$state/config/linux-factory-reset/language"
runuser -u gui-smoke -- env -u LFR_LANGUAGE LC_ALL=C GUI_FLOW_DIR="$state" XDG_CONFIG_HOME="$state/config" GUI_CASE=inspect "$app" gui
grep -q -- '--column=Status' "$state/menu"
test ! -e /var/lib/ubuntu-reset/request
printf 'Five-language GUI confirmations, backend language forwarding, manual selection, private preference and invalid-preference fallback passed.\n'
