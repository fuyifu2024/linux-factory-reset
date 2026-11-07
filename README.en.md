# Ubuntu / Debian Reset

[简体中文](README.md) · [Releases](https://github.com/fuyifu2024/ubuntu-reset/releases) · [Validation](docs/validation.md)

A **C++20 application with a Chinese desktop entry and Shell recovery scripts**. Download trusted initial-system data, reboot into initramfs, remove previous users/data/extra software, and restore files without formatting partitions or manually running an installer. The repository, package and CLI remain named `ubuntu-reset` for compatibility.

> **v0.2.0-alpha.1 is experimental.** File-tree recovery and cross-distribution packaging are tested. Real hardware reboot recovery, initial setup and physical BIOS reset have not been validated. Use disposable test systems.

## Platforms

| Distribution and CPU | Boot layout | Recovery source |
|---|---|---|
| Debian 12/13, generic ARM64 | UEFI + GRUB, ext4 `/`, FAT `/boot/efi`, `EFI/debian` | Trusted administrator/OEM installed-system profile matching the device |
| Ubuntu, generic ARM64 | UEFI + GRUB, ext4 `/`, FAT `/boot/efi`, `EFI/ubuntu` | Matching administrator/OEM profile |
| Debian/Ubuntu, x86_64 | Same layout; distro-specific EFI directory and x64 loader | Matching administrator/OEM profile |
| Ubuntu, Raspberry Pi 5 native firmware | ext4 `/`, FAT `/boot/firmware`, stock Ubuntu `current/` recipe | Automatic download and GPG verification of an official Ubuntu preinstalled desktop image |

ARM64 support includes compatible UEFI servers, virtual machines, ARM64 computers and development boards; it is **not limited to Raspberry Pi**. A Pi booting through UEFI uses the generic `arm64-uefi` path. Matching boot layout and a device-compatible profile are required; this is not a claim that every device has been validated.

Native non-UEFI U-Boot/extlinux/bare-firmware layouts need separate adapters. ARM32, LUKS, LVM, RAID, Btrfs, separate `/home` or `/boot`, dual-boot and custom boot schemes are unsupported. The existing native-Pi adapter is Ubuntu-specific; other Debian Pi firmware layouts are not supported by that adapter.

**Debian and generic ARM64 use `prepare-profile`, not an automatic arbitrary ISO import.** Installer/live filesystems are not equivalent to installed systems. Cross-distribution Debian/Ubuntu replacement is rejected. See [profile preparation](docs/profiles.md).

## Operation

1. For native Ubuntu Pi, download an official image and verify the Ubuntu GPG signature plus SHA-256. Otherwise download a trusted Debian/Ubuntu Squashfs profile and verify an independently trusted SHA-256.
2. Keep complete pristine reference data on a **separate ext4 recovery partition**. Initial preparation downloads complete data; it is not a network-delta downloader.
3. Preview changes with `plan`. After explicit confirmation, build a recovery initramfs and write the reset marker last.
4. On the next boot, verify target UUIDs, recovery hash, capacity and mount layout before modifying the installed system. Restore changed/missing files, reuse identical file content, remove extra files and previous users.
5. Relocate GRUB pointers using `EFI/debian` or `EFI/ubuntu` and the correct architecture loader (`grubaa64.efi` for ARM64, `grubx64.efi` for x86_64).
6. Reboot into initial setup. Generic UEFI profiles include a local tty1 administrator-creation flow; native Ubuntu Pi retains the official image's setup flow.

The selected source defines the initial system; historical personal installation choices cannot be reconstructed automatically. Previous `/home` and `/root` data is logically deleted, not securely erased. Other disks and the recovery partition remain. The tool itself disappears after restoration if it is absent from the reference.

## Downloads and installation

[Releases](https://github.com/fuyifu2024/ubuntu-reset/releases) contain **Chinese and English notes**, source archives, native ARM64/amd64 binaries, DEB packages and SHA-256 checksums. Choose `ubuntu24.04`, `debian12` or `debian13`, then `arm64` or `amd64`. Ubuntu builds may run on newer Ubuntu versions satisfying their dependencies. ARM64 binaries target AArch64 userspace, not a particular board brand.

```sh
sudo apt install ./matching-distribution-and-architecture.deb
ubuntu-reset gui
```

Raw executables dynamically link system libraries. Install the DEB for the complete recovery scripts. The package has no installation script that arms a reset, rebuilds initramfs or reboots; installing dependencies may trigger normal distribution maintenance.

## CLI

Prepare and mount a separate ext4 recovery partition yourself. The tool does not partition or format disks. Preparation requires space for downloads, expanded images, staging and compressed profiles; a 64 GB or larger recovery device is recommended, subject to capacity checks.

```sh
# Debian/Ubuntu generic ARM64 or x86_64, with a trusted matching installed-system profile
sudo ubuntu-reset prepare-profile \
  --url https://your-trusted-server.example/factory.squashfs \
  --sha256 YOUR_INDEPENDENTLY_TRUSTED_SHA256 \
  --storage /mnt/recovery

# Native Ubuntu Raspberry Pi 5 only
sudo ubuntu-reset prepare-pi --release 26.04.1 --storage /mnt/recovery

ubuntu-reset status
sudo ubuntu-reset plan

# Disposable test system only; explicit confirmation, no immediate reboot by default
sudo ubuntu-reset arm --confirm ERASE-ALL-USERS
sudo ubuntu-reset cancel  # cancel before reboot
```

The GUI's final confirmation uses `arm --confirm ERASE-ALL-USERS --reboot`. Keep recovery storage connected. CLI preparation optionally accepts `--proxy http://127.0.0.1:PORT` or `--proxy socks5h://127.0.0.1:PORT`; TLS and integrity checks remain enabled.

## Build and tests

```sh
sudo apt install build-essential cmake libssl-dev nlohmann-json3-dev \
  rsync squashfs-tools curl gpgv xz-utils util-linux findutils udev \
  initramfs-tools zenity pkexec
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j2
ctest --test-dir build --output-on-failure
(cd build && cpack -G DEB)
```

Official native-Pi verification additionally needs the Ubuntu keyring. Debian's generic path has no `ubuntu-keyring` dependency. Runtime library dependencies, including OpenSSL package names, are derived from each distribution build. No Python dependency is required.

CI builds, tests, packages and non-destructively installs the package in Ubuntu 24.04, Debian 12 and Debian 13 containers on both ARM64 and amd64. Those checks do not validate hardware recovery.

## Firmware and failure boundaries

`ubuntu-reset firmware-status` detects an explicit factory BIOS-reset interface on supported x86 firmware. The separate, confirmed `reset-firmware --confirm RESET-BIOS-DEFAULTS` operation requests defaults only where supported; verify them after reboot. It is not automatically coupled to OS restoration and does not clear EFI variables or Secure Boot keys.

Download/verification/preparation failure does not arm reset. Offline errors stop in rescue/halt rather than booting a partially restored system. **Restoration is not a cross-partition transaction; power loss can damage the OS and boot files. Keep independent rescue media.** See [recovery design](docs/recovery.md) and [validation limits](docs/validation.md).

Implemented independently after reviewing [Resetter](https://github.com/gaining/Resetter), [Timeshift](https://github.com/linuxmint/timeshift) and [Snapper](https://github.com/openSUSE/snapper); no source copied. [Research references](docs/research.md). MIT license; not an official Canonical or Debian product.
