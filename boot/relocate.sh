#!/bin/sh
# Offline boot-pointer relocation, shared with directory-fixture tests.
uf_relocate() {
    uf_platform=$1
    uf_distro=$2
    uf_root_tree=$3
    uf_boot_tree=$4
    uf_root_uuid=$5
    uf_boot_uuid=$6
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
