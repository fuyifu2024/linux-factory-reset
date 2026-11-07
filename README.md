# Ubuntu / Debian Reset

[English](README.en.md) · [发行版 / Releases](https://github.com/fuyifu2024/ubuntu-reset/releases) · [验证记录](docs/validation.md)

**C++20 主程序 + 中文桌面入口 + initramfs 离线恢复脚本。** 下载可信的初始系统资料，重启后删除原用户、个人文件和额外软件，恢复初始环境，不格式化分区、不手动操作安装向导。为兼容旧版，仓库名、命令和包名仍为 `ubuntu-reset`。

> **v0.2.0-alpha.1 是实验版。** 已加入 Debian 和通用 ARM64 UEFI 流程、文件系统测试及跨发行版打包测试；真实硬件的完整重启恢复、首次设置和 BIOS 重置仍未验证。只应在可丢弃的测试系统中验收。

## 支持范围

| 系统与平台 | 引导布局 | 恢复源 |
|---|---|---|
| Debian 12 / 13，通用 ARM64 | UEFI + GRUB，ext4 `/`、FAT `/boot/efi`、`EFI/debian` | 管理员/厂商制作的匹配设备的可信恢复资料 |
| Ubuntu，通用 ARM64 | UEFI + GRUB，ext4 `/`、FAT `/boot/efi`、`EFI/ubuntu` | 管理员/厂商制作的匹配设备的可信恢复资料 |
| Debian / Ubuntu，x86_64 | 同上，使用各自的 EFI 目录及 x64 引导文件 | 管理员/厂商恢复资料 |
| Ubuntu，树莓派 5 原生固件 | ext4 `/`、FAT `/boot/firmware`，Ubuntu `current/` 布局 | 自动下载、验签官方 Ubuntu 预装桌面镜像 |

**ARM64 不再等于树莓派。** ARM64 服务器、UEFI 虚拟机、UEFI ARM64 电脑及使用 UEFI/GRUB 的开发板均可走通用 `arm64-uefi` 路径，但仍需符合布局并使用匹配设备的恢复源，不能据此声称所有设备已通过验收。树莓派以 UEFI 启动时也走通用路径。

不直接支持非 UEFI 的 U-Boot/extlinux/裸固件布局、ARM32、LUKS、LVM、RAID、Btrfs、独立 `/home` 或 `/boot`、双系统和自定义引导。Debian 在树莓派原生固件下的其他布局需要专门适配；现有树莓派原生固件适配器用于 Ubuntu。平台检测依据 CPU、UEFI、发行版及实际文件系统布局。

**Debian 和通用 ARM64 的恢复源下载使用 `prepare-profile`，并非自动下载任意安装 ISO。** ISO/live 环境不等于已安装系统，跨 Debian/Ubuntu 替换会被拒绝。恢复资料如何制作见 [profiles.md](docs/profiles.md)。

## 恢复过程

1. 下载官方 Ubuntu Pi 镜像并验证 Ubuntu GPG 签名、SHA-256；或下载你信任的 Debian/Ubuntu `.squashfs` 恢复源并验证独立获得的 SHA-256。
2. 将完整原始文件参照保存到**独立 ext4 恢复分区**。首次准备仍下载完整资料，不是网络差分下载。
3. `plan` 只读预览文件增删改。明确确认后，生成恢复 initramfs，最后才写入待恢复标记。
4. 下次启动，在桌面和用户服务运行之前，验证 UUID、资料哈希、容量、挂载布局，再按文件校验和恢复。内容相同的文件不重写；缺失或改变的文件修复，多余文件和原用户删除。
5. 按发行版与 CPU 修复引导指针：Debian/Ubuntu 的 `EFI/debian` / `EFI/ubuntu`，ARM64 的 `grubaa64.efi` 或 x86_64 的 `grubx64.efi`。
6. 重启进入首次设置：通用 UEFI 资料带有本地 tty1 创建管理员的流程；原生 Ubuntu Pi 资料保留官方镜像的流程。

资料代表所选原始系统，无法推断电脑多年前安装时的全部自定义选择。恢复删除原 `/home` 和 `/root` 数据，但不等于磁盘安全擦除，未覆盖的数据块可能恢复。其他磁盘和恢复分区保留。本工具若不在恢复源中，恢复后也会被移除。

## 下载与安装

[Releases](https://github.com/fuyifu2024/ubuntu-reset/releases) 提供**中英双语说明**、源码归档、ARM64/amd64 原生二进制、DEB 和 SHA-256。请按系统选择 `ubuntu24.04`、`debian12` 或 `debian13`，再按 CPU 选择 `arm64` 或 `amd64`；`ubuntu24.04` 构建也用于满足依赖的更新 Ubuntu 版本。ARM64 二进制适用于对应的 AArch64 运行库，不限定板卡品牌。

```sh
sudo apt install ./匹配系统与架构的安装包.deb
ubuntu-reset gui
```

裸二进制动态链接系统库，完整恢复功能请通过 DEB 安装配套脚本。本包没有自动设置恢复标记、重建 initramfs 或重启的安装脚本；安装依赖本身可能触发发行版正常的维护操作。

## CLI

先自行准备并挂载独立 ext4 恢复分区；程序不创建、不格式化分区。首次准备需要原下载、展开镜像、暂存及压缩资料的空间，建议恢复介质 64 GB 或更大，仍以容量检查为准。

```sh
# Debian / Ubuntu：匹配本机发行版和 ARM64/x86_64 的可信已安装系统恢复源
sudo ubuntu-reset prepare-profile \
  --url https://your-trusted-server.example/factory.squashfs \
  --sha256 YOUR_INDEPENDENTLY_TRUSTED_SHA256 \
  --storage /mnt/recovery

# 仅 Ubuntu 树莓派 5 原生固件模式：自动获取官方预装镜像
sudo ubuntu-reset prepare-pi --release 26.04.1 --storage /mnt/recovery

ubuntu-reset status
sudo ubuntu-reset plan

# 在可丢弃的测试系统中，确认准备清空全部用户后执行；默认不立即重启
sudo ubuntu-reset arm --confirm ERASE-ALL-USERS
sudo ubuntu-reset cancel  # 重启前可取消
```

GUI 最终确认后使用 `arm --confirm ERASE-ALL-USERS --reboot`。恢复介质必须保持连接。下载受限时可增加 `--proxy http://127.0.0.1:端口` 或 `--proxy socks5h://127.0.0.1:端口`，不关闭 TLS/签名/哈希检查。

## 构建与测试

Debian 12、13 和 Ubuntu 可使用：

```sh
sudo apt install build-essential cmake file libssl-dev nlohmann-json3-dev \
  rsync squashfs-tools curl gpgv xz-utils util-linux findutils udev \
  initramfs-tools zenity pkexec
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j2
ctest --test-dir build --output-on-failure
(cd build && cpack -G DEB)
```

原生 Ubuntu Pi 的官方验签另需 Ubuntu keyring。Debian 的通用恢复流程不依赖 `ubuntu-keyring`；OpenSSL 运行库包名由各发行版构建环境自动解析。主程序及恢复流程无 Python 依赖。

CI 在 Ubuntu 24.04、Debian 12、Debian 13 容器中分别进行 ARM64 和 amd64 构建、测试、打包及非破坏性安装检查。它们验证软件/打包行为，不代表真实硬件恢复已验收。

## 固件及故障边界

`ubuntu-reset firmware-status` 检查 x86 主板是否提供明确的 `reset_bios/factory` 接口。只有支持时，独立命令 `sudo ubuntu-reset reset-firmware --confirm RESET-BIOS-DEFAULTS` 才可请求默认值，需重启后核实；不会自动随系统还原执行，不删除 EFI 变量或 Secure Boot 密钥。

下载、验签或准备失败不会设置恢复标记。离线恢复中途失败会停在救援环境/停止状态。**恢复不是跨分区事务，断电可能损坏系统及引导，需要独立救援介质。** 见 [恢复设计](docs/recovery.md) 与 [验证边界](docs/validation.md)。

本项目先调研了 [Resetter](https://github.com/gaining/Resetter)、[Timeshift](https://github.com/linuxmint/timeshift)、[Snapper](https://github.com/openSUSE/snapper)，然后独立实现，未复制其源码。来源见 [调研记录](docs/research.md)。MIT License；非 Canonical 或 Debian 官方产品。
