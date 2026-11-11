# Stable-release readiness / 正式版验收

The first stable release must state its supported distributions, architectures, boot layout and tested hardware/VMs precisely. Unsupported combinations stay excluded or experimental; a successful build is not a restoration pass. / 首个正式版须明确发行版、架构、引导布局及实际验收设备；不支持的组合继续排除或标为实验，不以构建成功代替恢复通过。

| Gate / 门槛 | Current evidence / 当前证据 |
| --- | --- |
| Native packages, ordinary-user CLI/GUI, upgrade ordering and exact-artifact compatibility / 原生安装包、普通用户界面、版本升级顺序与产物兼容 | Beta.2 passed; rerun for each changed release candidate / Beta.2 已通过，候选版本重新检查 |
| New GUI compatibility gate must accept a supported guest and refuse incompatible layouts before downloading / 新界面检查接受有效客体，并在下载前拒绝不兼容布局 | RC.1 acceptance pending / 待 RC.1 验收 |
| Actual restore/reboot, old-user/file removal, first administrator, locked root, password-authenticated sudo / 实际恢复重启、旧资料删除、开户、root 锁定和 sudo | Beta.1 Ubuntu 26.04 AMD64/ARM64 passed; bind any fresh run to its actual candidate source / 已通过，新增运行绑定实际源码 |
| Repeated resets, real cancellation, corrupt-profile refusal and interrupted-write rollback / 连续恢复、实际取消、损坏镜像拒绝和断电回退 | Beta.1 AMD64 passed; repeat if the owning recovery code changes / 已通过，修改相关恢复代码时重新检查 |
| A physical-device report for each hardware path claimed stable / 对宣称稳定的真机路径提供真机报告 | Missing; no destructive test on the user's Pi / 暂缺，不在用户树莓派执行破坏性测试 |
| Installation, limits, backup/data-deletion instructions, reproducible build/source and checksums / 安装、限制、备份与删除说明、构建源码及校验清单 | Bilingual documentation and artifacts exist; audit before stable publication / 已有中英双语文档与产物，正式发布前复核 |

Fedora ARM64 full guest restoration and native non-UEFI board validation are not complete. They must not be silently included in stable claims. ISO deployment, Mint and unsupported target layouts are future features, not prerequisites to shipping a narrower honest release. / Fedora ARM64 完整恢复与原生非 UEFI 板卡仍待验收，不能自动包含在稳定性承诺中；ISO、Mint 等新功能不作为扩大首个正式版范围的前提。

Website, commercial Rescue media and payment integration are outside this stabilization effort. / 网站、商业 Rescue 启动盘和支付不属于本轮稳定化工作。
