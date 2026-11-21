# Stable-release readiness / 正式版验收

The first stable release must state its supported distributions, architectures, boot layout and tested hardware/VMs precisely. Unsupported combinations stay excluded or experimental; a successful build is not a restoration pass. / 首个正式版须明确发行版、架构、引导布局及实际验收设备；不支持的组合继续排除或标为实验，不以构建成功代替恢复通过。

| Gate / 门槛 | Current evidence / 当前证据 |
| --- | --- |
| Native packages, ordinary-user CLI/GUI, upgrade ordering and exact-artifact compatibility / 原生安装包、普通用户界面、版本升级顺序与产物兼容 | RC.3: eight native builds/installations, two exact-artifact Ubuntu 26.04 checks, five-language confirmations, 30 actual GTK direction/rendering checks and Debian upgrade ordering passed / RC.3 八组原生安装、两组产物兼容性、五语言确认、30 次 GTK 方向渲染与版本顺序通过 |
| Preparation checks and actual stage/elapsed display / 完整准备前检查与真实阶段耗时 | RC.2: 11 checks passed in the real UEFI guest; GUI screenshot, silent-period heartbeat, blocker aggregation and unknown-state refusal retained / RC.2 实际客体 11 项通过，保留真实截图、静默心跳、问题汇总和未知状态拒绝证据 |
| Actual restore/reboot, old-user/file removal, first administrator, locked root, password-authenticated sudo / 实际恢复重启、旧资料删除、开户、root 锁定和 sudo | The November readiness branch passed full Ubuntu 26.04 AMD64 GUI and ARM64 UEFI recovery with the expanded preflight. [Evidence](validation/recovery-readiness.md) / 11 月验收分支通过 Ubuntu 26.04 AMD64 GUI 与 ARM64 UEFI 完整恢复，[验收记录](validation/recovery-readiness.md) |
| Post-recovery desktop / 恢复后可选桌面 | Ubuntu 26.04 AMD64 installed the desktop and selected the graphical target after recovery. Graphical login and Debian/Fedora desktop installation still need checks. [Evidence](validation/recovery-readiness.md) / Ubuntu AMD64 已验证安装及图形启动目标。图形登录与 Debian/Fedora 桌面安装待验证。 |
| Repeated resets, real cancellation, corrupt-profile refusal and interrupted-write rollback / 连续恢复、实际取消、损坏镜像拒绝和断电回退 | Beta.1 AMD64 passed; repeat if the owning recovery code changes / 已通过，修改相关恢复代码时重新检查 |
| A physical-device report for each hardware path claimed stable / 对宣称稳定的真机路径提供真机报告 | Pending a dedicated physical UEFI test device. [Acceptance procedure](physical-acceptance.md) / 等待专用 UEFI 测试机，[真机验收流程](physical-acceptance.md) |
| Installation, limits, backup/data-deletion instructions, reproducible build/source and checksums / 安装、限制、备份与删除说明、构建源码及校验清单 | Bilingual documentation and artifacts exist; audit before stable publication / 已有中英双语文档与产物，正式发布前复核 |

Fedora ARM64 full guest restoration and native non-UEFI board validation are not complete. They must not be silently included in stable claims. ISO deployment, Mint and unsupported target layouts are future features, not prerequisites to shipping a narrower honest release. / Fedora ARM64 完整恢复与原生非 UEFI 板卡仍待验收，不能自动包含在稳定性承诺中；ISO、Mint 等新功能不作为扩大首个正式版范围的前提。

Website, commercial Rescue media and payment integration are outside this stabilization effort. / 网站、商业 Rescue 启动盘和支付不属于本轮稳定化工作。
