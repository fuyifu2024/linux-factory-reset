# Linux Factory Reset

[中文](README.md) · [Releases](https://github.com/fuyifu2024/linux-factory-reset/releases) · [Acceptance evidence](docs/validation/v0.5.0-alpha.1.md)

A C++20 official-image downloader and experimental Linux recovery tool, with small offline Shell components. Previously named `ubuntu-reset`; the old command remains compatible.

**The Beta candidate is not released:** two successive GUI/CLI restores and interrupted-write rollback passed. ARM64 restored and booted into initial setup, but completed administrator creation and real sudo remain unverified. See the [candidate record](docs/validation/v0.5.0-beta.1.md).

**v0.5 is an experimental preview for the systems and layouts listed in its acceptance record.** Actual AMD64 UEFI VMs running Ubuntu 26.04, Debian 13 and Fedora 45 have completed automatic restoration, old-account/file removal, new-administrator setup and real sudo checks. ARM64 has native build and source-conversion evidence, without guest-boot validation. Consult the exact evidence and fault-test scope.

| Feature | Scope |
| --- | --- |
| Official ISO downloads | Ubuntu 24.04/26.04, Debian stable, Fedora stable; AMD64/ARM64 |
| Download display | Percentage, bytes, average speed, ETA, cancellation; final file published only after verification |
| Automatic recovery sources | Debian 13 NoCloud, Ubuntu 24.04/26.04 Server, Fedora 44/45 Cloud Base; native Ubuntu Pi boot detected automatically |
| Local sources | Trusted pristine installed-system IMG/RAW, IMG.XZ/RAW.XZ, standalone QCOW2 and project Squashfs profiles |
| Generic targets | Plain ext4 root + separate FAT ESP, standard GRUB, x86_64/ARM64 UEFI; independent rescue currently requires Secure Boot already disabled |
| Recovery engines | Debian/Ubuntu initramfs-tools; Fedora dracut, SELinux-aware first-user setup and wheel administrator |
| Rescue and rollback | Temporary independent EFI entry, offline rollback copy and recovery journal; rollback after write failure/interruption, temporary personal data removed after success |
| Not supported | Automatic installer-ISO deployment, Btrfs **targets**, LVM, encryption, RAID, separate `/boot` or `/home`, shared/OEM ESPs, Mint |

**Generic automatic sources are base/server systems without a desktop. They do not recreate the exact original desktop installation.** Fedora's official Btrfs source and Ubuntu split-boot sources can be normalized in staging; existing Btrfs or split-boot targets remain unsupported. ARM64 is not limited to Raspberry Pi, but generic devices need UEFI; other native board firmware/U-Boot flows need adapters.

## Install

Choose the package matching your distribution and architecture. Builds cover Debian 12/13 and Ubuntu 24.04 AMD64/ARM64 DEBs, plus Fedora 45 RPMs for both architectures. Standalone binaries need matching runtime libraries. Use the capabilities and evidence of the tag you download.

```sh
sudo apt install ./linux-factory-reset_*.deb
# Fedora: sudo dnf install ./linux-factory-reset*.rpm
linux-factory-reset gui
```

The shared interface provides official downloads, recovery preparation, change preview, reset and cancellation. Ordinary users do not need a custom HTTPS server or manually entered checksum. Cairo software rendering avoids missing virtual-GPU driver problems without changing system graphics settings.

## Download and recover

Downloading an ISO needs neither root nor a second disk and never schedules a reset. URLs and checksums come from official HTTPS metadata, with progress, speed and cancellation. Fedora Beta is excluded from stable selection.

```sh
linux-factory-reset download --distro ubuntu --release 26.04 --edition desktop --arch arm64 --directory ~/Downloads
linux-factory-reset resolve --distro fedora --release stable --edition workstation --arch amd64
```

Ubuntu editions are `desktop`/`server`, Fedora `workstation`/`server`, Debian `netinst`. Unavailable combinations fail explicitly. An optional loopback proxy can be supplied with `--proxy http://127.0.0.1:7890`.

Recovery needs another mounted **ext4 recovery disk**, with space for the pristine image, staging and a temporary old-system rollback copy. The application does not automatically partition or format disks. Add a second virtual disk for VM evaluation; any directory on its mounted filesystem can be selected.

```sh
linux-factory-reset sources
sudo linux-factory-reset prepare-auto --storage /mnt/recovery
# Or: sudo linux-factory-reset prepare-image --file /path/to/pristine.img --storage /mnt/recovery
sudo linux-factory-reset plan
sudo linux-factory-reset arm --confirm ERASE-ALL-USERS --reboot
# Cancel before reboot: sudo linux-factory-reset cancel
```

Preparation, downloads and previews do not perform restoration. Once armed, the next boot restores system/boot files offline and removes all old personal users, data, extra software and configuration. Keep the recovery disk connected. BootOrder is temporarily adjusted; BIOS defaults are not reset and Secure Boot is not disabled automatically. A new administrator is created locally, with root locked.

Successful reset deletes temporary rollback data and the host-generated rescue initramfs. Successful rollback intentionally restores old accounts and files. This is not A/B boot management, secure erasure or a transaction guarantee against damaged hardware and every post-reset driver failure. Native Ubuntu Pi boot retains its earlier flow without this UEFI rollback protocol. See the [recovery protocol](docs/recovery.md) and [profile guide](docs/profiles.md).

ISO downloads use official HTTPS SHA-256 metadata. Debian recovery uses SHA-512; native Ubuntu Pi additionally verifies OpenPGP. Generic Ubuntu cloud and Fedora recovery currently use official HTTPS SHA-256 without detached PGP verification. Fedora conversion runs the trusted source's dracut as root; never select untrusted system images.

## Build and validate

```sh
sudo apt install build-essential cmake libssl-dev libcurl4-openssl-dev nlohmann-json3-dev rsync squashfs-tools
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Heavy builds, large images and actual restoration tests run on disposable remote runners, not the user's Raspberry Pi. CI covers native architectures, package installation, HTTPS download/cancellation, GPU-free GUI and directory fixtures. Disposable UEFI VMs separately exercise boot, account/data removal, a usable sudo administrator and rollback. Successful ARM64 source conversion does not establish ARM guest boot or physical-hardware compatibility.
