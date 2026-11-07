# 实现前的调研

检索日期：2026-11-07。先通过 GitHub 仓库检索和项目原始 README 检查同类方案，再编写代码。

| 项目 | 当日观察 | 与本项目关系 |
|---|---|---|
| [gaining/Resetter](https://github.com/gaining/Resetter) | 仓库已归档；作者声明停止维护 | 旧式 Ubuntu/Debian 重置方案，证明此需求有同类实现 |
| [linuxmint/timeshift](https://github.com/linuxmint/timeshift) | 未归档；提供 rsync/Btrfs 系统快照 | 主要恢复预存状态，默认排除用户个人文件 |
| [openSUSE/snapper](https://github.com/openSUSE/snapper) | 未归档；比较及恢复文件系统快照差异 | 文件系统快照管理，适合受支持的快照布局 |

本项目最终选择**从可信原始系统建立参照、在启动前按差异恢复**，删除原个人用户，并非 Timeshift 封装。无复制上述项目源码。

原始资料：

- [Ubuntu 26.04.1 官方发布目录](https://cdimage.ubuntu.com/releases/26.04.1/release/)：预装 Pi 镜像、manifest、校验和与签名。软件包 manifest 不是完整的默认文件内容，不能单凭它恢复被覆盖文件。
- [Ubuntu initramfs-tools 手册](https://manpages.ubuntu.com/manpages/focal/man7/initramfs-tools.7.html)：`local-bottom` 在根挂载后、切换到安装系统前执行，hook 使用 `copy_exec` 包含所需程序。
- [Linux firmware-attributes ABI](https://github.com/torvalds/linux/blob/master/Documentation/ABI/testing/sysfs-class-firmware-attributes)：受支持设备的 `reset_bios` 接口、`factory` 值及重启要求；接口并非每台主板都有。
- [fwupd BIOS 设置文档](https://github.com/fwupd/fwupd/blob/main/docs/bios-settings.md)：内核仅暴露平台支持的设置。
- [systemd bootctl 文档](https://www.freedesktop.org/software/systemd/man/latest/bootctl.html)：进入固件设置与恢复固件默认值不同。

## Debian / generic ARM64 extension

- [Debian UEFI documentation](https://wiki.debian.org/UEFI): Debian UEFI support includes arm64 and amd64. A CPU architecture alone does not specify every board's boot chain; native U-Boot and other firmware layouts need explicit support.
- [Debian Reference, system initialization](https://www.debian.org/doc/manuals/debian-reference/ch03.en.html): standard Debian UEFI uses `EFI/debian/grub.cfg` to find `/boot/grub/grub.cfg` by UUID.
- [Debian GRUB installer source](https://sources.debian.org/src/grub2/2.12-9%2Bdeb13u2/util/grub-install.c/): ARM64 and x86_64 EFI suffixes are `aa64` and `x64` respectively.
- [Debian 12 libssl3](https://packages.debian.org/bookworm/libssl3) and [Debian 13 libssl3t64](https://packages.debian.org/trixie/libssl3t64): package runtime dependencies must be derived from the actual build distro instead of hardcoding Ubuntu's library package names.
