#!/bin/bash
# Runs in a pristine Debian/Ubuntu x86_64 or ARM64 UEFI profile after reset.
set -euo pipefail
[[ $EUID -eq 0 ]] || exit 1
[[ ! -e /var/lib/ubuntu-reset-firstboot-complete ]] || exit 0
echo 'Linux initial setup / 首次开机设置'
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
while :; do
    if passwd "$factory_user"; then break; else factory_status=$?; fi
    if [[ $factory_status -eq 126 || $factory_status -eq 127 ]]; then
        echo 'Cannot execute the system password tool. Check the service log and SELinux policy.'
        exit "$factory_status"
    fi
    echo 'Password setup failed; try again.'
    sleep 1
done
factory_group=sudo
if [[ -e /etc/fedora-release ]]; then factory_group=wheel; fi
usermod --append --groups "$factory_group" "$factory_user"
passwd --lock root >/dev/null
touch /var/lib/ubuntu-reset-firstboot-complete
# Disable the one-time setup; Before=getty@tty1 orders login after this script exits.
rm -f /etc/systemd/system/multi-user.target.wants/ubuntu-reset-firstboot.service
systemctl daemon-reload
echo 'Initial setup complete. / 首次设置完成。'
if [[ ! -e /etc/systemd/system/display-manager.service ]]; then
    # Show an opt-in next step on base images, before getty takes over the console.
    desktop_command=''
    if grep -Eq '^ID="?ubuntu"?$' /usr/lib/os-release; then
        desktop_command='sudo apt update && sudo apt install ubuntu-desktop-minimal'
    elif grep -Eq '^ID="?debian"?$' /usr/lib/os-release; then
        desktop_command='sudo apt update && sudo apt install task-gnome-desktop'
    elif [[ -e /etc/fedora-release ]]; then
        desktop_command='sudo dnf5 environment install workstation-product-environment'
    fi
    if [[ -n $desktop_command ]]; then
        echo 'Optional desktop setup after signing in / 登录后可选装桌面'
        printf '  %s\n' "$desktop_command"
        echo 'After installation succeeds / 安装成功后'
        echo '  sudo systemctl set-default graphical.target'
        echo '  sudo reboot'
    fi
fi
if [[ -f /usr/lib/systemd/system/kmsconvt@.service ]]; then
    systemctl --no-block start kmsconvt@tty1.service
else
    systemctl --no-block start getty@tty1.service
fi
