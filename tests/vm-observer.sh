#!/bin/bash
set -o pipefail
source /mnt/recovery/lfr-qa.conf
read -r cycle </mnt/recovery/qa-cycle
    exec > >(tee /var/lib/lfr-qa-diagnostics /dev/console) 2>&1
    for ((i=0;i<180;i++)); do
        if test -f /var/lib/ubuntu-reset-firstboot-complete; then
            uname -m
            id lfrqatest
            passwd -S root
            if test -e /etc/fedora-release && test "$(getenforce)" != Enforcing; then echo LFR_VM_SELINUX_FAIL; systemctl --no-block poweroff; exit 1; fi
            # Check the locked field without relying on passwd implementation
            # status formatting or printing password hashes into evidence.
            root_locked=0
            while IFS=: read -r account credential remainder; do
                if [[ $account == root && $credential =~ ^[!*]+$ ]]; then root_locked=1; fi
            done </etc/shadow
            if getent passwd lfrqatest && id -nG lfrqatest | grep -Eq "(^| )(sudo|wheel)( |$)" && test "$root_locked" -eq 1 && printf "%s\n" qa7reset92factory | runuser -u lfrqatest -- sudo -S -k -p "" /usr/bin/id -u | grep -qx 0; then echo PASS >/var/lib/lfr-qa-result; sync; echo "LFR_VM_ACCOUNT_PASS $cycle"; else echo FAIL >/var/lib/lfr-qa-result; sync; echo LFR_VM_NEW_USER_FAIL; fi
            cp /var/lib/lfr-qa-result "/mnt/recovery/cycle-$cycle-result"
            cp /var/lib/lfr-qa-diagnostics "/mnt/recovery/cycle-$cycle-diagnostics"
            sync
            if grep -qx PASS /var/lib/lfr-qa-result && (( cycle < REC_CYCLES )); then
                echo "LFR_VM_CYCLE_PASS $cycle"
                next_context=()
                if [[ -e /etc/fedora-release ]]; then next_context=(--property=SELinuxContext=system_u:system_r:initrc_t:s0); fi
                sleep 2
                systemd-run "${next_context[@]}" --unit=lfr-qa-next --setenv="REC_UUID=$REC_UUID" --property=StandardOutput=journal+console /bin/bash /mnt/recovery/vm-guest.sh
            else
                echo LFR_VM_ALL_CYCLES_COMPLETE
                systemctl --no-block poweroff
            fi
            exit
        fi
        sleep 1
    done
    journalctl -b -u ubuntu-reset-firstboot.service --no-pager
    echo LFR_VM_SETUP_TIMEOUT; systemctl --no-block poweroff; exit 1
