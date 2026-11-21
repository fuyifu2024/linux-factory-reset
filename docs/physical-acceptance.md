# Physical UEFI acceptance / UEFI 真机验收

Use a dedicated, expendable test computer with a verified external backup and independent boot media. The first target is Ubuntu 26.04 AMD64 UEFI with Secure Boot already disabled, plain ext4 root, a dedicated FAT ESP and an independent ext4 recovery disk. Disconnect unrelated drives. Recheck the target by model and capacity before authorizing any write.

请用专门的可清空测试机，并备好已验证的外部备份和独立启动介质。首个目标为 Ubuntu 26.04 AMD64 UEFI，Secure Boot 已关闭，普通 ext4 根分区、独立 FAT ESP，以及另一块 ext4 恢复盘。拔掉无关磁盘，任何写入前都核对目标盘的型号和容量。

1. Record the exact package version and checksum, machine and firmware model/version, disk layout, and where the recovery disk is attached. Run `linux-factory-reset diagnose --output report.json` and `sudo linux-factory-reset check --storage /mnt/recovery` before preparation.
2. Prepare the official source and inspect the change preview. Use a test account and non-sensitive marker files for verification. Record the original boot order. On the test machine, schedule without rebooting, cancel, and verify the boot order and test files after an ordinary reboot.
3. After verifying the backup and target again, run one complete recovery on that same test machine. Record rescue boot, completion, new-account sign-in and the result of `sudo id -u` (expected `0`). Verify that the old account and marker files are gone and the recovery disk has no temporary rollback copy left.
4. Record rollback as a separate result. Only use a dedicated test device and a controlled fault setup, with an external image available for recovery. Record that the old account/files and boot order are restored. Leave the check pending if it was not exercised.

1. 记录安装包版本和校验值、设备和固件型号版本、磁盘布局及恢复盘接法。准备前执行上面的只读诊断与预检。
2. 准备官方来源并查看变更。使用测试账号和非敏感标记文件，记录原启动顺序。先测试安排后取消，在正常重启后核对启动顺序和测试文件。
3. 再次核对备份和目标盘，在同一备用机上完成一次恢复。记录救援启动、恢复完成、开户登录及 sudo，确认旧账号和测试文件已删除、恢复盘不再保留临时回退副本。
4. 回退单独登记。只有专用测试机且备好外部镜像时才做受控故障测试，记录旧账号、文件和启动顺序是否恢复。未做就标记待验证。

Report each item as passed, failed or untested, with the exact source/tag and an error excerpt or redacted diagnostics. Omit credentials, serial numbers, personal paths, disk images and recovery copies. / 报告请逐项填写通过、失败或未测，附准确版本和脱敏错误或诊断。勿上传账号凭据、序列号、个人路径、磁盘镜像或恢复副本。[Report / 反馈](https://github.com/fuyifu2024/linux-factory-reset/issues/new?template=bug.yml)
