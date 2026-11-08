# 恢复与回退 / Recovery and rollback

## 中文

当前开发版在 C++20 主程序外保留少量离线 Shell。目标系统要求普通 ext4 根分区、单独 FAT ESP、标准 GRUB 和可写 UEFI 启动变量；原生 Ubuntu Pi 引导走原有适配器。LVM、加密、RAID、Btrfs 目标、独立 `/boot`、独立 `/home` 尚不支持。Fedora 的 Btrfs **来源镜像**会在私有暂存区转换为平铺 ext4 恢复资料，这不表示可以直接重置已安装的 Btrfs 系统。

通用 UEFI 恢复顺序：

1. 从官网 HTTPS 元数据选择本机发行版、版本和架构；下载、进度、速度、取消和校验沿用统一界面。也可选可信本地镜像。
2. 在另一块 ext4 恢复盘制作压缩、只读的初始系统资料。清除源中的机器身份、root 个人资料及 root 密码；拒绝含个人用户的来源。云端初始化被禁用，首次开机创建本地管理员。
3. 完整检查资料与目标身份，生成发行版对应的 initramfs。Debian/Ubuntu 使用 initramfs-tools；Fedora 使用 dracut。
4. 将当前内核、恢复 initramfs 复制到恢复盘，生成独立 GRUB EFI 救援程序。记录原 BootOrder，设置临时救援入口优先启动。这里操作的是启动项，不是恢复 BIOS 默认值；不会关闭 Secure Boot。当前独立入口要求 Secure Boot 已关闭，否则拒绝授权恢复。
5. 在 initramfs 中离线核对三个不同 UUID、设备类型、资料哈希和空间；在恢复盘保存临时 root/ESP 回退副本，完成并同步后才进入 `WRITING` 阶段。
6. 通过 rsync 校验内容并恢复 root、ESP，定位 UUID。独立救援 EFI 目录不参与覆盖或删除。写入错误触发回退；`WRITING` 阶段断电后，独立入口使用保留的内核/initramfs，再次启动时回退。
7. 两部分文件及引导指针同步完成后，持久化 `COMMITTED` 决定，删除临时个人数据副本，再清除激活标记、恢复原 BootOrder、删除临时启动项。此阶段中断会继续清理，不使用已部分删除的回退副本。
8. 重启进入初始用户设置。Debian/Ubuntu 新用户进入 sudo 组；Fedora 进入 wheel 组，保持 root 锁定，并通过正常 SELinux 自动重新标记处理新增文件。

重启前可运行 `sudo linux-factory-reset cancel`。取消不依赖恢复镜像仍然完好，但要求原恢复盘仍挂载、ESP 身份未改变；已经进入写入阶段时，应使用独立救援入口。救援菜单提供继续恢复与回退选项。回退成功后原账号和文件会回到原系统；正常重置成功后，不保留这份临时个人资料副本。

这些机制需要实际 VM/硬件证据验证，不能凭编译通过宣布可用。具体版本的实测范围见验收记录。它们不能保证故障硬盘、损坏的 ESP/分区表、丢失的恢复盘、所有固件行为或恢复后任意驱动故障均可回退。不是 A/B 系统，也不是磁盘安全擦除；只恢复文件后尚未证明系统可启动。原生 Pi 适配器暂未增加独立 UEFI 入口和此回退协议。

## English

Generic UEFI targets currently require a plain ext4 root, separate FAT ESP, standard GRUB and writable UEFI boot variables. LVM, encryption, RAID, Btrfs targets, separate `/boot` and separate `/home` remain unsupported. Fedora Btrfs **source images** are flattened in private staging; this does not enable resetting an installed Btrfs target.

The application verifies the official source and all target identities, builds the distro's recovery initramfs, and copies the kernel/initramfs onto a separate ext4 recovery disk. An independent GRUB EFI loader temporarily takes priority in BootOrder. Firmware defaults are not reset, and Secure Boot is never disabled automatically; the unsigned independent loader requires it already disabled.

Before modifying the offline target, recovery saves and syncs a temporary root/ESP rollback copy. A durable `WRITING` journal precedes changes. Write failure triggers rollback; interruption during that phase boots the independent copy and rolls back without relying on the partially replaced target's `/boot` or `/usr`. The rescue directory on the ESP is excluded from reconciliation.

After root, ESP and relocated boot pointers are synced, `COMMITTED` is recorded. Temporary personal backups are deleted before normal boot is re-enabled. Interrupted committed cleanup resumes cleanup rather than using a partially deleted backup. The original BootOrder is restored and the temporary entry removed. A successful reset removes all old accounts/data and creates a new sudo/wheel administrator with root locked. Fedora retains SELinux and requests normal relabeling.

Use `sudo linux-factory-reset cancel` before reboot. Cancellation does not need an undamaged source image, but needs the original mounted recovery disk and unchanged ESP identity. Once writing starts, use the independent rescue entry. Successful rollback intentionally restores the old accounts and files; successful reset deletes the temporary personal backup.

Consult the release's acceptance evidence. This is not A/B boot management, secure erasure, or a guarantee against damaged media/ESP/partition tables, missing recovery storage, arbitrary firmware behavior or every post-reset driver failure. The native Ubuntu Pi adapter retains its earlier flow and does not gain this UEFI rollback protocol.
