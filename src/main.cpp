#include "reset.hpp"
#include "download.hpp"
#include <fcntl.h>
#include <iostream>
#include <set>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

static void help() {
    std::cout << "Linux Factory Reset " << uf::VERSION << " (C++20, experimental)\n"
        "  linux-factory-reset gui [--preview]\n"
        "  linux-factory-reset status\n"
        "  linux-factory-reset catalog\n"
        "  linux-factory-reset resolve --distro fedora --release stable --edition workstation --arch arm64\n"
        "  linux-factory-reset download --distro debian --release stable --edition netinst --arch arm64 --directory ~/Downloads\n"
        "  linux-factory-reset sources  # show actual built-in download availability\n"
        "  sudo linux-factory-reset prepare-image --file /path/to/system.img --storage /mnt/recovery\n"
        "  sudo linux-factory-reset prepare-auto --storage /mnt/recovery\n"
        "  # Advanced administrator interface: prepare-profile --url URL --sha256 HASH --storage PATH\n"
        "  sudo linux-factory-reset make-profile --root /mnt/pristine --boot /mnt/pristine-boot --platform x86_64-uefi --output /mnt/factory.squashfs\n"
        "  # --platform arm64-uefi supports generic ARM64 UEFI/GRUB devices, including Debian\n"
        "  sudo linux-factory-reset plan\n"
        "  sudo linux-factory-reset arm --confirm ERASE-ALL-USERS [--reboot]\n"
        "  sudo linux-factory-reset cancel\n"
        "  linux-factory-reset firmware-status\n"
        "  sudo linux-factory-reset reset-firmware --confirm RESET-BIOS-DEFAULTS\n"
        "Prepare commands optionally accept --proxy http://127.0.0.1:PORT or socks5h://127.0.0.1:PORT.\n"
        "Actual restore runs offline in initramfs; it removes ALL personal users/data.\n";
}
int main(int argc, char** argv) {
    try {
        if (geteuid() == 0) umask(0022);
        if (argc < 2 || std::string(argv[1]) == "--help") { help(); return 0; }
        std::string cmd = argv[1]; if (cmd == "--version") { std::cout << uf::VERSION << '\n'; return 0; }
        std::map<std::string, std::set<std::string>> allowed{
            {"gui", {"--preview"}}, {"status", {}}, {"sources", {}}, {"firmware-status", {}}, {"plan", {}}, {"cancel", {}},
            {"catalog", {}}, {"resolve", {"--distro", "--release", "--edition", "--arch", "--proxy"}},
            {"download", {"--distro", "--release", "--edition", "--arch", "--directory", "--proxy"}},
            {"prepare-image", {"--file", "--storage"}}, {"prepare-auto", {"--storage", "--proxy"}},
            {"prepare-pi", {"--release", "--storage", "--proxy"}}, {"prepare-profile", {"--url", "--sha256", "--storage", "--proxy"}},
            {"make-profile", {"--root", "--boot", "--platform", "--output"}},
            {"arm", {"--confirm", "--reboot"}}, {"reset-firmware", {"--confirm"}}};
        uf::need(allowed.contains(cmd), "Unknown command; use --help"); std::map<std::string, std::string> options;
        for (int i = 2; i < argc; ++i) {
            std::string key = argv[i]; uf::need(allowed[cmd].contains(key) && !options.contains(key), "Unknown or repeated option: " + key);
            if (key == "--reboot" || key == "--preview") options[key] = "yes";
            else { uf::need(i + 1 < argc, "Missing option value: " + key); options[key] = argv[++i]; }
        }
        auto get = [&](const std::string& key) { uf::need(options.contains(key), "Required option: " + key); return options.at(key); };
        if (cmd == "gui") return uf::gui(options.contains("--preview"));
        if (cmd == "sources") { std::cout << uf::detected_sources().dump(2) << '\n'; return 0; }
        if (cmd == "firmware-status") { std::cout << uf::firmware_status().dump(2) << '\n'; return 0; }
        if (cmd == "status") {
            if (!uf::fs::exists(uf::STATE)) std::cout << "No factory profile prepared. No reset performed.\n";
            else { auto s = uf::load_state(); s["reset_pending"] = uf::fs::exists(uf::REQUEST); std::cout << s.dump(2) << '\n'; }
            return 0;
        }
        if (cmd == "catalog") { std::cout << uf::download_catalog().dump(2) << '\n'; return 0; }
        if (cmd == "resolve" || cmd == "download") {
            auto proxy = options.contains("--proxy") ? get("--proxy") : "";
            auto image = uf::resolve_image(get("--distro"), get("--release"), get("--edition"), get("--arch"), proxy);
            if (cmd == "resolve") std::cout << image.describe().dump(2) << '\n';
            else { auto path = uf::download_official(image, get("--directory"), [](const uf::TransferProgress& p) { std::cerr << "\r" << p.percent() << "%  " << p.text() << "          " << std::flush; return true; }, proxy); std::cout << "\nSaved: " << path << "\nSHA-256 OK. ISO automatic restore is not implemented.\n"; }
            return 0;
        }
        uf::root_only();
        if (options.contains("--proxy")) uf::set_download_proxy(get("--proxy"));
        int lock = open("/run/lock/ubuntu-reset.lock", O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
        uf::need(lock >= 0, "Cannot open operation lock"); struct stat st{};
        uf::need(fstat(lock, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == 0 && !(st.st_mode & 0077), "Unsafe operation lock");
        uf::need(flock(lock, LOCK_EX | LOCK_NB) == 0, "Another Linux Factory Reset operation is active");
        if (cmd == "prepare-image") uf::prepare_image(get("--file"), get("--storage"));
        else if (cmd == "prepare-auto") uf::prepare_automatic(get("--storage"));
        else if (cmd == "prepare-pi") uf::prepare_pi(options.contains("--release") ? get("--release") : "26.04.1", get("--storage"));
        else if (cmd == "prepare-profile") uf::prepare_profile(get("--url"), get("--sha256"), get("--storage"));
        else if (cmd == "plan") uf::plan(uf::load_state());
        else if (cmd == "arm") uf::arm(uf::load_state(), get("--confirm"), options.contains("--reboot"));
        else if (cmd == "cancel") uf::cancel();
        else if (cmd == "reset-firmware") uf::firmware_reset(get("--confirm"));
        else if (cmd == "make-profile") {
            auto root = uf::fs::canonical(get("--root")); auto boot = uf::fs::canonical(get("--boot"));
            struct stat live{}, source{};
            uf::need(stat("/etc", &live) == 0 && stat((root / "etc").c_str(), &source) == 0 && live.st_dev != source.st_dev,
                     "Use an offline pristine installed filesystem, not the running root");
            for (const auto& tree : {root, boot}) {
                auto mount = uf::json::parse(uf::output({"findmnt", "--json", "--target", tree.string(), "-o", "OPTIONS"}));
                auto opts = "," + mount.at("filesystems")[0].at("options").get<std::string>() + ",";
                uf::need(opts.find(",ro,") != std::string::npos, "Pristine reference partitions must be mounted read-only");
            }
            auto out = uf::fs::absolute(get("--output")); uf::secure(out.parent_path());
            uf::make_profile(root, boot, get("--platform"), out, {{"type", "administrator pristine installed reference"}});
            std::cout << "SHA256: " << uf::sha256(out) << '\n';
        }
        close(lock); return 0;
    } catch (const std::exception& e) { std::cerr << "Linux Factory Reset: " << e.what() << '\n'; return 1; }
}
