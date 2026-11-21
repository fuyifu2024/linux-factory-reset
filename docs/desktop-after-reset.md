# Optional desktop after recovery / 恢复后可选装桌面

The generic official recovery images start at a local text console. First create the new administrator and sign in. You can keep the base system or install a desktop using your distribution's package manager. The download needs a working internet connection and sufficient free disk space. The package manager shows the transaction for review.

通用官方恢复镜像先启动到本地文本控制台。创建新管理员并登录后，可以保留基础系统，或通过发行版包管理器安装桌面。安装需要联网和足够的磁盘空间，请先阅读包管理器显示的安装清单。

| Distribution / 发行版 | Commands from the new account / 从新账号执行 |
| --- | --- |
| Ubuntu 24.04/26.04 | `sudo apt update && sudo apt install ubuntu-desktop-minimal` ([Ubuntu 24.04](https://packages.ubuntu.com/noble/ubuntu-desktop-minimal), [Ubuntu 26.04](https://packages.ubuntu.com/resolute/ubuntu-desktop-minimal)) |
| Debian 13 | `sudo apt update && sudo apt install task-gnome-desktop` ([Debian GNOME task](https://packages.debian.org/trixie/task-gnome-desktop)) |
| Fedora 44/45 | `sudo dnf5 environment install workstation-product-environment` ([DNF5 environment command](https://dnf5.readthedocs.io/en/latest/commands/environment.8.html)) |

After a successful install, select graphical startup and reboot. / 安装成功后，设置图形启动并重启。

```sh
sudo systemctl set-default graphical.target
sudo reboot
```

The commands install a fresh desktop from the configured distribution repositories. Old accounts, applications and settings are never imported. If the installation fails, keep using the text console and address the package manager's error before switching startup targets. For a local recovery image that already includes a desktop, follow that image's setup instead.

这些命令从当前发行版的配置软件源安装新的桌面，不导入旧账号、应用或设置。安装失败时可以继续使用文本控制台，处理包管理器报错后再切换启动目标。若可信的本地恢复镜像自带桌面，按镜像原有方式使用即可。
