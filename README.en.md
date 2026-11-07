# Linux Factory Reset

[简体中文](README.md) · [Releases](https://github.com/fuyifu2024/linux-factory-reset/releases) · [Validation](docs/validation.md)

A C++20 official-image downloader and experimental Linux recovery tool. Renamed from `ubuntu-reset`; the old command remains a compatibility entry point.

**Development preview: this is not yet a universal one-click factory reset for arbitrary Linux installations.** The reset backend removes existing accounts and files, but a real VM/hardware reboot-and-restore acceptance test has not been completed.

## Capabilities

| Feature | Actual status |
| --- | --- |
| Official downloads | Ubuntu 24.04 / 26.04, current stable Debian, latest stable Fedora; AMD64 and ARM64 |
| Progress | Percentage, bytes, average speed, ETA and cancellation; unknown server sizes are labelled |
| Integrity | SHA-256 from official HTTPS metadata; mismatches never become final files. Detached PGP signatures are not checked by the ISO downloader |
| Local recovery import | IMG / RAW, XZ disk images, standalone QCOW2, project Squashfs; requires a compatible pristine installed system |
| Debian / Ubuntu reset engine | Experimental ordinary ext4 root + separate FAT ESP, standard GRUB, ARM64/x86_64 UEFI; native Ubuntu Pi firmware detected internally |
| Automatic installation from ISO | **Not implemented**. Downloading an ISO does not enable restoration or arm reset |
| Fedora reset engine | **Not implemented**. Official download works; RPM, dracut, SELinux and Btrfs restoration remain future work |
| Linux Mint | Not integrated; no official ARM64 image is assumed |

ARM64 includes generic UEFI VMs, servers and compatible computers. Native U-Boot and other board boot flows are unsupported. Matching CPU architecture alone does not make board images interchangeable.

## Install and use

Choose the `.deb` matching your system and architecture from [Releases](https://github.com/fuyifu2024/linux-factory-reset/releases). Debian 12, Debian 13 and Ubuntu 24.04 builds are provided for AMD64/ARM64. Prefer DEB installation to resolve runtime dependencies.

```sh
sudo apt install ./linux-factory-reset_*.deb
linux-factory-reset gui
```

The unified interface offers official downloads, recovery preparation, a change preview and restoration. Ordinary users do not need to host an HTTPS source or paste checksum strings. Custom administrator sources remain under Advanced.

Official ISO downloads run without administrator privileges or a second disk. Select distribution, release, edition, architecture and a download folder. Addresses and checksums come from official metadata; Fedora prereleases are excluded.

```sh
linux-factory-reset catalog
linux-factory-reset resolve --distro fedora --release stable --edition workstation --arch arm64
linux-factory-reset download --distro debian --release stable --edition netinst --arch arm64 --directory ~/Downloads
```

Ubuntu: `--release 24.04|26.04`, `--edition desktop|server`. Fedora: `--release stable`, `--edition workstation|server`. If the official site lacks a selected combination, the tool stops with an explanation instead of using a third-party source. Optional explicit proxy: `--proxy http://127.0.0.1:7890`; user proxy environment variables also work.

## Experimental recovery preparation

Provide a separate mounted **ext4 recovery partition**. The application does not partition or format disks. In a VM, add a second virtual disk first. Any subdirectory on that recovery partition can be selected; the actual mountpoint is detected, and files are placed under its compatibility `ubuntu-reset/` directory. A folder on the running system filesystem is not a recovery partition.

```sh
sudo linux-factory-reset prepare-image --file /path/to/pristine-system.img --storage /mnt/recovery/folder
sudo linux-factory-reset plan
```

An imported image must contain a pristine installed system of the same distribution, CPU architecture and boot layout, with exactly one ext4 system partition and one FAT boot partition, and no existing personal accounts. Installer/live ISOs, incremental QCOW2 and encrypted disks are not installed-system references. Local files are snapshotted privately and parsed read-only; a computed file hash does not authenticate its publisher.

Automatic preparation appears only when a built-in source matches the detected system. The development branch integrates official Debian 13 NoCloud images for generic AMD64/ARM64 UEFI, using official HTTPS SHA-512 checksums, plus signed Ubuntu 26.04 native Pi images. Downloads run with visible progress/speed/cancellation as the ordinary desktop user; the privileged importer checks the snapshot against the expected digest again.

**The Debian automatic source is a base system without a desktop, with a new administrator created at the local console. It is not a historical copy of the original desktop package set or configuration.** Generic Ubuntu UEFI and Debian 12 still need local installed-system images or administrator-produced profiles. Consult the acceptance record for the release tag being used. **Downloading an official ISO is separate from preparing a usable recovery source.**

Previewing changes requires prepared recovery data. Missing state now gives setup guidance; actual backend errors are displayed and can be copied. Preparation/comparison shows a busy dialog.

```sh
sudo linux-factory-reset arm --confirm ERASE-ALL-USERS --reboot
# Cancel before reboot:
sudo linux-factory-reset cancel
```

Restore runs offline in initramfs and overwrites the root and boot filesystems, deleting old users, personal files, additional software and configuration. Keep recovery media connected. It is neither a power-loss transaction nor a secure disk wipe. Btrfs, LVM, encryption, RAID, separate `/home` or `/boot` and additional system partitions are unsupported. A new administrator is created at first boot. Downloads/previews do not arm reset, reboot or change BIOS settings.

## Graphics compatibility and upgrades

The GUI defaults to Cairo/software rendering for VMs without working EGL/Zink drivers. It changes only its own process environment and preserves explicit user overrides.

```sh
GSK_RENDERER=cairo LIBGL_ALWAYS_SOFTWARE=1 linux-factory-reset gui
```

Legacy state directories, initramfs hook names and recovery-directory names are retained to avoid multiple incompatible reset markers. The new DEB replaces the old package. Upgrading does not prepare or arm recovery.

## Build and validation

```sh
sudo apt install build-essential cmake libssl-dev libcurl4-openssl-dev nlohmann-json3-dev rsync squashfs-tools
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

[CI](.github/workflows/ci.yml) uses temporary directories, synthetic system trees, a local HTTPS fixture inside disposable containers, and a virtual display without a GPU. A successful build or directory fixture does not establish bootable real-machine recovery. See [validation](docs/validation.md) and [recovery protocol](docs/recovery.md).
