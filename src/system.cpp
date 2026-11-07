#include "reset.hpp"
#include <chrono>
#include <fcntl.h>
#include <iostream>
#include <regex>
#include <sstream>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <thread>
#include <unistd.h>

namespace uf {
static std::string proxy;
void set_download_proxy(const std::string& value) {
    need(std::regex_match(value, std::regex("(http|socks5h)://127\\.0\\.0\\.1:[0-9]{1,5}")),
         "Proxy must be an explicit loopback http://127.0.0.1:PORT or socks5h://127.0.0.1:PORT without credentials");
    proxy = value;
}
static std::string str(const fs::path& p) { return p.string(); }
static json filesystem(const fs::path& p) {
    auto data = json::parse(output({"findmnt", "--json", "--target", str(p), "--output", "TARGET,SOURCE,FSTYPE,UUID,OPTIONS"}));
    need(data.at("filesystems").size() == 1, "Cannot establish filesystem identity");
    auto fsinfo = data["filesystems"][0]; need(fsinfo["uuid"].is_string() && valid_uuid(fsinfo["uuid"]), "Filesystem has no usable UUID");
    return fsinfo;
}
static std::string machine() { struct utsname u{}; need(uname(&u) == 0, "uname failed"); return u.machine; }
static std::string kernel() { struct utsname u{}; need(uname(&u) == 0, "uname failed"); return u.release; }
json hardware() {
    std::string kind, boot;
    if (machine() == "aarch64" && fs::exists("/proc/device-tree/model") && read("/proc/device-tree/model").find("Raspberry Pi 5") != std::string::npos) {
        kind = "raspi-arm64"; boot = "/boot/firmware";
    } else if (machine() == "x86_64" && fs::exists("/sys/firmware/efi")) {
        kind = "x86_64-uefi"; boot = "/boot/efi";
    } else throw Error("Supported targets: Raspberry Pi 5 or x86_64 UEFI Ubuntu");
    auto root = filesystem("/"); auto fw = filesystem(boot);
    need(root["fstype"] == "ext4" && fw["fstype"] == "vfat" && root["target"] == "/" && fw["target"] == boot,
         "Requires ordinary ext4 root + separate FAT firmware/EFI partition");
    for (const auto& part : {root, fw}) need(output({"lsblk", "-dn", "-o", "TYPE", part.at("source")}) == "part", "LVM, encryption, RAID and whole-device filesystems are unsupported");
    auto mounts = json::parse(output({"findmnt", "--json", "--list", "-o", "TARGET,SOURCE,FSTYPE"}));
    for (const auto& m : mounts.at("filesystems")) {
        auto target = m.at("target").get<std::string>(); auto src = m.at("source").get<std::string>();
        if (src.starts_with("/dev/") && m.at("fstype") != "squashfs")
            need(target == "/" || target == boot || target.starts_with("/media/") || target.starts_with("/mnt/") || target.starts_with("/run/"), "Unsupported additional system/data mount: " + target);
    }
    return {{"platform", kind}, {"root", root}, {"boot", fw}};
}
struct Storage {
    fs::path work; json fsinfo; json hw;
    explicit Storage(const fs::path& path) {
        auto p = fs::canonical(path); fsinfo = filesystem(p); hw = hardware();
        need(fsinfo["fstype"] == "ext4" && fsinfo.at("options").get<std::string>().find("rw") != std::string::npos,
             "Recovery storage must be writable ext4");
        need(fsinfo["target"] == str(p), "Select the recovery partition mountpoint, not a subdirectory");
        need(fsinfo["uuid"] != hw["root"]["uuid"] && fsinfo["uuid"] != hw["boot"]["uuid"], "Recovery storage must be a separate filesystem");
        work = p / "ubuntu-reset";
        if (!fs::exists(work)) { fs::create_directory(work); fs::permissions(work, fs::perms::owner_all); }
        secure(work);
    }
};
static void no_pending() { need(!fs::exists(REQUEST) && !fs::is_symlink(REQUEST), "Cancel the pending reset before preparing another profile"); }
static void download(const std::string& url, const fs::path& dest, const std::string& expected = "", bool official = false, uint64_t limit = 16ULL << 30) {
    https(url, official);
    if (fs::exists(dest)) { need(!expected.empty() && sha256(dest) == expected, "Existing download is damaged; inspect/remove it first"); return; }
    auto partial = fs::path(str(dest) + ".part"); need(!fs::is_symlink(partial), "Unsafe partial download");
    need(fs::space(dest.parent_path()).available > (1ULL << 30), "Recovery storage has less than 1 GiB free");
    try {
        Args args{"curl", "--disable", "--fail", "--location", "--proto", "=https", "--proto-redir", "=https", "--max-redirs", "4",
             "--connect-timeout", "30", "--retry", "2", "--retry-delay", "6", "--max-filesize", std::to_string(limit), "--output", str(partial), "--url", url};
        if (!proxy.empty()) { args.push_back("--proxy"); args.push_back(proxy); }
        run(args);
        need(fs::file_size(partial) <= limit, "Download exceeds size limit");
        need(expected.empty() || sha256(partial) == expected, "Downloaded file failed SHA-256 verification");
        int fd = open(partial.c_str(), O_RDONLY | O_NOFOLLOW); need(fd >= 0, "Cannot flush download"); int rc = fsync(fd); close(fd); need(rc == 0, "Cannot flush download");
        fs::rename(partial, dest);
    } catch (...) { std::error_code ec; fs::remove(partial, ec); throw; }
}
static json reference(const fs::path& root, const fs::path& boot, const std::string& kind, bool prepared) {
    need(kind == "raspi-arm64" || kind == "x86_64-uefi", "Unsupported platform");
    need(fs::is_regular_file(root / "usr/lib/os-release"), "Reference is not an installed Ubuntu filesystem");
    need(std::regex_search(read(root / "usr/lib/os-release"), std::regex("(^|\n)ID=\"?ubuntu\"?(\n|$)")), "Only Ubuntu profiles are supported");
    for (const auto& name : {"etc/passwd", "etc/shadow", "etc/fstab", "var/lib/dpkg/status"})
        need(fs::is_regular_file(root / name) && !fs::is_symlink(root / name), "Missing installed-system reference file: " + std::string(name));
    std::istringstream passwd(read(root / "etc/passwd")); std::string line;
    while (std::getline(passwd, line)) {
        std::istringstream fields(line); std::vector<std::string> f; std::string value;
        while (std::getline(fields, value, ':')) f.push_back(value);
        need(f.size() == 7 && std::regex_match(f[2], std::regex("[0-9]+")), "Invalid reference passwd");
        auto uid = std::stoul(f[2]);
        need(!(uid >= 1000 && uid < 60000 && f[0] != "oem" && f[0] != "gnome-initial-setup"), "Reference contains a personal user: " + f[0]);
    }
    need(!fs::is_symlink(root / "home"), "Reference home cannot be a symlink");
    if (fs::exists(root / "home")) for (const auto& e : fs::directory_iterator(root / "home")) need(e.path().filename() == "oem", "Reference contains personal home directories");
    need(!fs::exists(root / "casper") && !fs::exists(root / "rofs"), "A live ISO root is not an installed-system reference");
    need(!fs::exists(root / "var/lib/cloud/instance"), "Reference has already completed cloud initialization");
    need(!fs::exists(root / "var/lib/ubuntu-reset-firstboot-complete") && !fs::exists(root / "var/lib/ubuntu-reset-firstboot-user"),
         "Reference initial-user setup has already run");
    auto elf = read(root / "usr/bin/bash");
    need(elf.size() >= 20 && elf.substr(0, 4) == std::string("\177ELF", 4) && elf[4] == 2 && elf[5] == 1,
         "Reference must contain a 64-bit little-endian installed bash binary");
    unsigned arch = static_cast<unsigned char>(elf[18]) | (static_cast<unsigned>(static_cast<unsigned char>(elf[19])) << 8);
    need(arch == (kind == "raspi-arm64" ? 183U : 62U), "Reference CPU architecture does not match profile platform");
    if (kind == "raspi-arm64") {
        need(fs::is_regular_file(boot / "config.txt"), "Missing Pi firmware config.txt"); bool cmdline = false, modules = false;
        need(fs::is_regular_file(boot / "current/vmlinuz") && fs::is_regular_file(boot / "current/initrd.img"), "Missing standard Ubuntu Pi current/ kernel and initrd");
        for (const auto& e : fs::recursive_directory_iterator(boot)) if (e.path().filename() == "cmdline.txt" && e.is_regular_file()) cmdline = true;
        for (const auto& base : {root / "usr/lib/modules", root / "lib/modules"})
            if (fs::is_directory(base)) for (const auto& e : fs::directory_iterator(base)) if (str(e.path().filename()).find("raspi") != std::string::npos) modules = true;
        need(cmdline && modules, "Missing Pi command line or kernel modules");
    } else {
        need(fs::is_regular_file(root / "boot/grub/grub.cfg") && fs::is_regular_file(boot / "EFI/ubuntu/grub.cfg"), "Only standard installed Ubuntu GRUB/EFI layout is supported");
        need(fs::is_regular_file(boot / "EFI/ubuntu/shimx64.efi") && fs::is_regular_file(boot / "EFI/ubuntu/grubx64.efi"), "Missing Ubuntu EFI binaries");
        if (prepared) need(fs::is_regular_file(root / "var/lib/ubuntu-reset-firstboot-ready") && fs::is_regular_file(root / "usr/lib/ubuntu-reset/firstboot.sh"), "x86 profile is missing the initial-user setup flow");
    }
    return {{"schema", 1}, {"platform", kind}};
}
static void replace_all(std::string& text, const std::string& from, const std::string& to) {
    need(!from.empty(), "Cannot replace empty text"); size_t i = 0;
    while ((i = text.find(from, i)) != std::string::npos) { text.replace(i, from.size(), to); i += to.size(); }
}
void make_profile(const fs::path& root, const fs::path& boot, const std::string& kind, const fs::path& dest, const json& provenance) {
    auto meta = reference(root, boot, kind, false); need(!fs::exists(dest), "Destination already exists");
    auto size = std::stoull(output({"du", "-sx", "--block-size=1", str(root)}));
    need(fs::space(dest.parent_path()).available > size * 2 + (1ULL << 30), "Need space for staging plus compressed recovery data");
    TempDir stage(dest.parent_path()); auto r = stage.path / "root", b = stage.path / "boot"; fs::create_directory(r); fs::create_directory(b);
    run({"rsync", "-aHAXx", "--numeric-ids", "--exclude=/dev/***", "--exclude=/proc/***", "--exclude=/sys/***", "--exclude=/run/***", "--exclude=/boot/firmware/***", "--exclude=/boot/efi/***", str(root) + "/", str(r) + "/"});
    run({"rsync", "-rt", str(boot) + "/", str(b) + "/"});
    // Root's personal data must not become part of an administrator-created profile.
    fs::remove_all(r / "root"); fs::create_directory(r / "root"); fs::permissions(r / "root", fs::perms::owner_all);
    for (const auto& name : {"etc/machine-id", "var/lib/dbus/machine-id"}) if (fs::exists(r / name) && !fs::is_symlink(r / name)) write(r / name, "");
    fs::remove(r / "var/lib/systemd/random-seed");
    if (fs::is_directory(r / "etc/ssh")) for (const auto& e : fs::directory_iterator(r / "etc/ssh")) if (str(e.path().filename()).starts_with("ssh_host_") && e.is_regular_file()) fs::remove(e.path());
    auto boot_mount = kind == "raspi-arm64" ? "/boot/firmware" : "/boot/efi";
    std::istringstream lines(read(r / "etc/fstab")); std::string line, old_uuid; bool hasroot = false;
    while (std::getline(lines, line)) {
        auto pos = line.find_first_not_of(" \t"); if (pos == std::string::npos || line[pos] == '#') continue;
        std::istringstream f(line); std::string spec, point, type; f >> spec >> point >> type;
        need(point == "/" || point == boot_mount, "Complex fstab/swap layouts are unsupported");
        if (point == "/") { need(type == "ext4", "Reference root must be ext4"); hasroot = true; if (spec.starts_with("UUID=")) old_uuid = spec.substr(5); }
    }
    need(hasroot, "Missing reference root fstab entry");
    write(r / "etc/fstab", "UUID=@ROOT_UUID@ / ext4 defaults 0 1\nUUID=@BOOT_UUID@ " + std::string(boot_mount) + " vfat defaults 0 2\n");
    if (kind == "raspi-arm64") {
        for (const auto& e : fs::recursive_directory_iterator(b)) if (e.path().filename() == "cmdline.txt" && e.is_regular_file()) {
            need(!e.is_symlink(), "Unsafe cmdline symlink"); auto text = read(e.path());
            need(std::regex_search(text, std::regex("\\broot=\\S+")), "Missing Pi root= argument");
            write(e.path(), std::regex_replace(text, std::regex("\\broot=\\S+"), "root=UUID=@ROOT_UUID@"));
        }
    } else {
        need(valid_uuid(old_uuid), "x86 reference must use a root UUID in fstab");
        for (const auto& p : {r / "boot/grub/grub.cfg", b / "EFI/ubuntu/grub.cfg"}) {
            need(!fs::is_symlink(p), "Unsafe GRUB config symlink"); auto text = read(p); replace_all(text, old_uuid, "@ROOT_UUID@"); write(p, text);
        }
        // A fresh x86 profile uses our local-console first-user wizard.
        fs::create_directories(r / "usr/lib/ubuntu-reset"); fs::create_directories(r / "etc/systemd/system/multi-user.target.wants");
        fs::copy_file("/usr/lib/ubuntu-reset/firstboot.sh", r / "usr/lib/ubuntu-reset/firstboot.sh", fs::copy_options::overwrite_existing);
        fs::copy_file("/usr/lib/ubuntu-reset/ubuntu-reset-firstboot.service", r / "etc/systemd/system/ubuntu-reset-firstboot.service", fs::copy_options::overwrite_existing);
        fs::create_directories(r / "etc/systemd/system/display-manager.service.d");
        write(r / "etc/systemd/system/display-manager.service.d/ubuntu-reset-firstboot.conf",
              "[Unit]\nRequires=ubuntu-reset-firstboot.service\nAfter=ubuntu-reset-firstboot.service\n");
        auto link = r / "etc/systemd/system/multi-user.target.wants/ubuntu-reset-firstboot.service";
        fs::remove(link); fs::create_symlink("../ubuntu-reset-firstboot.service", link);
        write(r / "var/lib/ubuntu-reset-firstboot-ready", "console-setup-v1\n");
    }
    // Empty runtime mountpoint directories are needed for the next boot.
    for (const auto& dir : {"dev", "proc", "sys", "run", "boot/firmware", "boot/efi"}) fs::create_directories(r / dir);
    meta["provenance"] = provenance; meta["semantics"] = "initial system; all personal accounts/data removed";
    write(stage.path / "profile.json", meta.dump(2) + "\n");
    run({"mksquashfs", str(stage.path), str(dest), "-noappend", "-comp", "zstd", "-processors", "2", "-no-progress"});
}
static void register_profile(const fs::path& profile, const Storage& st, const json& provenance) {
    no_pending(); auto id = random_id(); auto final = st.work / ("factory-" + id + ".squashfs");
    fs::rename(profile, final); fs::permissions(final, fs::perms::owner_read | fs::perms::owner_write);
    json state{{"schema", 1}, {"id", id}, {"platform", st.hw["platform"]}, {"root_uuid", st.hw["root"]["uuid"]},
               {"boot_uuid", st.hw["boot"]["uuid"]}, {"store_uuid", st.fsinfo["uuid"]}, {"file", "ubuntu-reset/" + str(final.filename())},
               {"sha256", sha256(final)}, {"provenance", provenance}};
    validate_state(state); atomic_write(STATE, state.dump(2) + "\n", 0644);
    std::cout << "Recovery data prepared. No reset has been performed.\n";
}
struct Loop {
    std::string device;
    explicit Loop(const fs::path& image) { device = output({"losetup", "--find", "--show", "--read-only", "--partscan", str(image)}); }
    ~Loop() { try { command({"losetup", "--detach", device}, false); } catch (...) {} }
};
void prepare_pi(const std::string& release, const fs::path& location) {
    root_only(); no_pending(); Storage st(location); need(st.hw["platform"] == "raspi-arm64", "Official Pi images are only supported on Raspberry Pi 5");
    need(std::regex_match(release, std::regex("[0-9]{2}\\.[0-9]{2}(\\.[0-9]+)?")), "Invalid Ubuntu release");
    auto file = "ubuntu-" + release + "-preinstalled-desktop-arm64+raspi.img.xz";
    auto base = "https://cdimage.ubuntu.com/releases/" + release + "/release/"; std::string sum;
    {
        TempDir tmp(st.work); auto sums = tmp.path / "SHA256SUMS", signature = tmp.path / "SHA256SUMS.gpg";
        download(base + "SHA256SUMS", sums, "", true, 1024 * 1024); std::this_thread::sleep_for(std::chrono::seconds(6));
        download(base + "SHA256SUMS.gpg", signature, "", true, 1024 * 1024);
        run({"gpgv", "--keyring", "/usr/share/keyrings/ubuntu-archive-keyring.gpg", str(signature), str(sums)});
        sum = signed_checksum(read(sums), file);
    }
    std::this_thread::sleep_for(std::chrono::seconds(6)); auto compressed = st.work / file; download(base + file, compressed, sum, true);
    auto listing = output({"xz", "--robot", "--list", str(compressed)}); std::istringstream lines(listing); std::string line; uint64_t expanded = 0;
    while (std::getline(lines, line)) if (line.starts_with("totals\t")) {
        std::istringstream cols(line); std::string tag, streams, blocks, packed; cols >> tag >> streams >> blocks >> packed >> expanded;
    }
    need(expanded && expanded < (128ULL << 30), "Invalid XZ expanded image size");
    need(fs::space(st.work).available > expanded * 2 + (2ULL << 30), "Insufficient space for decompressed image + staging; use a larger ext4 recovery partition");
    TempDir tmp(st.work); auto image = tmp.path / "source.img", profile = st.work / "prepared.squashfs";
    need(!fs::exists(profile), "Stale prepared.squashfs exists; inspect/remove it first");
    int fd = open(image.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600); need(fd >= 0, "Cannot create source image");
    auto rc = command({"xz", "--decompress", "--stdout", str(compressed)}, false, fd).code; close(fd); need(rc == 0, "Image decompression failed");
    json provenance{{"url", base + file}, {"sha256", sum}, {"verified", "Ubuntu CD image GPG signature"}};
    {
        Loop loop(image); run({"udevadm", "settle"}); auto partitions = json::parse(output({"lsblk", "--json", "-o", "PATH,FSTYPE", loop.device}));
        std::vector<std::string> roots, boots;
        for (const auto& p : partitions["blockdevices"][0].at("children")) {
            if (p["fstype"] == "ext4") roots.push_back(p["path"]);
            if (p["fstype"] == "vfat") boots.push_back(p["path"]);
        }
        need(roots.size() == 1 && boots.size() == 1, "Expected exactly one ext4 root and one FAT firmware partition");
        Mount source(roots[0], "ext4", "ro,noload,nosuid,nodev,noexec"), boot(boots[0], "vfat", "ro,nosuid,nodev,noexec");
        make_profile(source.temp.path, boot.temp.path, "raspi-arm64", profile, provenance);
    }
    register_profile(profile, st, provenance);
}
void prepare_profile(const std::string& url, const std::string& sum, const fs::path& location) {
    root_only(); no_pending(); need(valid_sha(sum), "Provide an independently trusted SHA-256 for the factory profile"); Storage st(location);
    auto profile = st.work / ("download-" + sum + ".squashfs"); download(url, profile, sum);
    {
        Mount source(profile, "squashfs", "ro,loop,nosuid,nodev,noexec"); auto meta = json::parse(read(source.temp.path / "profile.json"));
        need(meta.at("schema") == 1 && meta.at("platform") == st.hw["platform"], "Profile platform does not match target");
        reference(source.temp.path / "root", source.temp.path / "boot", st.hw["platform"], true);
        need(read(source.temp.path / "root/etc/fstab").find("@ROOT_UUID@") != std::string::npos, "Factory profile is not relocatable");
    }
    register_profile(profile, st, {{"url", url}, {"sha256", sum}});
}
fs::path preflight(const json& s) {
    validate_state(s); auto hw = hardware();
    need(hw["platform"] == s["platform"] && hw["root"]["uuid"] == s["root_uuid"] && hw["boot"]["uuid"] == s["boot_uuid"], "Target layout changed; prepare a new recovery profile");
    auto mounts = json::parse(output({"findmnt", "--json", "--source", "UUID=" + s.at("store_uuid").get<std::string>(), "-o", "TARGET"}));
    need(mounts.at("filesystems").size() == 1, "Mount the prepared recovery partition first");
    auto path = fs::path(mounts["filesystems"][0].at("target").get<std::string>()) / s.at("file").get<std::string>();
    secure(path); need(sha256(path) == s.at("sha256").get<std::string>(), "Recovery profile is damaged or changed"); return path;
}
void plan(const json& s) {
    root_only(); Mount source(preflight(s), "squashfs", "ro,loop,nosuid,nodev,noexec");
    std::cout << "ROOT: this preview includes removal of all personal users and files.\n" << std::flush;
    run({"rsync", "-aHAXxnci", "--numeric-ids", "--delete", "--exclude=/dev/***", "--exclude=/proc/***", "--exclude=/sys/***", "--exclude=/run/***", "--exclude=/boot/firmware/***", "--exclude=/boot/efi/***", "--exclude=/var/lib/ubuntu-reset/***", str(source.temp.path / "root") + "/", "/"});
    auto boot = s["platform"] == "raspi-arm64" ? "/boot/firmware/" : "/boot/efi/";
    std::cout << "BOOT:\n" << std::flush; run({"rsync", "-rtnci", "--delete", str(source.temp.path / "boot") + "/", boot});
    std::cout << "Preview only. UUID placeholders are replaced offline. Pending state is removed after successful restore.\n";
}
void arm(const json& s, const std::string& confirmation, bool reboot) {
    root_only(); need(confirmation == "ERASE-ALL-USERS", "Requires --confirm ERASE-ALL-USERS"); no_pending(); preflight(s);
    need(fs::is_regular_file("/etc/initramfs-tools/hooks/ubuntu-reset") && fs::is_regular_file("/etc/initramfs-tools/scripts/local-bottom/ubuntu-reset"), "Install the recovery hooks first");
    secure("/etc/initramfs-tools/hooks/ubuntu-reset"); secure("/etc/initramfs-tools/scripts/local-bottom/ubuntu-reset");
    atomic_write("/etc/ubuntu-reset/boot.conf", boot_config(s)); auto k = kernel(); run({"update-initramfs", "-u", "-k", k});
    auto initrd = fs::path("/boot") / ("initrd.img-" + k); auto contents = output({"lsinitramfs", str(initrd)});
    need(contents.find("scripts/local-bottom/ubuntu-reset") != std::string::npos && contents.find("conf/ubuntu-reset.conf") != std::string::npos, "Recovery files are absent from initramfs");
    if (s["platform"] == "raspi-arm64") {
        auto config = read("/boot/firmware/config.txt");
        need(stock_pi_boot(config), "Unsupported/ambiguous Pi boot recipe");
        need(sha256("/boot/firmware/current/initrd.img") == sha256(initrd), "flash-kernel did not update the active Pi initrd");
    } else need(read("/boot/grub/grub.cfg").find("initrd.img-" + k) != std::string::npos, "Current kernel initrd is not listed in GRUB");
    atomic_write(REQUEST, s.at("id").get<std::string>() + "\n");
    std::cout << "Reset armed for next normal boot. Cancel before reboot: sudo ubuntu-reset cancel\n" << std::flush;
    if (reboot) run({"systemctl", "reboot"});
}
void cancel() {
    root_only(); if (fs::exists(REQUEST) || fs::is_symlink(REQUEST)) { secure(REQUEST); fs::remove(REQUEST); sync(); }
    std::cout << "Pending reset cancelled. System files were not restored.\n";
}
json firmware_status() {
    std::vector<std::string> matches; fs::path base = "/sys/class/firmware-attributes";
    if (fs::is_directory(base)) for (const auto& e : fs::directory_iterator(base)) {
        auto p = e.path() / "attributes/reset_bios"; if (!fs::exists(p)) continue;
        try { auto text = read(p); replace_all(text, "[", " "); replace_all(text, "]", " ");
            std::istringstream values(text); std::string value;
            while (values >> value) if (value == "factory") { matches.push_back(str(p)); break; }
        } catch (const Error&) {}
    }
    return {{"supported", machine() == "x86_64" && matches.size() == 1}, {"interfaces", matches},
            {"note", "BIOS defaults are separate from Ubuntu reset and require hardware support."}};
}
void firmware_reset(const std::string& confirmation) {
    root_only(); need(confirmation == "RESET-BIOS-DEFAULTS", "Requires --confirm RESET-BIOS-DEFAULTS"); auto info = firmware_status();
    need(info.at("supported").get<bool>(), "No unambiguous documented factory BIOS reset interface");
    write(info["interfaces"][0].get<std::string>(), "factory\n");
    std::cout << "Firmware accepted the request; reboot is required. Verify defaults in firmware setup.\n";
}
}
