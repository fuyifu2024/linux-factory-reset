# 首版验证记录

2026-11-07，本地 ARM64 / Ubuntu 26.04：

- C++20 编译通过，编译器检查使用 `-Wall -Wextra -Wpedantic -Wconversion -Wshadow`。
- 36 个 C++ 检查通过：哈希、校验清单、配置验证、注入拦截、平台/镜像参照处理等。
- 5 个 CTest 组通过，包括实际生成/解开 Squashfs 和使用 rsync 的临时目录恢复集成测试。
- 集成测试验证了账户/个人文件/额外程序删除、默认配置及包数据库恢复、相同文件不重写、目录链接不写穿以及恢复失败不继续替换启动文件。
- ShellCheck 0.11.0 检查通过。ARM64 DEB 构建、目录权限和依赖元数据检查通过。
- 用已有的本地代理下载官方 `SHA256SUMS` 与签名，并由系统 Ubuntu keyring 验证：`Ubuntu CD Image Automatic Signing Key (2012)`，指纹 `843938DF228D22F7B3742BC0D94AA3F0EFE21092`。
- 官方校验清单包含 `ubuntu-26.04.1-preinstalled-desktop-arm64+raspi.img.xz`；当日 SHA-256 为 `8216f54cc1df2fcf7de37530a7edccdd200abd7dcfbc5d4f0407df7513544d6a`。程序每次获取并验签清单，不只信任文档中写死的值。

**没有执行的验证：**完整官方大镜像下载/导入、真实 initramfs 启动恢复、真实 x86/OEM 恢复资料、首次开机向导、物理 BIOS 重置、断电/磁盘故障恢复及耗时基准。GUI 只验证了配置输出，尚未进行人工交互验收。

GitHub CI 分别在 Ubuntu 24.04 x86_64 和 ARM64 上构建、运行相同安全测试并产生二进制及 DEB。CI 的结果不代表真实硬件恢复已通过验收；请以 [Actions](https://github.com/fuyifu2024/ubuntu-reset/actions) 中对应提交的结果为准。

整个开发过程中没有在当前工作机器上安装本工具、设置待恢复标记、修改启动文件、执行系统恢复或 BIOS 重置。
