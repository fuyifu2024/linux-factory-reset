# x86/OEM 恢复资料

首版没有通用 x86 出厂状态来源。安装 ISO 的 squashfs 是 live 环境：其包、引导、账户、安装器状态与已安装系统不同，直接覆盖不能保证可启动。本项目主动拒绝常见的 live 树。

厂商/管理员需准备一个匹配目标机型的干净 Ubuntu 安装，使用普通 ext4 根分区和 FAT ESP，标准 Ubuntu GRUB 布局。制作前移除个人用户/数据，仅保留系统和必要 OEM 设置；原始系统的 `fstab` 仅允许根和 ESP 两项，根使用 UUID。不要把正在使用的机器作为参考。

在另一套 Ubuntu/救援系统安装本工具，将参考根分区和 ESP **只读挂载**，然后：

```sh
sudo ubuntu-reset make-profile \
  --root /mnt/pristine-root \
  --boot /mnt/pristine-esp \
  --platform x86_64-uefi \
  --output /mnt/recovery/factory.squashfs
```

工具会拒绝普通个人账户、个人主目录、已初始化的 cloud-init 状态及复杂 fstab。生成资料时清除 machine-id、SSH 主机密钥和随机种子，将 fstab/标准 GRUB 指针的 UUID 改为可定位占位符，并加入本地 tty1 首次创建管理员的服务。这个服务不是 Ubuntu 官方 OEM 安装器；需在目标机型上验证它与桌面登录管理器配合正常。

文件生成后**先在可丢弃的同型号机器/虚拟机上做完整恢复测试**，包括 UEFI 启动、首次创建管理员、sudo、重启及驱动，再通过 HTTPS 发布 `.squashfs`。从独立可信渠道分发打印的 SHA-256；不要只从同一个不可信下载页面取得校验值。

目标机器执行：

```sh
sudo ubuntu-reset prepare-profile \
  --url https://your-trusted-server.example/factory.squashfs \
  --sha256 YOUR_INDEPENDENTLY_TRUSTED_64_CHARACTER_SHA256 \
  --storage /mnt/recovery
```

恢复资料包含整个可信系统，等同于授予其系统执行权限；哈希校验只能证明与信任的文件一致，不能替代发布者审查。不要从不可信来源导入资料。本仓库不提供已通过真机验收的 x86/OEM 恢复资料。

官方 Pi 模式无需管理员预制资料，但同样尚未经过真机恢复验证。它下载完整官方原始镜像，导入流程拒绝个人用户并保留镜像自带的首次开机服务。
