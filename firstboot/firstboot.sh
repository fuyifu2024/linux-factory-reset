#!/bin/bash
# Runs in a pristine Debian/Ubuntu x86_64 or ARM64 UEFI profile after reset.
set -euo pipefail
[[ $EUID -eq 0 ]] || exit 1
[[ ! -e /var/lib/ubuntu-reset-firstboot-complete ]] || exit 0
echo 'Debian / Ubuntu initial setup / 首次开机设置'
echo 'Create a new local administrator. / 请创建新的本地管理员。'
if [[ -f /var/lib/ubuntu-reset-firstboot-user ]]; then
    read -r factory_user </var/lib/ubuntu-reset-firstboot-user
    [[ $factory_user =~ ^[a-z][a-z0-9_-]{0,31}$ ]] || exit 1
else
    while :; do
        read -r -p 'Username / 用户名: ' factory_user
        if [[ $factory_user =~ ^[a-z][a-z0-9_-]{0,31}$ ]] && ! getent passwd "$factory_user" >/dev/null; then
            break
        fi
        echo 'Use a new lowercase username (1–32 characters).'
    done
    umask 077
    printf '%s\n' "$factory_user" >/var/lib/ubuntu-reset-firstboot-user
fi
if ! getent passwd "$factory_user" >/dev/null; then
    useradd --create-home --shell /bin/bash "$factory_user"
fi
while ! passwd "$factory_user"; do echo 'Password setup failed; try again.'; done
usermod --append --groups sudo "$factory_user"
passwd --lock root >/dev/null
touch /var/lib/ubuntu-reset-firstboot-complete
# Disable the one-time setup; Before=getty@tty1 orders login after this script exits.
rm -f /etc/systemd/system/multi-user.target.wants/ubuntu-reset-firstboot.service
systemctl daemon-reload
systemctl --no-block start getty@tty1.service
echo 'Initial setup complete. / 首次设置完成。'
