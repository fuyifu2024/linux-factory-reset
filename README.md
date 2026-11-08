# Linux Factory Reset

[English](README.en.md) · [下载 / Releases](https://github.com/fuyifu2024/linux-factory-reset/releases) · [验收记录](docs/validation/v0.5.0-alpha.1.md)

C++20 编写的 Linux 官方镜像下载与实验性恢复工具，恢复阶段使用少量 Shell。原名 `ubuntu-reset`，旧命令仍兼容。

**v0.5 是实验性开发预览版，仅面向验收记录列出的系统与布局。** Ubuntu 26.04、Debian 13、Fedora 45 的 AMD64 UEFI 虚拟机已完成真实自动恢复、旧用户及文件删除、新管理员创建和 sudo 检查；ARM64 已验证原生构建和镜像转换，尚未验证客体启动。具体范围及故障测试见验收记录。

| 功能 | 范围 |
| --- | --- |
| 官网 ISO 下载 | Ubuntu 24.04/26.04、Debian 稳定版、Fedora 稳定版；AMD64/ARM64 |
| 下载显示 | 百分比、大小、平均速度、预计时间、取消；校验成功后才保存最终文件 |
| 内置自动恢复源 | Debian 13 NoCloud、Ubuntu 24.04/26.04 Server、Fedora 44/45 Cloud Base；另自动识别原生 Ubuntu Pi 引导 |
| 本地来源 | IMG/RAW、IMG.XZ/RAW.XZ、独立 QCOW2、本项目 Squashfs；必须是可信的干净已安装系统 |
| 通用恢复目标 | ext4 根分区 + 单独 FAT ESP、标准 GRUB、x86_64/ARM64 UEFI；当前独立救援入口要求 Secure Boot 已关闭 |
| 恢复引擎 | Debian/Ubuntu initramfs-tools；Fedora dracut、SELinux 首次开户及 wheel 管理员 |
| 救援与回退 | 临时独立 EFI 启动入口、离线回退副本、恢复阶段记录；写入失败或中断后回退，成功后清除临时个人资料 |
| 尚不支持 | 安装 ISO 自动恢复、Btrfs **目标**、LVM、加密、RAID、额外 `/boot` 或 `/home` 分区、共享/OEM ESP、Mint |

**通用 UEFI 自动来源是不带桌面的基础/服务器系统，不会精确复原原来的桌面软件和配置。** Fedora 官方 Btrfs 来源、Ubuntu 独立 `/boot` 来源可以在暂存区转换；这不表示当前已安装的 Btrfs 或独立 `/boot` 目标能够直接重置。ARM64 不限于树莓派，但通用路径要求 UEFI；其他原生 U-Boot/板卡固件仍需适配。

## 安装

从发布页选择与发行版、架构匹配的包。提供 Debian 12/13、Ubuntu 24.04 的 AMD64/ARM64 DEB，以及 Fedora 45 的双架构 RPM；单独二进制需要对应运行库。请以所下载 tag 的功能与验收范围为准。

```sh
sudo apt install ./linux-factory-reset_*.deb
# Fedora：sudo dnf install ./linux-factory-reset*.rpm
linux-factory-reset gui
```

统一界面提供“下载官方镜像”“准备恢复资料”“查看变更”“恢复系统”“取消恢复”。普通流程不需要自建 HTTPS 地址或手填校验值。默认 Cairo 软件绘制，兼容缺少虚拟 GPU 驱动的环境；不会修改系统图形配置。

## 下载与恢复

仅下载 ISO 不需要管理员权限或第二块磁盘，也不会触发恢复。下载器自动获取官网 HTTPS 地址及校验值，显示进度和速度，可取消；不会将 Fedora Beta 当成稳定版。

```sh
linux-factory-reset download --distro ubuntu --release 26.04 --edition desktop --arch arm64 --directory ~/Downloads
linux-factory-reset resolve --distro fedora --release stable --edition workstation --arch amd64
```

Ubuntu 下载类型为 `desktop` / `server`，Fedora 为 `workstation` / `server`，Debian 为 `netinst`。官网没有对应组合时停止。可加 `--proxy http://127.0.0.1:7890`。

恢复资料需要另一块已挂载的 **ext4 恢复盘**，并预留容纳初始镜像、临时文件和旧系统回退副本的空间。本程序不自动分区、格式化磁盘。虚拟机请先添加第二块虚拟硬盘；可选择该盘任意文件夹，程序自动识别挂载点。

```sh
linux-factory-reset sources
sudo linux-factory-reset prepare-auto --storage /mnt/recovery
# 或：sudo linux-factory-reset prepare-image --file /path/to/pristine.img --storage /mnt/recovery
sudo linux-factory-reset plan
sudo linux-factory-reset arm --confirm ERASE-ALL-USERS --reboot
# 重启前取消：sudo linux-factory-reset cancel
```

准备、下载和预览都不会执行恢复。确认后下次启动离线覆盖系统及启动文件，删除所有旧个人账号、文件、额外软件和配置。恢复盘应始终连接。UEFI 启动顺序只在恢复期间临时调整，不会自动恢复 BIOS 默认值或关闭 Secure Boot。恢复后在本地控制台新建管理员，root 保持锁定。

完成正常恢复后清除临时回退副本及宿主恢复 initramfs；成功回退则有意恢复原账号和文件。它不是 A/B 系统、磁盘安全擦除或对损坏硬件的事务保证，不能覆盖任意恢复后驱动故障。原生 Ubuntu Pi 路径暂未加入此独立 UEFI 回退协议。细节见 [恢复协议](docs/recovery.md) 和 [资料制作](docs/profiles.md)。

官网 ISO 使用 HTTPS 元数据 SHA-256；Debian 恢复源使用 SHA-512，原生 Ubuntu Pi 来源另校验 OpenPGP。通用 Ubuntu 云镜像和 Fedora 来源使用官网 HTTPS SHA-256，尚未验证独立 PGP 签名。Fedora 镜像转换会以管理员权限运行可信来源中的 dracut；不要选择不可信系统镜像。

## 构建与验证

```sh
sudo apt install build-essential cmake libssl-dev libcurl4-openssl-dev nlohmann-json3-dev rsync squashfs-tools
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

本项目的重编译、大镜像和真实恢复验收运行在云端一次性环境，不在用户的树莓派上执行。CI 包含原生 AMD64/ARM64 构建、包安装、HTTPS 下载/取消、无 GPU GUI 和临时目录测试；真实启动、清除用户、创建可 sudo 的新管理员及回退另由可丢弃 UEFI VM 验证。ARM64 镜像导入通过不代表 ARM 客体启动或物理硬件已验证。
