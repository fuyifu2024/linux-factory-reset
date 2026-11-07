# Linux Factory Reset

[English](README.en.md) · [下载 / Releases](https://github.com/fuyifu2024/linux-factory-reset/releases) · [验证范围](docs/validation.md)

C++20 编写的 Linux 镜像下载与实验性恢复工具。项目由 `ubuntu-reset` 更名，旧命令仍可使用。

**当前是开发预览版，还不能作为任意 Linux 电脑的通用“一键恢复出厂设置”。** Debian 13 AMD64 UEFI 虚拟机已通过实际恢复、旧用户及测试文件删除、新管理员创建和关机后的磁盘检查；物理硬件、ARM64 启动和其他发行版的恢复仍需验证。详见 [v0.4 验收记录](docs/validation/v0.4.0-alpha.1.md)。

## 现在能做什么

| 功能 | 实际状态 |
| --- | --- |
| 官网镜像下载 | Ubuntu 24.04 / 26.04、Debian 当前稳定版、Fedora 最新稳定版；AMD64 / ARM64 |
| 下载进度 | 百分比、已下载大小、平均速度、预计剩余时间、取消；官网未提供大小时显示“大小未知” |
| 镜像完整性 | 下载后比较官网 HTTPS 元数据中的 SHA-256，不一致不保存成最终文件；ISO 下载器尚未验独立 PGP 签名 |
| 导入本地恢复资料 | IMG / RAW、XZ 压缩磁盘镜像、独立 QCOW2、本项目 Squashfs；需要符合恢复布局的干净已安装系统 |
| Debian / Ubuntu 恢复后端 | 实验性；普通 ext4 根分区 + 单独 FAT ESP，标准 GRUB，ARM64 / x86_64 UEFI；原生 Ubuntu Pi 引导自动识别 |
| 安装 ISO 自动恢复 | **尚未实现**。下载 ISO 不会启用恢复按钮或设置恢复标记 |
| Fedora 恢复后端 | **尚未实现**。当前支持其官网下载；RPM / dracut / SELinux / Btrfs 恢复适配待开发 |
| Linux Mint | 尚未接入，不假设存在官方 ARM64 镜像 |

ARM64 不限于树莓派。一般 ARM64 UEFI 虚拟机、服务器和兼容电脑使用同一界面。原生 U-Boot 等其他引导方式尚未适配。不能仅凭架构相同就互换板卡 IMG。

## 安装与使用

在 [发布页](https://github.com/fuyifu2024/linux-factory-reset/releases) 选择与系统及架构匹配的 `.deb`。发行版分别提供 Debian 12、Debian 13、Ubuntu 24.04 的 AMD64 / ARM64 构建。二进制需要对应系统的运行库；推荐安装 DEB 以获取依赖。

```sh
sudo apt install ./linux-factory-reset_*.deb
linux-factory-reset gui
```

界面统一提供“下载官方镜像”“准备恢复资料”“查看变更”“恢复系统”。不再要求普通用户自建 HTTPS 地址或手填校验值。自定义管理员恢复源保留在“高级功能”中。

下载官方镜像只需选择发行版、版本、架构和保存目录，不需要管理员权限、第二块磁盘。自动从官网取得地址及校验值；Fedora 不会选择 Beta。

```sh
linux-factory-reset catalog
linux-factory-reset resolve --distro fedora --release stable --edition workstation --arch arm64
linux-factory-reset download --distro debian --release stable --edition netinst --arch arm64 --directory ~/Downloads
```

Ubuntu 可用 `--release 24.04` 或 `26.04`，`--edition desktop` 或 `server`；Fedora 使用 `--release stable`、`--edition workstation` 或 `server`。官网没有所选架构/版本组合时，会明确报错，不切换到非官方镜像。可加 `--proxy http://127.0.0.1:7890`，也支持当前用户的代理环境变量。

## 准备实验性恢复

准备恢复资料需要另一个已挂载的 **ext4 恢复分区**；目前不会创建、格式化分区。虚拟机请先添加第二块虚拟硬盘。可以选择恢复分区内任意文件夹，程序会识别实际挂载点，把资料保存在该盘的 `ubuntu-reset/` 兼容目录。当前系统分区的普通文件夹不能替代恢复分区。

```sh
sudo linux-factory-reset prepare-image --file /path/to/pristine-system.img --storage /mnt/recovery/folder
sudo linux-factory-reset plan
```

镜像必须包含同发行版、同 CPU 架构、匹配引导方式的干净已安装系统：恰好一个 ext4 系统分区和一个 FAT 启动分区，无已有个人用户。ISO 安装盘、Live 根目录、增量 QCOW2、加密磁盘不能直接当作已安装系统。程序会先复制本地文件到私有暂存区，再以只读方式解析；文件计算的哈希只用于完整性记录，不证明来源可信。

只有本机检测到已内置的恢复源时，“准备恢复资料”中才显示自动下载。当前开发分支内置 Debian 13 通用 AMD64/ARM64 UEFI 的官方 NoCloud 基础镜像，以及 Ubuntu 26.04 原生 Pi 引导镜像。Debian 来源用官方 HTTPS SHA-512 清单校验，原生 Ubuntu 来源另校验 OpenPGP 签名。下载在普通用户界面显示进度、速度和取消；管理员导入时重新检查暂存文件校验值。

**Debian 自动来源是不带桌面的基础系统，恢复后在本地控制台创建新管理员。它不是本机最初桌面软件和配置的历史副本。** 普通 Ubuntu UEFI、Debian 12 等设备仍需本地已安装系统镜像或管理员制作的恢复资料；不能把服务器镜像宣传成原来的桌面系统。发布版本的验证情况请看对应 tag 的验收记录。**官网 ISO 下载与可自动恢复的资料不是同一件事。**

“查看变更”需要先准备资料；未准备时会说明下一步，不再报 `Cannot inspect ...state.json`。完整性检查、比较可能较慢，界面显示忙碌状态，真实错误可复制。

确认恢复的命令：

```sh
sudo linux-factory-reset arm --confirm ERASE-ALL-USERS --reboot
# 在重启前取消：
sudo linux-factory-reset cancel
```

此操作会在下次 initramfs 启动阶段覆盖系统和启动文件，删除全部旧用户、个人文件、额外软件及配置。恢复介质必须一直连接。恢复不是断电事务，也不是磁盘安全擦除；不支持 Btrfs、LVM、加密、RAID、额外 `/home` 或 `/boot` 等分区。首次开机需要创建新管理员，不保留原账号。不会因为下载镜像或预览就恢复、重启、改 BIOS。

## 虚拟机图形兼容与升级

界面默认使用 Cairo 软件绘制，避免缺少 GPU 驱动时反复出现 Mesa / EGL / Zink 错误；不修改系统图形设置，保留用户显式设置。仍有问题可用终端运行以取得实际错误。

```sh
GSK_RENDERER=cairo LIBGL_ALWAYS_SOFTWARE=1 linux-factory-reset gui
```

原 `ubuntu-reset` 命令是兼容入口。内部状态、initramfs hook 及恢复资料目录保留旧名字，避免升级时出现两套恢复标记。新版 DEB 替换旧包，**不会**自动准备资料或设置恢复标记。

## 构建与验证

```sh
sudo apt install build-essential cmake libssl-dev libcurl4-openssl-dev nlohmann-json3-dev rsync squashfs-tools
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

依赖及集成测试见 [CI](.github/workflows/ci.yml)。测试使用临时目录、合成系统树、隔离容器里的本地 HTTPS 服务器和无 GPU 的虚拟显示。不会在开发者或用户主机上执行真实恢复。通过构建、目录恢复测试不等于证明真实系统能够恢复后启动。更多限制见 [验证记录](docs/validation.md)、[恢复协议](docs/recovery.md)。
