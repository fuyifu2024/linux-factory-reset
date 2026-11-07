# Debian / Ubuntu UEFI 恢复资料 · UEFI recovery profiles

## 中文

通用 ARM64 和 x86_64 都使用可信的**已安装系统**恢复资料，支持 Debian 与 Ubuntu。它不是直接导入安装 ISO/live 系统的功能，也不会自动凭包清单猜测历史配置。发布者需要提供适合目标机器的内核、驱动、设备树/固件及启动文件；同样是 ARM64 不代表恢复资料可以在任意板卡之间互换。

准备一份匹配目标机型的干净安装：普通 ext4 根分区、FAT ESP 挂载于 `/boot/efi`，标准 `/boot/grub/grub.cfg`。ESP 使用 `EFI/debian/grub.cfg` 或 `EFI/ubuntu/grub.cfg`，ARM64 有 `grubaa64.efi`，x86_64 有 `grubx64.efi`；使用 shim/Secure Boot 时，发布者必须保留匹配的可信签名链。

制作前移除个人账户和主目录，保留系统账户。`fstab` 仅允许根和 ESP，根使用 UUID；两个 GRUB 文本配置都需引用该 UUID。必须安装 `sudo`、`passwd` 及账户工具，并保留 `sudo` 管理员组。源不能已经完成 cloud-init/本项目首次设置。程序也会清理 root 个人目录、machine-id、随机种子和 SSH 主机密钥。

在另一套系统/救援环境**安装本工具**，将参考根和 ESP 只读挂载，再制作：

```sh
# 通用 ARM64：Debian 或 Ubuntu
sudo ubuntu-reset make-profile \
  --root /mnt/pristine-root --boot /mnt/pristine-esp \
  --platform arm64-uefi --output /mnt/recovery/factory.squashfs

# x86_64：改用 --platform x86_64-uefi
```

生成资料将保留发行版、版本和 CPU/引导平台元数据，将 fstab 与对应发行版的 GRUB 指针变成可定位占位符，并为两种 UEFI CPU 平台加入本地 tty1 创建管理员服务。首次设置需要键盘/本地虚拟控制台；不提供无头远程自动开户。与桌面登录管理器、无头设备及物理主机的配合需要真实验证。

先在可丢弃的同型号机器或匹配 VM 上验证完整恢复、首次开户、sudo、驱动和重启，再通过 HTTPS 发布 `.squashfs`。独立分发打印的 SHA-256。目标执行：

```sh
sudo ubuntu-reset prepare-profile \
  --url https://your-trusted-server.example/factory.squashfs \
  --sha256 YOUR_INDEPENDENTLY_TRUSTED_64_CHARACTER_SHA256 \
  --storage /mnt/recovery
```

程序按目标发行版和 CPU/引导平台检查资料，拒绝将 Debian 源当作 Ubuntu 或将 x86_64 源当作 ARM64。旧 schema 1 的 Ubuntu 资料/状态仍可读取；通用 ARM64 和 Debian 使用 schema 2。资料等同于完整系统执行权限，哈希只证明与所信任的文件一致，不能代替发布者审查。本仓库未提供真机验收的通用恢复资料。

原生树莓派适配器仍只处理 Ubuntu Pi 5 的 `current/` 固件布局。使用 UEFI 启动的树莓派走上述通用 ARM64 路径；非 UEFI 的其他 U-Boot/固件布局需要新适配器。

## English

Generic ARM64 and x86_64 use trusted **installed-system profiles** for Debian or Ubuntu. This is not arbitrary installer/live-ISO import or reconstruction of historical settings from package names. A profile must include the kernel, drivers, firmware/device-tree and boot files appropriate to the target device; sharing the ARM64 CPU architecture does not make profiles interchangeable across boards.

Prepare a pristine installed reference: plain ext4 root, FAT ESP at `/boot/efi`, standard `/boot/grub/grub.cfg`, and `EFI/debian/grub.cfg` or `EFI/ubuntu/grub.cfg`. ARM64 requires `grubaa64.efi`, x86_64 requires `grubx64.efi`. Preserve a valid matching shim/signature chain when Secure Boot is used.

Remove personal users/home directories. The reference fstab may contain only root and ESP, with root identified by UUID; both GRUB text configs must reference that UUID. Install sudo/passwd/account tools and keep the sudo administrator group. Completed cloud-init or this tool's initial-user setup is rejected. Profile creation also clears root personal data, machine identity, random seed and SSH host keys.

Install the tool in another system/rescue environment, mount the reference root and ESP **read-only**, then use the `make-profile` command above with `arm64-uefi` or `x86_64-uefi`. Metadata records distribution/version/platform. The builder inserts relocatable filesystem identifiers and the same local tty1 administrator-creation service for both UEFI architectures. A local keyboard/virtual console is required; unattended remote account setup is not provided.

Test full recovery, initial login, sudo, drivers and reboot on disposable matching hardware/VMs before hosting the profile over HTTPS. Distribute its SHA-256 independently. The target's `prepare-profile` command rejects distribution/platform mismatches. Legacy Ubuntu schema 1 remains readable; Debian/generic ARM64 use schema 2. Trusting a profile means trusting its entire operating system, not merely its checksum. No hardware-validated generic factory profile is supplied here.

The native Pi adapter remains specific to Ubuntu's Pi 5 `current/` firmware recipe. A Pi using UEFI follows the generic ARM64 path. Other native U-Boot/firmware layouts need dedicated adapters.
