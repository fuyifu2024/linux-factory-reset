#include "reset.hpp"
#include <iostream>
#include <unistd.h>

namespace uf {
static std::string dialog(Args args) {
    args.insert(args.begin(), "zenity"); args.push_back("--title=Ubuntu 恢复出厂设置");
    auto result = command(args, true, -1, true);
    if (result.code == 1 || result.code == 5) return "";
    need(result.code == 0, "Cannot open desktop dialog (zenity)");
    while (!result.text.empty() && result.text.back() == '\n') result.text.pop_back();
    return result.text;
}
static bool privileged(Args args) {
    args.insert(args.begin(), "/usr/bin/ubuntu-reset"); args.insert(args.begin(), "/usr/bin/pkexec");
    return command(args, false, -1, true).code == 0;
}
int gui(bool preview) {
    if (preview) {
        std::cout << json{{"title", "Ubuntu 恢复出厂设置"}, {"language", "C++20"},
                         {"actions", {"准备官方树莓派恢复资料", "下载 x86/OEM 恢复资料", "查看恢复清单", "恢复出厂设置", "取消待执行恢复", "BIOS 支持情况"}},
                         {"destructive_confirmation", "清空全部用户"}}.dump(2) << '\n'; return 0;
    }
    need(geteuid() != 0, "Open the GUI as your ordinary desktop user; actions authenticate separately");
    for (;;) {
        auto choice = dialog({"--list", "--width=700", "--height=420", "--text=云下载 · 校验原始系统 · 离线恢复\n恢复将删除全部原用户、文件和配置。首版为实验版。", "--column=操作",
                              "准备官方树莓派恢复资料", "下载 x86/OEM 恢复资料", "查看恢复清单", "恢复出厂设置", "取消待执行恢复", "BIOS 支持情况"});
        if (choice.empty()) return 0;
        try {
            bool ok = true;
            if (choice == "准备官方树莓派恢复资料" || choice == "下载 x86/OEM 恢复资料") {
                auto store = dialog({"--file-selection", "--directory"});
                if (store.empty()) continue;
                if (choice == "准备官方树莓派恢复资料") {
                    auto release = dialog({"--entry", "--text=Ubuntu 官方树莓派桌面镜像版本", "--entry-text=26.04.1"});
                    if (release.empty()) continue;
                    ok = privileged({"prepare-pi", "--release", release, "--storage", store});
                } else {
                    auto url = dialog({"--entry", "--width=650", "--text=可信厂商/管理员发布的 factory.squashfs HTTPS 地址\n通用 Ubuntu 安装 ISO 不能直接当作已安装系统。"});
                    if (url.empty()) continue;
                    auto sum = dialog({"--entry", "--width=650", "--text=从独立可信渠道取得的 SHA-256（64 位小写十六进制）"});
                    if (sum.empty()) continue;
                    ok = privileged({"prepare-profile", "--url", url, "--sha256", sum, "--storage", store});
                }
            } else if (choice == "查看恢复清单") ok = privileged({"plan"});
            else if (choice == "取消待执行恢复") ok = privileged({"cancel"});
            else if (choice == "BIOS 支持情况") {
                auto info = firmware_status();
                dialog({"--info", "--width=600", "--text=" + std::string(info["supported"].get<bool>() ? "检测到 BIOS 出厂默认值接口。\n这是独立操作，CLI 命令为：\nsudo ubuntu-reset reset-firmware --confirm RESET-BIOS-DEFAULTS\n需重启后到固件界面核实。" : "没有检测到明确可用的 BIOS 出厂重置接口。\nUbuntu 恢复不依赖 BIOS 重置；请在主板固件界面处理。")});
                continue;
            } else if (choice == "恢复出厂设置") {
                load_state();
                auto answer = dialog({"--entry", "--width=680", "--text=将重启并永久删除全部原用户、个人文件、软件和配置。\n恢复分区必须保持连接；请接通稳定电源。\n本功能不等同于磁盘安全擦除。\n\n确认后输入：清空全部用户"});
                if (answer != "清空全部用户") continue;
                ok = privileged({"arm", "--confirm", "ERASE-ALL-USERS", "--reboot"});
            }
            dialog({ok ? "--info" : "--error", "--width=550", ok ? "--text=操作完成，请查看终端中的详细结果。" : "--text=操作未完成。请查看终端错误；不要手动绕过检查。"});
        } catch (const std::exception& e) { dialog({"--error", "--width=600", "--text=" + std::string(e.what())}); }
    }
}
}
