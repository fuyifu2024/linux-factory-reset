# November 2026 recovery acceptance / 恢复验收

The localization and desktop guidance changes were built from `45c668c51d0b39c7012619248360c90b49f5e003`. The follow-up commit `3d05d53f1d6a149c05f036cdda0d2663a51ef858` changes only the VM test scripts. / 多语言报错及桌面引导使用以上源码构建。后一提交只修改了虚拟机测试脚本。

| Check / 检查 | Evidence / 证据 |
| --- | --- |
| Ubuntu 24.04 and Debian 12/13 on AMD64 and ARM64. Build, install, CTest, real HTTPS certificate rejection with localized advice and original error, five-language GUI fixtures, and cancellation/authorization diagnostics. / 双架构构建、安装、真实 HTTPS 证书错误和授权错误测试 | [Passed / 通过](https://github.com/fuyifu2024/linux-factory-reset/actions/runs/44868113345) |
| Fedora 45 AMD64 and ARM64 RPM builds, installs and tests. / Fedora 双架构构建、安装和测试 | [Passed / 通过](https://github.com/fuyifu2024/linux-factory-reset/actions/runs/44868275853) |
| Ubuntu 26.04 AMD64 UEFI real GUI recovery. Official download, preparation, preview, authorization, cancel and restore, old-data removal and password-authenticated sudo. / 真实界面下载、准备、预览、授权、取消及恢复，验证删除旧资料与新管理员 sudo | [Passed / 通过](https://github.com/fuyifu2024/linux-factory-reset/actions/runs/44868122206) |
| Optional desktop after that recovery. The guest displayed the setup command, installed `ubuntu-desktop-minimal` and `gdm3`, and selected `graphical.target`. The test verified package and target state before shutdown. Desktop login and Debian/Fedora desktop package installation remain untested. / 恢复后显示可选安装命令，安装 Ubuntu 桌面及 GDM，关机前核对图形启动目标。桌面登录和 Debian/Fedora 安装仍待测试。 | [Passed / 通过](https://github.com/fuyifu2024/linux-factory-reset/actions/runs/44868122206) |
| Ubuntu 26.04 ARM64 UEFI. Official source conversion on the hosted ARM64 runner, real prepared-profile import, independent EFI rescue, offline restore, reboot, removal of old test data, new administrator and password-authenticated sudo. The powered-off disk checks confirmed temporary backup and rescue files were cleaned up. / 官方源原生转换、实际导入、独立救援、离线恢复重启、删除旧数据、新管理员及真实 sudo 通过。关机后核对临时回退副本及救援文件已清理。 | [Passed / 通过](https://github.com/fuyifu2024/linux-factory-reset/actions/runs/44868969432) |

The [first ARM64 run](https://github.com/fuyifu2024/linux-factory-reset/actions/runs/44867746896) booted into Ubuntu, but its standard AAVMF image exposed neither SecureBoot nor SetupMode. Preparation stopped at preflight. The retry selects Ubuntu's Secure Boot capable AAVMF image with a private, unenrolled variable store. The guest recorded SecureBoot=0 and SetupMode=1. The application preflight rules were unchanged. [Ubuntu firmware package contents](https://packages.ubuntu.com/noble/all/qemu-efi-aarch64/filelist)

首轮 ARM64 虚拟机进入 Ubuntu 后发现固件缺少 SecureBoot 和 SetupMode，预检拒绝继续。重测改用支持 Secure Boot 的 AAVMF 固件和独立空白变量盘，客体确认 Secure Boot 已关闭，产品预检规则保持原样。

Retained [ARM64 preflight](november-2026/arm64-layout-check.json), [firmware bytes](november-2026/arm64-firmware-state.txt), [guest result](november-2026/arm64-guest-result.txt), [account checks](november-2026/arm64-guest-diagnostics.txt), [host verification](november-2026/arm64-host-verification.txt), and [AMD64 desktop result](november-2026/amd64-desktop-result.txt). ARM64 guest conversion, repeated restores and physical-hardware paths are outside this run. / 保留 ARM64 预检、固件、开户及关机验证和 AMD64 桌面结果。此次 ARM64 验收覆盖已转换资料导入，不含客体内转换和多次恢复。

Physical UEFI recovery and rollback are pending a dedicated device. The Hindi, Spanish and Arabic warnings were checked for deletion scope and the exact confirmation token. Native-speaker proofreading is pending. / 真机恢复与回退待专用设备。印地语、西语和阿语的删除范围及确认词已核对，母语措辞校对待完成。
