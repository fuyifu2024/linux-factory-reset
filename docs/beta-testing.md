# Beta public testing / Beta 公测

**Validation update / 验收更新（2026-11-09）：** Ubuntu 26.04 ARM64 UEFI now passed restoration, reboot, first-user creation and real sudo, on the published Beta tag. Ubuntu 26.04 ARM64 UEFI 已在发布标签上通过恢复、重启、首次开户与实际 sudo 验证。Fedora ARM64 guest-boot acceptance and physical hardware remain unverified / Fedora ARM64 启动及物理硬件仍未通过验收。具体方法和范围见 [验证记录](validation/v0.5.0-beta.1.md)。

[中文](#中文) · [English](#english) · [Releases](https://github.com/fuyifu2024/linux-factory-reset/releases) · [Report a bug / 反馈问题](https://github.com/fuyifu2024/linux-factory-reset/issues/new?template=bug.yml)

## 中文

公测请使用可丢弃的 UEFI 虚拟机，先在虚拟机软件中建立快照。恢复会删除目标系统上的全部旧用户、个人文件和额外安装的软件；恢复盘上的临时回退副本不是长期备份。

### 环境与安装

- AMD64 或 ARM64 UEFI、标准 GRUB，Secure Boot 已关闭。
- 一块系统盘：平铺 ext4 根分区和专用 FAT EFI 系统分区。不要使用默认 Btrfs、LVM、磁盘加密、独立 `/boot` 或共享 ESP 的安装布局。
- 另一块已挂载的 ext4 恢复盘；本工具不替你分区或格式化。先给它分配足够空间容纳来源、转换暂存区和整个旧系统的回退副本；工具还会检查实际余量。
- 从对应 Beta 发布页下载匹配发行版和架构的 DEB/RPM，以及 `SHA256SUMS`。在下载目录执行 `sha256sum --ignore-missing -c SHA256SUMS`，确认所下载文件为 `OK`。

```sh
sudo apt install ./linux-factory-reset_*.deb
# Fedora: sudo dnf install ./linux-factory-reset*.rpm
linux-factory-reset --version
linux-factory-reset gui
```

GUI 以普通用户启动，需要桌面的 polkit 授权代理。不要用 `sudo ... gui`。在 macOS 的 ARM64 虚拟机上应安装 ARM64 Linux 并选择 ARM64 包；AMD64 包不能替代。

### 建议测试顺序

1. 在未准备镜像时打开“查看变更”，确认有可理解的提示。
2. 选择“准备恢复资料”中的官方自动来源，选择恢复盘；观察下载进度、速度和校验结果。也可用可信的干净 IMG/QCOW2 测试本地导入。
3. 查看变更，核对根分区、ESP 和恢复盘。准备与预览不应删除任何文件。
4. 在“恢复系统”中输入错误确认文字，验证操作没有安排；再测试正确确认。GUI 正确确认后会立即安排恢复并重启，务必提前保存虚拟机快照。
5. 如需专门测试重启前取消，可先运行下面的命令（没有 `--reboot`），再在 GUI 点“取消恢复”，然后正常重启，检查原用户和文件仍在。

```sh
sudo linux-factory-reset arm --confirm ERASE-ALL-USERS
# 然后在 GUI 取消，或运行：
sudo linux-factory-reset cancel
sudo linux-factory-reset cancel
```

6. 实际恢复后，在本地控制台新建管理员，验证可以登录并执行 `sudo id -u`，结果应为 `0`。确认旧用户和测试文件已删除。自动来源是无桌面的基础/服务器系统，恢复后进入控制台属于预期行为。
7. 如测试第二次恢复，重新安装本工具，再完成准备和恢复。物理硬件兼容性不由虚拟机测试代替。

### 反馈

使用上方问题模板，提供版本、发行版、架构、虚拟机型号、根分区/ESP/恢复盘布局、Secure Boot 状态、操作步骤和错误原文。可附 `linux-factory-reset status` 和 `sudo linux-factory-reset plan` 的输出；如包含个人路径或设备序列号，请先遮盖。不要上传整个恢复盘、回退副本、密码、密钥或个人文件。

安装 ISO 目前仅支持下载，不能直接作为自动恢复来源。Mint、Btrfs 目标和非 UEFI 板卡引导不在本轮公测范围内。恢复协议及限制见 [recovery.md](recovery.md)。

## English

Use a disposable UEFI VM and take a hypervisor snapshot first. Reset deletes every old user, personal file and additional application on the target system. The temporary rollback copy is not a permanent backup.

### Environment and installation

- AMD64 or ARM64 UEFI, standard GRUB, Secure Boot already disabled.
- A system disk with a plain ext4 root and a dedicated FAT ESP. Default Btrfs, LVM, encryption, separate `/boot` and shared ESP layouts are outside this test scope.
- Another mounted ext4 recovery disk, large enough for the source, staging and a copy of the old system. The application checks available space but does not partition or format disks.
- Download the matching Beta DEB/RPM and `SHA256SUMS`. Run `sha256sum --ignore-missing -c SHA256SUMS` in the download directory and check that your downloaded file reports `OK`.

Install with the commands above. Start the GUI as a regular user with a working desktop polkit agent. On an Apple Silicon Mac, use an ARM64 Linux guest and ARM64 package.

### Test sequence

1. Open the change preview before preparation and check the explanation.
2. Prepare an official automatic source, select the recovery disk, and observe progress, speed and checksum verification. Alternatively import a trusted pristine installed-system IMG/QCOW2.
3. Preview the actual root, ESP and recovery disk. Preparation and preview must preserve personal data.
4. Test incorrect confirmation text, then correct confirmation. Correct GUI confirmation schedules reset and reboots immediately; take the snapshot beforehand.
5. To test cancellation before reboot, arm using the CLI command above without `--reboot`, then cancel in the GUI. Repeated CLI cancellation should also succeed. Reboot normally and check that the old account and files remain.
6. After actual restoration, create a new administrator at the local console, log in and run `sudo id -u`; expect `0`. Check that the old account and test files are gone. Generic automatic sources deliberately restore a base/server system without a desktop.
7. For a second reset, install the tool again and repeat preparation and restoration. VM results do not establish physical-hardware compatibility.

### Feedback

Use the linked issue template with version, distribution, architecture, VM model, disk layout, Secure Boot state, reproduction steps and the exact error. Include `linux-factory-reset status` and `sudo linux-factory-reset plan` output where useful; redact personal paths and device serials. Never upload the recovery disk, rollback copy, passwords, keys or personal files.

Installer ISOs can be downloaded but cannot yet be deployed automatically. Mint, Btrfs targets and native non-UEFI board boot are outside this Beta scope. See [recovery.md](recovery.md) for the protocol and limitations.
