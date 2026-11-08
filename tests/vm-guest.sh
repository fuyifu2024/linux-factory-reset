#!/bin/bash
set -Eeuo pipefail
trap 'echo LFR_VM_PREPARATION_FAIL >/dev/console; systemctl --no-block poweroff' ERR
if [[ -f /etc/lfr-qa.conf ]]; then source /etc/lfr-qa.conf; fi
mkdir -p /mnt/recovery
mountpoint -q /mnt/recovery || mount -t ext4 "UUID=$REC_UUID" /mnt/recovery
source /mnt/recovery/lfr-qa.conf
if [[ $REC_FAULT != none ]] && grep -qx ROLLED_BACK /mnt/recovery/ubuntu-reset/rescue-*/result 2>/dev/null; then
    getent passwd lfr_old_user
    grep -qx 'old private data' /root/lfr-private
    grep -qx 'old home' /home/lfr_old_user/private
    grep -qx 'extra software' /opt/lfr-extra/unwanted
    test ! -e /var/lib/ubuntu-reset/request
    echo PASS >/var/lib/lfr-qa-result; sync; echo LFR_VM_ROLLBACK_PASS >/dev/console
    systemctl --no-block poweroff; exit 0
fi
cycle=0; [[ ! -f /mnt/recovery/qa-cycle ]] || read -r cycle </mnt/recovery/qa-cycle
cycle=$((cycle+1)); echo "$cycle" >/mnt/recovery/qa-cycle
printf 'LFR_VM_CYCLE_START %s\n' "$cycle" >/dev/console
if [[ $REC_DISTRO == fedora ]]; then
    dnf install -y /mnt/recovery/tool.rpm
else
    apt-get update
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends /mnt/recovery/tool.deb
fi
# Fault injection alters only the disposable guest's installed test copy.
if [[ $REC_FAULT != none ]]; then
    if [[ $REC_FAULT == failure ]]; then
        sed -i '/uf_relocate .*Cannot relocate/a uf_fail "expected QA write failure"' /usr/lib/ubuntu-reset/restore.sh
    else
        sed -i '/echo .*removing users/a rm -rf "$UF_TARGET/boot" "$UF_TARGET/usr"; sync; echo LFR_VM_POWER_CUT_READY >/dev/console; sleep 600' /usr/lib/ubuntu-reset/restore.sh
    fi
fi
useradd --create-home --shell /bin/bash lfr_old_user
if [[ $REC_DISTRO == fedora ]]; then usermod -aG wheel lfr_old_user; else usermod -aG sudo lfr_old_user; fi
echo lfr_old_user:qa7reset92factory | chpasswd
mkdir -p /opt/lfr-extra
printf 'old private data\n' >/root/lfr-private
printf 'old home\n' >/home/lfr_old_user/private
printf 'extra software\n' >/opt/lfr-extra/unwanted
# Observe the actual production first-user body without altering it; only the test profile gets this wrapper.
{
printf '#!/bin/bash\nREC_UUID=%q\n' "$REC_UUID"
cat <<'OBSERVER'
set -euo pipefail
exec > >(tee /dev/console) 2>&1
mkdir -p /mnt/recovery
mountpoint -q /mnt/recovery || mount -t ext4 "UUID=$REC_UUID" /mnt/recovery
if getent passwd lfrqatest || getent passwd lfr_old_user || test -e /home/lfr_old_user || test -e /root/lfr-private || test -e /opt/lfr-extra/unwanted; then
    echo LFR_VM_OLD_DATA_SURVIVED; systemctl --no-block poweroff; exit 1
fi
observer_context=()
if [[ -e /etc/fedora-release ]]; then observer_context=(--property=SELinuxContext=system_u:system_r:initrc_t:s0); fi
systemd-run "${observer_context[@]}" --unit=lfr-qa-observer --property=StandardOutput=tty --property=TTYPath=/dev/console --expand-environment=no /bin/bash /mnt/recovery/vm-observer.sh
echo LFR_VM_AWAITING_NEW_USER
OBSERVER
sed '1d' /usr/lib/ubuntu-reset/firstboot.sh
} >/tmp/lfr-observed-firstboot
install -m 755 /tmp/lfr-observed-firstboot /usr/lib/ubuntu-reset/firstboot.sh
if [[ $REC_GUI == yes && $cycle -eq 1 ]]; then
    apt-get update
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends xvfb openbox xterm xdotool xclip scrot nodejs dbus-x11
    Xvfb :99 -screen 0 1024x768x24 -ac >/mnt/recovery/xvfb.txt 2>&1 &
    sleep 2
    mkdir -p /mnt/recovery/gui-evidence
    chmod 777 /mnt/recovery/gui-evidence
    runuser -u lfr_old_user -- env DISPLAY=:99 GDK_BACKEND=x11 GSK_RENDERER=cairo LIBGL_ALWAYS_SOFTWARE=1 dbus-run-session -- bash -c '
        openbox >/mnt/recovery/gui-evidence/openbox.txt 2>&1 &
        /usr/bin/linux-factory-reset gui >/mnt/recovery/gui-evidence/gui.txt 2>&1 &
        gui_pid=$!
        xterm -T "LFR authorization" -e script -q -f /mnt/recovery/gui-evidence/auth.txt -c "pkttyagent --process $gui_pid" &
        node /mnt/recovery/drive-gui.mjs "$gui_pid" /mnt/recovery/gui-evidence /mnt/recovery/GUI-ARM-NOW
        wait "$gui_pid"'
    exit 0
fi
if [[ $REC_MODE == automatic ]]; then
    linux-factory-reset prepare-auto --storage /mnt/recovery
else
    linux-factory-reset prepare-image --file /mnt/recovery/pristine.raw --storage /mnt/recovery
fi
original_order=$(efibootmgr | sed -n 's/^BootOrder: //p')
for attempt in 1 2; do
    linux-factory-reset arm --confirm ERASE-ALL-USERS
    test -f /var/lib/ubuntu-reset/request
    linux-factory-reset cancel
    linux-factory-reset cancel
    test ! -e /var/lib/ubuntu-reset/request
    test "$(efibootmgr | sed -n 's/^BootOrder: //p')" = "$original_order"
done
profile=$(find /mnt/recovery/ubuntu-reset -maxdepth 1 -name 'factory-*.squashfs' -type f | sort | tail -1)
# Corrupt only the selected current profile, then restore its original bytes by truncation.
profile=$(sed -n 's/.*"file": "\([^" ]*\)".*/\1/p' /etc/ubuntu-reset/state.json)
profile=/mnt/recovery/$profile
size=$(stat -c %s "$profile")
printf BAD >>"$profile"
if linux-factory-reset arm --confirm ERASE-ALL-USERS; then exit 91; fi
test ! -e /var/lib/ubuntu-reset/request
truncate -s "$size" "$profile"
udevadm settle
echo LFR_VM_READY_TO_ARM >/dev/console
linux-factory-reset arm --confirm ERASE-ALL-USERS --reboot
