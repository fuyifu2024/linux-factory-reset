# Ubuntu Reset

**中文桌面入口 + C++20 主程序 + initramfs 离线恢复脚本。** 目标是让 Ubuntu 自动下载可信的初始系统资料，在重启后清除原用户、个人文件和新增软件，恢复默认系统，无须手工运行安装向导或格式化分区。

> **v0.1.0-alpha.1 是实验版。临时目录中的恢复集成测试已实现；真实树莓派、x86 的重启恢复和首次开机流程尚未验证。请仅在可丢弃的测试系统上验证，不要把它用于唯一的工作系统。**

## 实际怎么恢复

1. 树莓派模式下载 Ubuntu 官方预装桌面镜像，先验证 `SHA256SUMS.gpg` 的 Ubuntu 签名，再验证镜像 SHA-256。
2. 从原始系统提取文件、默认配置、初始包数据库和启动文件，生成只读 `factory.squashfs`，保存到**独立 ext4 恢复分区**。
3. `plan` 使用校验和预览增删改。GUI 中确认“清空全部用户”后，程序生成含恢复脚本的 initramfs，最后才写入待恢复标记。
4. 下次启动、进入桌面和用户服务之前，核对目标分区 UUID、恢复资料哈希、挂载布局和容量。
5. 对照原始文件执行恢复：内容相同的文件复用，缺失/被改动的文件修复，多余文件和原用户删除。启动分区也恢复，并写入本机分区 UUID。
6. 重启进入原始系统的首次设置。树莓派使用原镜像的流程；自建 x86 配置使用本项目的本地控制台创建管理员流程。

**首版第一次仍下载完整原始镜像，然后按文件差异写入；并非只从网络下载差异文件。** 无须提前给当前旧系统做快照。官方原始资料能恢复“该镜像的初始环境”，不能推断一台电脑几年前安装时的全部自定义选择。

恢复会移除原账户、`/home`、`/root` 中的个人文件及系统分区上的额外软件。它不是磁盘安全擦除，未覆盖的数据块可能被恢复。恢复分区保留；其他磁盘不属于清除范围。原始系统中不包含本工具时，本工具也会被移除，之后需重新安装使用。

## 支持范围

| 平台 | 恢复源 | 首版条件 |
|---|---|---|
| Raspberry Pi 5 / ARM64 | 官方 Ubuntu 预装桌面 `.img.xz`，自动下载及验签 | 普通 ext4 根分区、独立 FAT `/boot/firmware`、标准 Ubuntu `current/` 启动配置 |
| x86_64 / UEFI | 厂商或管理员制作并通过 HTTPS 发布的本项目 `.squashfs` | 普通 ext4 根分区、FAT `/boot/efi`、标准 `EFI/ubuntu/grub.cfg` |

**通用 x86 Ubuntu 安装 ISO 的 live 根文件系统不等于已经安装的系统。首版不提供“任意 x86 电脑直接下载 ISO 就恢复”的功能；必须先有可信的、匹配该机型的干净已安装系统配置。** 详见 [制作 x86/OEM 恢复资料](docs/profiles.md)。

LUKS、LVM、RAID、Btrfs、独立 `/home`、独立 `/boot`、双系统恢复和自定义引导不在首版范围内，检测到不支持的布局会拒绝继续。额外挂载在 `/mnt`、`/media` 的磁盘不清理。没有真实硬件兼容性保证。

## 安装与构建

从 [Releases](https://github.com/fuyifu2024/ubuntu-reset/releases) 下载匹配架构的 `.deb`，使用 `sudo apt install ./文件名.deb` 安装。安装包只安装程序和休眠的 hook，**不会自动准备资料、生成 initramfs、设置恢复标记或重启**。

Releases 同时提供 `ubuntu-reset-linux-arm64` / `ubuntu-reset-linux-amd64` 原生可执行文件和 SHA-256；它们动态链接系统运行库，完整恢复功能建议通过 DEB 安装所需脚本。源码就在本仓库，发行页也可下载源码归档。

源码构建：

```sh
sudo apt install build-essential cmake libssl-dev nlohmann-json3-dev \
  rsync squashfs-tools curl gpgv ubuntu-keyring xz-utils util-linux \
  initramfs-tools zenity pkexec
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build -j2
ctest --test-dir build --output-on-failure
cd build
cpack -G DEB
```

构建产物的主程序没有 Python 依赖。主程序用 C++20，界面对话框调用 Zenity，恢复和首次设置只使用 Shell 与系统工具。

## 使用

应用菜单打开 **Ubuntu 恢复出厂设置**，或运行：

```sh
ubuntu-reset gui
```

以下是测试环境中的 CLI 流程。先自行准备并挂载独立 ext4 恢复分区；本工具不创建、不格式化分区。准备过程峰值空间包含压缩下载、展开镜像、文件暂存及生成的恢复资料，建议使用 **64 GB 或更大的恢复介质**，实际仍由容量检查决定。

```sh
# 下载、验签、建立树莓派恢复资料；尚不清除当前系统
sudo ubuntu-reset prepare-pi --release 26.04.1 --storage /mnt/recovery

# 查看准备状态和只读增删改清单
ubuntu-reset status
sudo ubuntu-reset plan

# 仅当你确实准备清除测试系统时执行；默认不立即重启
sudo ubuntu-reset arm --confirm ERASE-ALL-USERS

# 重启之前可以取消
sudo ubuntu-reset cancel
```

GUI 确认恢复后会使用 `arm --confirm ERASE-ALL-USERS --reboot`，即准备完成后自动重启。恢复介质必须保持连接。再次准备不会自动删除旧的恢复资料，管理员需自行管理磁盘空间。

下载站直连受限时，CLI 准备命令可显式增加 `--proxy http://127.0.0.1:端口` 或 `--proxy socks5h://127.0.0.1:端口`，使用你已有的本地代理。它不会关闭 TLS/签名/哈希校验，不接受代理密码，也不会改系统网络配置。

## UEFI / BIOS

系统还原和 BIOS 默认值是独立功能。仅在 x86 内核明确提供单个支持 `factory` 的 `reset_bios` 接口时，程序允许请求默认设置：

```sh
ubuntu-reset firmware-status
sudo ubuntu-reset reset-firmware --confirm RESET-BIOS-DEFAULTS
```

该命令独立确认，不自动重启、不随系统恢复隐式执行，不删除 EFI 变量或 Secure Boot 密钥。固件密码、主板实现及重启后的实际应用结果由固件决定；需在固件设置中核实。不支持时请使用厂商的设置界面。`systemctl reboot --firmware-setup` 仅进入设置界面，不代表重置。

## 验证与故障处理

测试覆盖哈希、下载地址、签名清单解析、路径/UUID 注入拦截、恢复源个人账户拦截、原始资料生成、账户/数据删除、默认文件修复、保留相同文件、恶意目录链接和失败返回。所有恢复集成测试只操作临时目录。

已执行与尚未执行的验证逐项记录在 [验证记录](docs/validation.md)。

校验失败、下载失败或准备失败不会设置恢复标记。离线恢复开始后发生错误，会停留在 initramfs 救援环境，不启动恢复到一半的系统。恢复不是跨文件系统事务；断电可能损坏系统/启动分区，尤其启动文件替换期间，需要独立救援介质。具体人工恢复方法见 [恢复设计和故障边界](docs/recovery.md)。

## 同类项目与设计依据

先做了 GitHub 同类项目检索，再实现本项目：

- [Resetter](https://github.com/gaining/Resetter)：目标相近，已归档，作者停止维护。
- [Timeshift](https://github.com/linuxmint/timeshift)：维护中的系统快照恢复工具，默认侧重保留个人文件。
- [Snapper](https://github.com/openSUSE/snapper)：文件系统快照管理、比较与撤销更改。

本项目独立实现，未复制这些项目的代码，也不依赖 Timeshift。参考来源和研究结果见 [调研记录](docs/research.md)。

MIT License。本项目不是 Canonical 官方产品，不代表 Ubuntu 的支持承诺。
