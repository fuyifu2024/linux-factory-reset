#!/bin/sh
# Offline boot-pointer relocation, shared with directory-fixture tests.
uf_inside() {
    uf_base=$(readlink -f "$1") || return 1
    uf_path=$(readlink -f "$2") || return 1
    case "$uf_path" in "$uf_base"|"$uf_base"/*) return 0 ;; *) return 1 ;; esac
}
# Validate every relocation pointer before ANY destructive copying begins.
uf_validate_profile() {
    uf_p=$1 uf_d=$2 uf_r=$3 uf_b=$4
    case "$uf_d" in ubuntu|debian) ;; *) return 1 ;; esac
    uf_inside "$uf_r" "$uf_r/etc/fstab" || return 1
    grep -q '@ROOT_UUID@' "$uf_r/etc/fstab" && grep -q '@BOOT_UUID@' "$uf_r/etc/fstab" || return 1
    case "$uf_p" in
        raspi-arm64)
            [ "$uf_d" = ubuntu ] || return 1
            uf_commands=$(find "$uf_b" -type f -name cmdline.txt) || return 1
            [ -n "$uf_commands" ] || return 1
            # Reference creation rejects escaping command paths; repeat here on verified bytes.
            printf '%s\n' "$uf_commands" | while IFS= read -r uf_c; do
                uf_inside "$uf_b" "$uf_c" && grep -q 'root=UUID=@ROOT_UUID@' "$uf_c" || exit 1
            done || return 1
            ;;
        arm64-uefi|x86_64-uefi)
            for uf_c in "$uf_r/boot/grub/grub.cfg" "$uf_b/EFI/$uf_d/grub.cfg"; do
                if [ "$uf_c" = "$uf_r/boot/grub/grub.cfg" ]; then uf_tree=$uf_r; else uf_tree=$uf_b; fi
                uf_inside "$uf_tree" "$uf_c" && grep -q '@ROOT_UUID@' "$uf_c" || return 1
            done
            awk '
                $1 ~ /^linux(efi|16)?$/ {
                    seen=1; roots=0;
                    for (i=2;i<=NF;i++) if ($i ~ /^root=/) { roots++; if ($i != "root=UUID=@ROOT_UUID@") exit 1 }
                    if (roots != 1) exit 1
                }
                END { if (!seen) exit 1 }
            ' "$uf_r/boot/grub/grub.cfg" || return 1
            ;;
        *) return 1 ;;
    esac
}
uf_relocate() {
    uf_platform=$1
    uf_distro=$2
    uf_root_tree=$3
    uf_boot_tree=$4
    uf_root_uuid=$5
    uf_boot_uuid=$6
    uf_validate_profile "$uf_platform" "$uf_distro" "$uf_root_tree" "$uf_boot_tree" || return 79
    case "$uf_distro" in ubuntu|debian) ;; *) return 80 ;; esac
    for uf_uuid in "$uf_root_uuid" "$uf_boot_uuid"; do
        case "$uf_uuid" in ''|*[!A-Za-z0-9-]*) return 81 ;; esac
    done
    [ -f "$uf_root_tree/etc/fstab" ] || return 82
    [ ! -L "$uf_root_tree/etc/fstab" ] || return 82
    case "$uf_platform" in
        raspi-arm64)
            [ "$uf_distro" = ubuntu ] || return 83
            [ -n "$(find "$uf_boot_tree" -type f -name cmdline.txt)" ] || return 83
            ;;
        x86_64-uefi|arm64-uefi)
            for uf_cfg in "$uf_root_tree/boot/grub/grub.cfg" "$uf_boot_tree/EFI/$uf_distro/grub.cfg"; do
                if [ ! -f "$uf_cfg" ] || [ -L "$uf_cfg" ]; then return 84; fi
            done
            ;;
        *) return 85 ;;
    esac
    sed -i "s/@ROOT_UUID@/$uf_root_uuid/g; s/@BOOT_UUID@/$uf_boot_uuid/g" "$uf_root_tree/etc/fstab" || return 86
    case "$uf_platform" in
        raspi-arm64)
            # With -exec ... +, GNU find returns failure if the editor fails.
            find "$uf_boot_tree" -type f -name cmdline.txt -exec sed -i "s/@ROOT_UUID@/$uf_root_uuid/g" '{}' '+' || return 87
            ;;
        *)
            for uf_cfg in "$uf_root_tree/boot/grub/grub.cfg" "$uf_boot_tree/EFI/$uf_distro/grub.cfg"; do
                sed -i "s/@ROOT_UUID@/$uf_root_uuid/g" "$uf_cfg" || return 88
            done
            ;;
    esac
}
