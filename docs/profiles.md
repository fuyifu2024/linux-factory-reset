# Installed-system recovery profiles / 已安装系统恢复资料

[恢复及回退协议 / Recovery protocol](recovery.md)

## 中文

资料是完整系统执行权限。请选择可信、未初始化的同发行版和 CPU 架构镜像；SHA 校验只证明完整性，不能代替发布者审查。它不是根据包清单猜测本机历史配置，也不能直接导入安装 ISO / Live 根目录。通用 ARM64 只支持 UEFI/GRUB；不能凭 ARM64 相同就互换板卡镜像。

自动来源包括 Debian 13 NoCloud、Ubuntu 24.04/26.04 Server、Fedora 44/45 Cloud Base，以及原生 Ubuntu 26.04 Pi Desktop。**通用 UEFI 自动来源没有桌面，不是原桌面安装的精确历史副本。** Debian 使用官网 HTTPS SHA-512；Ubuntu 通用云镜像和 Fedora 使用官网 HTTPS SHA-256；原生 Ubuntu Pi 镜像另检查 OpenPGP 签名。没有将普通 ISO 下载宣传为自动恢复。

常规用户使用 `prepare-auto` 或 `prepare-image`。IMG/RAW、IMG.XZ/RAW.XZ、独立 QCOW2 可导入。源可包含 Ubuntu 的独立 ext4 `/boot` 或 Fedora 官方 root/boot/home/var Btrfs 子卷；在私有暂存区合并并生成适用于平铺 ext4 **目标**的资料。Fedora 转换会以管理员权限运行可信镜像中的 dracut，生成不包含原 Btrfs 子卷命令行的通用 initramfs；chroot 不是安全沙箱。不要选择不可信磁盘镜像。具体已实测的发行版、版本及架构，以对应 release 验收记录为准。

管理员也可将根和 ESP 只读挂载后制作：

```sh
sudo linux-factory-reset make-profile --root /mnt/pristine-root --boot /mnt/pristine-esp \
  --platform arm64-uefi --output /mnt/recovery/factory.squashfs
# AMD64 使用 --platform x86_64-uefi
```

该接口要求已经规范化的 ext4 根 + ESP 布局；发行版使用标准 `/boot/grub/grub.cfg` 或 Fedora 的 `/boot/grub2/grub.cfg` 及 `EFI/<发行版>/grub.cfg`。必须具有匹配的内核、initramfs、modules、sudo/passwd/账户工具及 sudo/wheel 组，无个人用户和已运行的 cloud-init。程序清空 root 私人目录、锁定 root、清除 machine-id/SSH 主机密钥等身份，并加入本地 tty1 首次开户。旧 Ubuntu schema 1 兼容；通用资料使用 schema 2。

开发者可用 `make-profile-image --file /path/offline.raw --distro fedora --platform x86_64-uefi --output /mnt/recovery/factory.squashfs` 单独验证来源转换，不设置恢复标记。该高级接口使用离线普通 RAW 文件；普通用户导入请使用会先复制及校验镜像的 `prepare-image`。

## English

Trusting a profile means trusting a complete operating system. Use a pristine image matching the target distribution and architecture. Checksums establish integrity, not publisher trust. Installer/live ISOs and historical reconstruction from package lists are unsupported. ARM64 profiles are not interchangeable across arbitrary boards.

Built-in sources are Debian 13 NoCloud, Ubuntu 24.04/26.04 Server, Fedora 44/45 Cloud Base, and native Ubuntu Pi Desktop. **Generic UEFI sources have no desktop and do not recreate the exact original desktop installation.** Official HTTPS checksums are verified; the native Ubuntu Pi source additionally verifies its OpenPGP manifest signature. See each release's acceptance evidence for tested combinations.

`prepare-auto` and `prepare-image` are the ordinary interfaces. Ubuntu split-boot sources and Fedora's official Btrfs root/boot/home/var source subvolumes are merged in private staging into profiles for the supported flat ext4 target. Fedora conversion runs the trusted image's dracut as root to remove source-specific Btrfs boot assumptions. A chroot is not a security sandbox; do not import untrusted images.

The administrator `make-profile` example above accepts an already normalized, read-only ext4 root and ESP with the distro's standard GRUB files, matching kernel/initramfs/modules, account tools and sudo/wheel group. Personal users and completed cloud initialization are rejected. The builder clears root personal files and machine identities, locks root and adds local tty1 account creation. Schema 2 covers generic targets; legacy Ubuntu schema 1 remains readable.

`make-profile-image` is an advanced offline RAW conversion interface that never arms a reset. The ordinary `prepare-image` interface additionally snapshots and verifies its input before import. Native Pi boot keeps its separate automatically detected adapter; other U-Boot/native firmware layouts need dedicated adapters.
