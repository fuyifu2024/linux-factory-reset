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
    args.insert(args.begin(), "zenity"); args.push_back("--title=" + tr("title"));
    args.push_back("--ok-label=" + tr("button.ok")); args.push_back("--cancel-label=" + tr("cancel"));
    auto r = command(args, true, -1, true);
    if (r.code == 1 || r.code == 5) return "";
    need(r.code == 0, tr("error.dialog"));
    while (!r.text.empty() && r.text.back() == '\n') r.text.pop_back();
    return r.text;
}
static void show_text(const std::string& text) {
    TempDir tmp("/tmp"); auto path = tmp.path / "result.txt"; write(path, text);
    dialog({"--text-info", "--width=760", "--height=420", "--filename=" + path.string()});
}
struct BusyDialog {
    pid_t pid = -1; int input = -1;
    std::string phase, pending; bool observed;
    std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now(), changed = began, refreshed{};
    explicit BusyDialog(const std::string& initial = tr("operation.lookup"), bool observed_operation = false) : phase(initial), observed(observed_operation) {
        int channel[2]; need(pipe2(channel, O_CLOEXEC) == 0, "Cannot open operation progress channel");
        if (fcntl(channel[1], F_SETFL, O_NONBLOCK) < 0) { close(channel[0]); close(channel[1]); throw Error("Cannot configure progress channel"); }
        pid = fork();
        if (pid == 0) {
            dup2(channel[0], STDIN_FILENO); close(channel[0]); close(channel[1]);
            auto title = "--title=" + tr("operation.title"), text = "--text=" + tr("operation.wait"), button = "--ok-label=" + tr("working");
            execl("/usr/bin/zenity", "zenity", "--progress", "--pulsate", "--no-cancel",
                  title.c_str(), "--width=720", text.c_str(), button.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }
        close(channel[0]); if (pid < 0) { close(channel[1]); throw Error("Cannot start operation progress display"); }
        input = channel[1]; update("");
    }
    void update(const std::string& chunk) {
        bool stage_changed = false;
        // Only a small, allowlisted stage code controls the progress label. Raw logs/paths never become markup.
        for (char c : chunk) {
            if (c == '\n') {
                if (pending.starts_with("LFR_STAGE ")) try {
                    auto value = json::parse(pending.substr(10)); auto label = stage_label(value.at("stage").get<std::string>());
                    if (!label.empty() && phase != label) { phase = label; changed = std::chrono::steady_clock::now(); stage_changed = true; }
                } catch (const std::exception&) {}
                pending.clear();
            } else if (pending.size() < 4096) pending += c;
        }
        auto now = std::chrono::steady_clock::now();
        if (!stage_changed && now - refreshed < std::chrono::seconds(1)) return;
        refreshed = now;
        auto total = std::chrono::duration_cast<std::chrono::seconds>(now - began).count();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - changed).count();
        auto text = "# " + tr("operation.stage", {{"stage", phase}});
        if (observed) text += " | " + tr("operation.elapsed", {{"stage_seconds", std::to_string(elapsed)}, {"total_seconds", std::to_string(total)}});
        text += '\n';
        // Nonblocking and <= PIPE_BUF: a slow/closed window cannot stall or cancel the backend.
        if (input >= 0) (void)::write(input, text.data(), text.size());
    }
    ~BusyDialog() {
        if (input >= 0) close(input);
        if (pid > 0) { kill(pid, SIGTERM); int status = 0; while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {} }
    }
};
static bool privileged(Args args, bool preview = false, bool show_success = true) {
    auto initial = args.at(0) == "plan" ? stage_label("verifying") : args.at(0) == "cancel" ? stage_label("cancelling") : stage_label("checking");
    args.push_back("--language"); args.push_back(language());
    args.insert(args.begin(), "/usr/bin/linux-factory-reset");
    args.insert(args.begin(), "--disable-internal-agent");
    args.insert(args.begin(), "/usr/bin/pkexec");
    // Keep errors with the result, while also streaming progress to the terminal.
    // No invisible terminal password prompt: a desktop authorization agent is required.
    Result result;
    { BusyDialog busy(initial, true); result = command(args, true, -1, true, true, true, 0, [&](const std::string& chunk) { busy.update(chunk); }); }
    if (result.code != 0) {
        show_text(tr("operation.failed") + "\n\n" + result.text + "\n" + tr("operation.auth_hint"));
        return false;
    }
    if (preview) show_text(tr("operation.preview") + "\n\n" + result.text);
    else if (show_success) show_text(tr("operation.done") + "\n\n" + result.text);
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
            auto title = "--title=" + tr("download.title"), text = "--text=" + tr("download.connecting"), cancel = "--cancel-label=" + tr("cancel");
            execl("/usr/bin/zenity", "zenity", "--progress", "--auto-close", "--percentage=0", "--width=660",
                title.c_str(), text.c_str(), cancel.c_str(), static_cast<char*>(nullptr)); _exit(127);
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
            (progress.total && progress.downloaded >= progress.total ? "\n# " + tr("download.verifying") + "\n" : "\n");
        return ::write(input, message.data(), message.size()) == static_cast<ssize_t>(message.size());
    }
    void complete() { auto value = "100\n# " + tr("download.saved") + "\n"; (void)::write(input, value.data(), value.size()); }
    ~DownloadDialog() {
        if (input >= 0) close(input);
        if (pid > 0) { kill(pid, SIGTERM); int status = 0; while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {} }
    }
};

static void automatic_preparation(const json& sources, const std::string& storage) {
    show_text(sources.at("reason").get<std::string>() + "\n\n" + tr("prepare.download_folder"));
    auto directory = dialog({"--file-selection", "--directory"}); if (directory.empty()) return;
    TempDir temporary(directory); OfficialImage image;
    { BusyDialog busy; image = resolve_recovery(sources.at("distro"), sources.at("version"), sources.at("platform")); }
    fs::path path;
    { DownloadDialog progress; path = download_official(image, temporary.path, [&](const TransferProgress& p) { return progress.update(p); }); progress.complete(); }
    privileged({"prepare-image", "--file", path.string(), "--storage", storage,
        image.checksum_algorithm == "sha512" ? "--sha512" : "--sha256", image.checksum});
}

static void official_download() {
    auto distro = dialog({"--list", "--width=720", "--height=350", "--column=ID", "--column=" + tr("column.source"), "--hide-column=1", "--print-column=1",
        "--text=" + tr("download.notice"),
        "ubuntu", "Ubuntu LTS — " + tr("edition.desktop") + " / " + tr("edition.server"), "debian", "Debian netinst", "fedora", "Fedora Workstation / Server"});
    if (distro.empty()) return;
    auto arch = dialog({"--list", "--column=ID", "--column=" + tr("column.arch"), "--hide-column=1", "--print-column=1",
        "--text=" + tr("download.arch", {{"arch", host_arch()}}),
        "arm64", "ARM64 / AArch64", "amd64", "x86_64 / AMD64"}); if (arch.empty()) return;
    std::string release = "stable", edition = "netinst";
    if (distro == "ubuntu") {
        release = dialog({"--list", "--column=" + tr("column.version"), "24.04", "26.04"}); if (release.empty()) return;
        edition = dialog({"--list", "--column=ID", "--column=" + tr("column.edition"), "--hide-column=1", "--print-column=1", "desktop", tr("edition.desktop"), "server", tr("edition.server")});
    } else if (distro == "fedora") edition = dialog({"--list", "--column=ID", "--column=" + tr("column.edition"), "--hide-column=1", "--print-column=1", "workstation", tr("edition.workstation"), "server", tr("edition.server_dvd")});
    if (edition.empty()) return;
    auto directory = dialog({"--file-selection", "--directory", "--text=" + tr("download.directory")}); if (directory.empty()) return;
    OfficialImage image;
    { BusyDialog busy; image = resolve_image(distro, release, edition, arch); }
    fs::path result;
    { DownloadDialog progress; result = download_official(image, directory, [&](const TransferProgress& p) { return progress.update(p); }); progress.complete(); }
    show_text(tr("download.result", {{"path", result.string()}}));
}

int gui(bool preview) {
    if (preview) {
        std::cout << json{{"title", tr("title")}, {"language", "C++20"},
            {"ui_language", language()}, {"text_direction", language() == "ar" ? "rtl" : "ltr"}, {"available_languages", available_languages()},
            {"actions", {tr("action.download"), tr("action.prepare"), tr("action.plan"), tr("action.restore"), tr("action.cancel"), tr("action.advanced"), tr("action.diagnose"), tr("action.check"), tr("action.language")}},
            {"preparation_methods", {tr("prepare.local"), tr("prepare.auto")}},
            {"requires_url_in_normal_flow", false}, {"iso_automatic_install", false},
            {"preview_requires_prepared_source", true}, {"destructive_confirmation", ERASE_CONFIRMATION}}.dump(2) << '\n';
        return 0;
    }
    need(geteuid() != 0, tr("error.gui_root"));
    // Dialogs are 2D: avoid fragile VM EGL/Zink acceleration by default.
    // This affects this process/children only and preserves an explicit user override.
    // Zenity converts argv using the locale charset; minimal containers often default to ASCII.
    configure_dialog_language();
    signal(SIGPIPE, SIG_IGN); // A closed progress window cancels the transfer instead of terminating us.
    setenv("GSK_RENDERER", "cairo", 0);
    setenv("LIBGL_ALWAYS_SOFTWARE", "1", 0);
    for (;;) {
        auto status = preparation_status(); bool ready = status.at("ready");
        auto choice = dialog({"--list", "--width=700", "--height=420",
            "--text=" + tr("main.notice") + "\n" + status.at("reason").get<std::string>(),
            "--column=ID", "--column=" + tr("column.action"), "--column=" + tr("column.status"), "--hide-column=1", "--print-column=1",
            "download", tr("action.download"), tr("status.download"),
            "prepare", tr("action.prepare"), ready ? tr("status.reselect") : tr("status.prepare_first"),
            "plan", tr("action.plan"), ready ? tr("status.preview") : tr("status.needs_image"),
            "restore", tr("action.restore"), ready ? tr("status.confirm") : tr("status.needs_image"),
            "cancel", tr("action.cancel"), tr("status.cancel"),
            "advanced", tr("action.advanced"), tr("status.advanced"),
            "diagnose", tr("action.diagnose"), tr("status.diagnose"),
            "check", tr("action.check"), tr("status.check"),
            "language", tr("action.language"), language()});
        if (choice.empty()) return 0;
        try {
            if ((choice == "plan" || choice == "restore") && !ready) {
                show_text(status.at("reason")); continue;
            }
            if (choice == "download") official_download();
            else if (choice == "language") {
                Args options{"--list", "--width=600", "--height=380", "--column=ID", "--column=" + tr("column.language"), "--hide-column=1", "--print-column=1",
                    "--text=" + tr("language.choose"), "auto", tr("language.auto")};
                for (const auto& row : available_languages()) { options.push_back(row.at("code").get<std::string>()); options.push_back(row.at("name").get<std::string>()); }
                auto selected = dialog(options); if (selected.empty()) continue;
                auto previous = language();
                try { set_language(selected); configure_dialog_language(); }
                catch (...) { set_language(previous); configure_dialog_language(); throw; }
                try { save_language(selected); } catch (const std::exception&) { show_text(tr("language.session")); }
            }
            else if (choice == "check") show_text(environment_text(check_environment()));
            else if (choice == "diagnose") {
                auto path = dialog({"--file-selection", "--save", "--filename=linux-factory-reset-diagnostics.json",
                    "--text=" + tr("diagnose.prompt")});
                if (path.empty()) continue;
                auto report = diagnostics(); export_diagnostics(path, report);
                show_text(tr("diagnose.saved", {{"path", path}}) + "\n\n" + report.dump(2));
            }
            else if (choice == "prepare") {
                auto sources = detected_sources(); bool automatic = sources.at("automatic_available");
                Args options{"--list", "--width=720", "--height=320", "--column=ID", "--column=" + tr("column.source"), "--hide-column=1", "--print-column=1",
                    "--text=" + tr("prepare.source") + "\n" + sources.at("reason").get<std::string>(),
                    "local", tr("prepare.local")};
                if (automatic) { options.push_back("automatic"); options.push_back(tr("prepare.auto")); }
                auto source = dialog(options); if (source.empty()) continue;
                std::string image;
                if (source == "local") {
                    image = dialog({"--file-selection", "--file-filter=" + tr("filter.images") + " | *.img *.raw *.img.xz *.raw.xz *.qcow2 *.squashfs", "--file-filter=" + tr("filter.all") + " | *"});
                    if (image.empty()) continue;
                    image_format(image); // Reject installer ISO with an explanation before elevation.
                }
                show_text(tr("prepare.storage"));
                auto store = dialog({"--file-selection", "--directory"}); if (store.empty()) continue;
                if (source == "local") privileged({"prepare-image", "--file", image, "--storage", store});
                else if (privileged({"check", "--storage", store}, false, false)) automatic_preparation(sources, store);
            } else if (choice == "plan") privileged({"plan"}, true);
            else if (choice == "cancel") privileged({"cancel"});
            else if (choice == "restore") {
                auto answer = dialog({"--entry", "--width=680", "--text=" + tr("restore.warning", {{"token", ERASE_CONFIRMATION}})});
                if (answer == ERASE_CONFIRMATION) privileged({"arm", "--confirm", ERASE_CONFIRMATION, "--reboot"});
            } else if (choice == "advanced") {
                auto action = dialog({"--list", "--column=ID", "--column=" + tr("action.advanced"), "--hide-column=1", "--print-column=1",
                    "--text=" + tr("advanced.notice"),
                    "custom", tr("advanced.custom"), "firmware", tr("advanced.firmware")});
                if (action == "firmware") show_text(firmware_status().dump(2));
                else if (action == "custom") {
                    auto url = dialog({"--entry", "--width=650", "--text=" + tr("advanced.url")}); if (url.empty()) continue;
                    auto sum = dialog({"--entry", "--width=650", "--text=" + tr("advanced.hash")}); if (sum.empty()) continue;
                    auto store = dialog({"--file-selection", "--directory"}); if (store.empty()) continue;
                    privileged({"prepare-profile", "--url", url, "--sha256", sum, "--storage", store});
                }
            }
        } catch (const std::exception& e) { show_text(localize_message(e.what())); }
    }
}
}
