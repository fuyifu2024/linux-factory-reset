#include "reset.hpp"
#include "download.hpp"
#include <fcntl.h>
#include <iostream>
#include <cstdlib>
#include <csignal>
#include <sys/wait.h>
#include <cerrno>
#include <unistd.h>

namespace uf {
static std::string dialog(Args args) {
    args.insert(args.begin(), "zenity"); args.push_back("--title=Linux 恢复出厂设置");
    auto r = command(args, true, -1, true);
    if (r.code == 1 || r.code == 5) return "";
    need(r.code == 0, "无法打开桌面对话框，请在终端使用 linux-factory-reset --help");
    while (!r.text.empty() && r.text.back() == '\n') r.text.pop_back();
    return r.text;
}
static void show_text(const std::string& text) {
    TempDir tmp("/tmp"); auto path = tmp.path / "result.txt"; write(path, text);
    dialog({"--text-info", "--width=760", "--height=420", "--filename=" + path.string()});
}
struct BusyDialog {
    pid_t pid = -1;
    BusyDialog() {
        pid = fork();
        if (pid == 0) {
            execl("/usr/bin/zenity", "zenity", "--progress", "--pulsate", "--no-cancel",
                  "--title=正在处理恢复资料", "--width=540",
                  "--text=正在执行，请稍候。\n准备镜像或比较文件可能需要几分钟；详细进度会显示在启动终端中。", static_cast<char*>(nullptr));
            _exit(127);
        }
    }
    ~BusyDialog() {
        if (pid > 0) { kill(pid, SIGTERM); int status = 0; while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {} }
    }
};
static bool privileged(Args args, bool preview = false) {
    args.insert(args.begin(), "/usr/bin/linux-factory-reset");
    args.insert(args.begin(), "--disable-internal-agent");
    args.insert(args.begin(), "/usr/bin/pkexec");
    // Keep errors with the result, while also streaming progress to the terminal.
    // No invisible terminal password prompt: a desktop authorization agent is required.
    Result result;
    { BusyDialog busy; result = command(args, true, -1, true, true, true); }
    if (result.code != 0) {
        show_text("操作未完成，尚未获得成功结果。\n以下是实际错误，可直接复制反馈：\n\n" + result.text +
                  "\n若错误与权限验证有关，请确认桌面有权限验证代理，或改用终端 sudo linux-factory-reset。\n");
        return false;
    }
    if (preview) show_text("这是只读预览，没有恢复系统。\n*deleting 表示将删除；其他行表示将新增/修改；相同文件不会列出。\n\n" + result.text);
    else show_text("操作已完成。\n\n" + result.text);
    return true;
}

struct DownloadDialog {
    pid_t pid = -1; int input = -1;
    std::chrono::steady_clock::time_point last{};
    DownloadDialog() {
        int channel[2]; need(pipe2(channel, O_CLOEXEC) == 0, "Cannot open progress channel");
        pid = fork();
        if (pid == 0) {
            dup2(channel[0], STDIN_FILENO); close(channel[0]); close(channel[1]);
            execl("/usr/bin/zenity", "zenity", "--progress", "--auto-close", "--percentage=0", "--width=660",
                "--title=下载官方系统镜像", "--text=正在连接官网，点击取消可停止下载。", static_cast<char*>(nullptr)); _exit(127);
        }
        close(channel[0]); if (pid < 0) { close(channel[1]); throw Error("Cannot start progress display"); }
        input = channel[1];
    }
    bool update(const TransferProgress& progress) {
        int status = 0; auto exited = waitpid(pid, &status, WNOHANG);
        if (exited == pid) { pid = -1; return false; }
        auto now = std::chrono::steady_clock::now();
        if (now - last < std::chrono::milliseconds(120)) return true;
        last = now;
        auto message = std::to_string(progress.percent()) + "\n# " + progress.text() +
            (progress.total && progress.downloaded >= progress.total ? "\n# 下载完成，正在校验镜像，请稍候。\n" : "\n");
        return ::write(input, message.data(), message.size()) == static_cast<ssize_t>(message.size());
    }
    void complete() { const char value[] = "100\n# 校验通过，镜像已保存\n"; (void)::write(input, value, sizeof(value)-1); }
    ~DownloadDialog() {
        if (input >= 0) close(input);
        if (pid > 0) { kill(pid, SIGTERM); int status = 0; while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {} }
    }
};

static void automatic_preparation(const json& sources, const std::string& storage) {
    show_text(sources.at("reason").get<std::string>() + "\n\n请选择一个用于暂存下载镜像的目录。下载显示进度和速度；导入完成后会清理临时下载。下一步仍只是准备资料，不会恢复系统。");
    auto directory = dialog({"--file-selection", "--directory"}); if (directory.empty()) return;
    TempDir temporary(directory); OfficialImage image;
    { BusyDialog busy; image = resolve_recovery(sources.at("distro"), sources.at("version"), sources.at("platform")); }
    fs::path path;
    { DownloadDialog progress; path = download_official(image, temporary.path, [&](const TransferProgress& p) { return progress.update(p); }); progress.complete(); }
    privileged({"prepare-image", "--file", path.string(), "--storage", storage,
        image.checksum_algorithm == "sha512" ? "--sha512" : "--sha256", image.checksum});
}

static void official_download() {
    auto distro = dialog({"--list", "--width=720", "--height=350", "--column=ID", "--column=官网来源", "--hide-column=1", "--print-column=1",
        "--text=自动从官网取得下载地址及 SHA-256，无需手填网址。\n这里下载的是安装 ISO；ISO 自动恢复尚未实现。自动恢复请在“准备恢复资料”中选择匹配的官方基础系统源。",
        "ubuntu", "Ubuntu LTS 桌面 / 服务器", "debian", "Debian 稳定版网络安装镜像", "fedora", "Fedora 稳定版 Workstation / Server"});
    if (distro.empty()) return;
    auto arch = dialog({"--list", "--column=ID", "--column=架构", "--hide-column=1", "--print-column=1",
        "--text=本机架构：" + host_arch() + "。ARM64 包括普通 UEFI 虚拟机、服务器和兼容电脑。请选择目标设备的架构。",
        "arm64", "ARM64 / AArch64", "amd64", "x86_64 / AMD64"}); if (arch.empty()) return;
    std::string release = "stable", edition = "netinst";
    if (distro == "ubuntu") {
        release = dialog({"--list", "--column=版本", "24.04", "26.04"}); if (release.empty()) return;
        edition = dialog({"--list", "--column=ID", "--column=版本类型", "--hide-column=1", "--print-column=1", "desktop", "桌面版 Desktop", "server", "服务器版 Server"});
    } else if (distro == "fedora") edition = dialog({"--list", "--column=ID", "--column=版本类型", "--hide-column=1", "--print-column=1", "workstation", "Workstation 桌面版", "server", "Server DVD"});
    if (edition.empty()) return;
    auto directory = dialog({"--file-selection", "--directory", "--text=选择镜像保存目录（不需要第二块恢复磁盘）"}); if (directory.empty()) return;
    OfficialImage image;
    { BusyDialog busy; image = resolve_image(distro, release, edition, arch); }
    fs::path result;
    { DownloadDialog progress; result = download_official(image, directory, [&](const TransferProgress& p) { return progress.update(p); }); progress.complete(); }
    show_text("下载完成，SHA-256 与官网 HTTPS 校验清单一致。\n文件：" + result.string() +
        "\n\n这是安装 ISO，目前不会自动导入为恢复资料，也不会设置恢复标记。可用于手工安装。\n此下载器未检查独立 PGP 签名；原生 Ubuntu 自动恢复源另有签名验证。\n");
}

int gui(bool preview) {
    if (preview) {
        std::cout << json{{"title", "Linux 恢复出厂设置"}, {"language", "C++20"},
            {"actions", {"下载官方镜像", "准备恢复资料", "查看变更", "恢复系统", "取消待执行恢复", "高级功能", "导出诊断报告"}},
            {"preparation_methods", {"选择本地镜像", "自动下载（根据本机自动识别可用性）"}},
            {"requires_url_in_normal_flow", false}, {"iso_automatic_install", false},
            {"preview_requires_prepared_source", true}, {"destructive_confirmation", "清空全部用户"}}.dump(2) << '\n';
        return 0;
    }
    need(geteuid() != 0, "请以普通桌面用户打开界面，需要管理员权限时会单独验证");
    // Dialogs are 2D: avoid fragile VM EGL/Zink acceleration by default.
    // This affects this process/children only and preserves an explicit user override.
    // Zenity converts argv using the locale charset; minimal containers often default to ASCII.
    setenv("LC_ALL", "C.UTF-8", 1);
    signal(SIGPIPE, SIG_IGN); // A closed progress window cancels the transfer instead of terminating us.
    setenv("GSK_RENDERER", "cairo", 0);
    setenv("LIBGL_ALWAYS_SOFTWARE", "1", 0);
    for (;;) {
        auto status = preparation_status(); bool ready = status.at("ready");
        auto choice = dialog({"--list", "--width=700", "--height=420",
            "--text=当前为开发测试版；先准备恢复镜像，再查看变更，最后确认恢复。\n" + status.at("reason").get<std::string>(),
            "--column=ID", "--column=操作", "--column=状态", "--hide-column=1", "--print-column=1",
            "download", "下载官方镜像", "Ubuntu / Debian / Fedora；显示进度和速度",
            "prepare", "准备恢复资料", ready ? "可重新选择镜像" : "请先完成这一步",
            "plan", "查看变更", ready ? "只读预览" : "需先准备镜像",
            "restore", "恢复系统", ready ? "需明确确认并重启" : "需先准备镜像",
            "cancel", "取消待执行恢复", "重启前可取消",
            "advanced", "高级功能", "管理员恢复源、固件信息",
            "diagnose", "导出诊断报告", "无需管理员权限，仅保存到本机"});
        if (choice.empty()) return 0;
        try {
            if ((choice == "plan" || choice == "restore") && !ready) {
                show_text(status.at("reason")); continue;
            }
            if (choice == "download") official_download();
            else if (choice == "diagnose") {
                auto path = dialog({"--file-selection", "--save", "--filename=linux-factory-reset-diagnostics.json",
                    "--text=选择新的 JSON 文件名；已有文件不会覆盖。\n仅包含版本、系统、架构、文件系统类型、准备状态和工具是否可用。\n不包含用户名、个人路径、磁盘 UUID、镜像网址或日志，不会自动上传。"});
                if (path.empty()) continue;
                auto report = diagnostics(); export_diagnostics(path, report);
                show_text("诊断报告已保存到本机，没有上传。\n文件：" + path + "\n\n" + report.dump(2));
            }
            else if (choice == "prepare") {
                auto sources = detected_sources(); bool automatic = sources.at("automatic_available");
                Args options{"--list", "--width=720", "--height=320", "--column=ID", "--column=来源", "--hide-column=1", "--print-column=1",
                    "--text=选择恢复资料来源。普通流程不要求填写网址或校验字符串。\n" + sources.at("reason").get<std::string>(),
                    "local", "选择本地完整系统镜像（IMG / RAW / QCOW2）"};
                if (automatic) { options.push_back("automatic"); options.push_back("自动下载并校验官方恢复资料"); }
                auto source = dialog(options); if (source.empty()) continue;
                std::string image;
                if (source == "local") {
                    image = dialog({"--file-selection", "--file-filter=系统镜像 | *.img *.raw *.img.xz *.raw.xz *.qcow2 *.squashfs", "--file-filter=所有文件 | *"});
                    if (image.empty()) continue;
                    image_format(image); // Reject installer ISO with an explanation before elevation.
                }
                show_text("请选择独立 ext4 恢复磁盘中的任意文件夹，程序会自动识别该磁盘，并在盘内 ubuntu-reset 目录保存资料。\n不能选择当前系统盘里的普通文件夹。虚拟机尚无第二块磁盘时，需要先添加并格式化、挂载它。\n下一步只准备资料，不会恢复系统。镜像需包含干净的已安装系统。ISO 自动安装功能尚未实现。");
                auto store = dialog({"--file-selection", "--directory"}); if (store.empty()) continue;
                if (source == "local") privileged({"prepare-image", "--file", image, "--storage", store});
                else automatic_preparation(sources, store);
            } else if (choice == "plan") privileged({"plan"}, true);
            else if (choice == "cancel") privileged({"cancel"});
            else if (choice == "restore") {
                auto answer = dialog({"--entry", "--width=680", "--text=将重启并删除全部原用户、个人文件、额外软件和配置。\n恢复磁盘必须保持连接，请接通稳定电源。\n开发测试版尚未完成真机恢复验收。\n\n确认后输入：清空全部用户"});
                if (answer == "清空全部用户") privileged({"arm", "--confirm", "ERASE-ALL-USERS", "--reboot"});
            } else if (choice == "advanced") {
                auto action = dialog({"--list", "--column=ID", "--column=高级功能", "--hide-column=1", "--print-column=1",
                    "--text=以下面向管理员，不是普通用户准备恢复镜像的必填步骤。",
                    "custom", "导入管理员发布的恢复源（需地址及独立校验值）", "firmware", "查看固件重置支持情况"});
                if (action == "firmware") show_text(firmware_status().dump(2));
                else if (action == "custom") {
                    auto url = dialog({"--entry", "--width=650", "--text=管理员自行发布的本项目恢复资料 HTTPS 地址"}); if (url.empty()) continue;
                    auto sum = dialog({"--entry", "--width=650", "--text=从独立可信渠道取得的 SHA-256"}); if (sum.empty()) continue;
                    auto store = dialog({"--file-selection", "--directory"}); if (store.empty()) continue;
                    privileged({"prepare-profile", "--url", url, "--sha256", sum, "--storage", store});
                }
            }
        } catch (const std::exception& e) { show_text(e.what()); }
    }
}
}
