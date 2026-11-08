#include "reset.hpp"
#include "download.hpp"
#include <chrono>
#include <cerrno>
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
    auto os = distribution(read("/usr/lib/os-release"));
    auto model = fs::exists("/proc/device-tree/model") ? read("/proc/device-tree/model") : "";
    auto kind = select_platform(machine(), fs::exists("/sys/firmware/efi"), model, os.at("id"));
    std::string boot = kind == "raspi-arm64" ? "/boot/firmware" : "/boot/efi";
    if (kind != "raspi-arm64") {
        need(fs::is_regular_file(efi_directory(boot, os.at("id")) / "grub.cfg") && fs::is_regular_file(os.at("id") == "fedora" ? "/boot/grub2/grub.cfg" : "/boot/grub/grub.cfg"),
             "Target must use its distribution's standard GRUB + EFI vendor-directory layout");
    }
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
    return {{"platform", kind}, {"distro", os.at("id")}, {"version_id", os.at("version_id")}, {"root", root}, {"boot", fw}};
}
struct Storage {
    fs::path work; json fsinfo; json hw;
    explicit Storage(const fs::path& path) {
        auto p = fs::canonical(path); fsinfo = filesystem(p); hw = hardware();
        auto mount = recovery_mount(p, fsinfo, hw);
        work = mount / "ubuntu-reset";
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

void require_inside(const fs::path& tree, const fs::path& path) {
    auto base = fs::canonical(tree); auto actual = fs::weakly_canonical(path);
    auto b = base.begin(), a = actual.begin();
    for (; b != base.end(); ++b, ++a) need(a != actual.end() && *a == *b, "镜像路径通过符号链接越出了系统目录：" + path.string());
}
static void reference_paths(const fs::path& root, const fs::path& boot) {
    for (const auto& path : {"usr/lib/os-release", "usr/bin/bash", "etc/passwd", "etc/shadow", "etc/group", "etc/fstab", "var/lib/dpkg/status",
             "home", "usr/lib/modules", "boot", "usr/bin/sudo", "usr/bin/passwd", "usr/sbin/useradd", "usr/sbin/usermod",
             "etc", "var/lib/dbus", "var/lib/systemd", "etc/ssh", "usr/lib/ubuntu-reset", "etc/systemd/system", "var/lib/ubuntu-reset-firstboot-ready"}) require_inside(root, root / path);
    for (const auto& path : {"EFI", "current", "config.txt"}) require_inside(boot, boot / path);
}

static json reference(const fs::path& root, const fs::path& boot, const std::string& kind, bool prepared) {
    need(supported_platform(kind), "Unsupported platform");
    reference_paths(root, boot);
    need(fs::is_regular_file(root / "usr/lib/os-release"), "Reference is not an installed Debian/Ubuntu filesystem");
    auto os = distribution(read(root / "usr/lib/os-release"));
    need(kind != "raspi-arm64" || os.at("id") == "ubuntu", "Native Pi firmware adapter currently requires Ubuntu; Debian ARM64 uses UEFI+GRUB");
    for (const auto& name : {"etc/passwd", "etc/shadow", "etc/fstab"})
        need(fs::is_regular_file(root / name) && !fs::is_symlink(root / name), "Missing installed-system reference file: " + std::string(name));
    need(fs::is_regular_file(root / (os.at("id") == "fedora" ? "usr/lib/sysimage/rpm/rpmdb.sqlite" : "var/lib/dpkg/status")), "Missing installed package database");
    std::istringstream passwd(read(root / "etc/passwd")); std::string line; bool root_account = false;
    while (std::getline(passwd, line)) {
        std::istringstream fields(line); std::vector<std::string> f; std::string value;
        while (std::getline(fields, value, ':')) f.push_back(value);
        need(f.size() == 7 && std::regex_match(f[2], std::regex("[0-9]+")), "Invalid reference passwd");
        auto uid = std::stoul(f[2]);
        if (uid == 0) { need(f[0] == "root" && !root_account, "Reference contains an alternate or duplicate root account"); root_account = true; }
        need(!(uid >= 1000 && uid < 60000 && f[0] != "oem" && f[0] != "gnome-initial-setup"), "Reference contains a personal user: " + f[0]);
    }
    need(root_account, "Reference has no root system account");
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
    need(arch == (kind == "x86_64-uefi" ? 62U : 183U), "Reference CPU architecture does not match profile platform");
    if (kind == "raspi-arm64") {
        need(fs::is_regular_file(boot / "config.txt"), "Missing Pi firmware config.txt"); bool cmdline = false, modules = false;
        need(fs::is_regular_file(boot / "current/vmlinuz") && fs::is_regular_file(boot / "current/initrd.img"), "Missing standard Ubuntu Pi current/ kernel and initrd");
        for (const auto& e : fs::recursive_directory_iterator(boot)) if (e.path().filename() == "cmdline.txt" && e.is_regular_file()) cmdline = true;
        for (const auto& base : {root / "usr/lib/modules", root / "lib/modules"})
            if (fs::is_directory(base)) for (const auto& e : fs::directory_iterator(base)) if (str(e.path().filename()).find("raspi") != std::string::npos) modules = true;
        need(cmdline && modules, "Missing Pi command line or kernel modules");
    } else {
        auto efi = efi_directory(boot, os.at("id"));
        require_inside(boot, efi / "grub.cfg");
        auto suffix = kind == "arm64-uefi" ? "aa64" : "x64";
        need(fs::is_regular_file(root / (os.at("id") == "fedora" ? "boot/grub2/grub.cfg" : "boot/grub/grub.cfg")) && fs::is_regular_file(efi / "grub.cfg"), "Requires standard installed GRUB + distribution EFI pointer");
        need(fs::is_regular_file(efi / (std::string("grub") + suffix + ".efi")), "Missing architecture-specific GRUB EFI binary");
        bool kernel_set = false;
        if (fs::is_directory(root / "usr/lib/modules")) for (const auto& entry : fs::directory_iterator(root / "usr/lib/modules")) {
            require_inside(root, entry.path());
            auto version = entry.path().filename().string();
            for (const auto& file : {root / "boot" / ("vmlinuz-" + version), root / "boot" / ((os.at("id") == "fedora" ? "initramfs-" : "initrd.img-") + version + (os.at("id") == "fedora" ? ".img" : ""))}) require_inside(root, file);
            if (entry.is_directory() && fs::is_regular_file(root / "boot" / ("vmlinuz-" + version)) && fs::is_regular_file(root / "boot" / ((os.at("id") == "fedora" ? "initramfs-" : "initrd.img-") + version + (os.at("id") == "fedora" ? ".img" : "")))) kernel_set = true;
        }
        need(kernel_set, "UEFI reference needs a matching installed kernel, initrd and modules directory; bootability still requires a VM/hardware test");
        // An unsigned GRUB installation is valid when Secure Boot is disabled.
        // If shim is used, its presence/validity remains the trusted profile producer's responsibility.
        for (const auto& name : {"usr/bin/sudo", "usr/bin/passwd", "usr/sbin/useradd", "usr/sbin/usermod"})
            need(fs::is_regular_file(root / name), "UEFI first-user setup requires sudo/passwd account tools: " + std::string(name));
        need(std::regex_search(read(root / "etc/group"), std::regex(os.at("id") == "fedora" ? "(^|\n)wheel:[^:]*:[0-9]+:" : "(^|\n)sudo:[^:]*:[0-9]+:")), "Reference must contain its administrator group");
        if (prepared) need(fs::is_regular_file(root / "var/lib/ubuntu-reset-firstboot-ready") && fs::is_regular_file(root / "usr/lib/ubuntu-reset/firstboot.sh"), "UEFI profile is missing the initial-user setup flow");
    }
    if (prepared) {
        std::istringstream shadows(read(root / "etc/shadow")); std::string record; bool locked = false;
        while (std::getline(shadows, record)) if (record.starts_with("root:")) {
            need(!locked && std::regex_match(record, std::regex("root:[!*]{1,8}:.*")), "Recovery profile contains a factory root credential; prepare it again"); locked = true;
        }
        need(locked, "Recovery profile has no locked root account");
        if (kind != "raspi-arm64") {
            auto mask = root / "etc/systemd/system/systemd-firstboot.service";
            need(fs::is_symlink(mask) && fs::read_symlink(mask) == "/dev/null", "旧恢复资料可能卡在系统首次设置。请用新版重新准备镜像。");
            validate_grub_roots(read(root / (os.at("id") == "fedora" ? "boot/grub2/grub.cfg" : "boot/grub/grub.cfg")));
        }
        auto fstab = read(root / "etc/fstab");
        need(fstab.find("@ROOT_UUID@") != std::string::npos && fstab.find("@BOOT_UUID@") != std::string::npos, "Profile fstab lacks relocation placeholders");
        if (kind == "raspi-arm64") {
            for (const auto& e : fs::recursive_directory_iterator(boot)) if (e.path().filename() == "cmdline.txt" && e.is_regular_file()) {
                require_inside(boot, e.path()); need(read(e.path()).find("root=UUID=@ROOT_UUID@") != std::string::npos, "Pi boot command lacks relocation placeholder");
            }
        } else for (const auto& file : {root / (os.at("id") == "fedora" ? "boot/grub2/grub.cfg" : "boot/grub/grub.cfg"), efi_directory(boot, os.at("id")) / "grub.cfg"})
            need(read(file).find("@ROOT_UUID@") != std::string::npos, "GRUB profile lacks root relocation placeholder");
    }
    return {{"schema", 2}, {"platform", kind}, {"distro", os.at("id")}, {"version_id", os.at("version_id")}};
}
void validate_reference(const fs::path& root, const fs::path& boot, const std::string& kind, bool prepared) {
    reference(root, boot, kind, prepared);
}
static void replace_all(std::string& text, const std::string& from, const std::string& to) {
    need(!from.empty(), "Cannot replace empty text"); size_t i = 0;
    while ((i = text.find(from, i)) != std::string::npos) { text.replace(i, from.size(), to); i += to.size(); }
}
void make_profile(const fs::path& root, const fs::path& boot, const std::string& kind, const fs::path& dest, const json& provenance, const fs::path& firstboot_assets) {
    auto meta = reference(root, boot, kind, false);
    auto grubpath = meta.at("distro") == "fedora" ? "boot/grub2/grub.cfg" : "boot/grub/grub.cfg"; need(!fs::exists(dest), "Destination already exists");
    auto size = std::stoull(output({"du", "-sx", "--block-size=1", str(root)}));
    need(fs::space(dest.parent_path()).available > size * 2 + (1ULL << 30), "Need space for staging plus compressed recovery data");
    TempDir stage(dest.parent_path()); auto r = stage.path / "root", b = stage.path / "boot"; fs::create_directory(r); fs::create_directory(b);
    run({"rsync", "-aHAXx", "--numeric-ids", "--exclude=/dev/***", "--exclude=/proc/***", "--exclude=/sys/***", "--exclude=/run/***", "--exclude=/boot/firmware/***", "--exclude=/boot/efi/***", str(root) + "/", str(r) + "/"});
    run({"rsync", "-rt", str(boot) + "/", str(b) + "/"});
    reference_paths(r, b);
    for (const auto& path : {"root", "dev", "proc", "sys", "run", "boot/firmware", "boot/efi", "etc/fstab", "etc/systemd/system/multi-user.target.wants",
             "etc/systemd/system/display-manager.service.d", "usr/lib/ubuntu-reset/firstboot.sh", "etc/systemd/system/ubuntu-reset-firstboot.service",
             "etc/systemd/system/display-manager.service.d/ubuntu-reset-firstboot.conf"}) require_inside(r, r / path);
    // Root's personal data must not become part of an administrator-created profile.
    fs::remove_all(r / "root"); fs::create_directory(r / "root"); fs::permissions(r / "root", fs::perms::owner_all);
    for (const auto& name : {"etc/machine-id", "var/lib/dbus/machine-id"}) if (fs::exists(r / name) && !fs::is_symlink(r / name)) write(r / name, "");
    // Never transfer a factory root password or passwordless root into the restored system.
    std::istringstream shadows(read(r / "etc/shadow")); std::ostringstream locked_shadow; std::string shadow_line; bool locked_root = false;
    while (std::getline(shadows, shadow_line)) {
        if (shadow_line.starts_with("root:")) {
            need(!locked_root, "Duplicate root shadow record"); auto colon = shadow_line.find(':', 5);
            need(colon != std::string::npos, "Invalid root shadow record"); shadow_line.replace(5, colon-5, "!"); locked_root = true;
        }
        locked_shadow << shadow_line << '\n';
    }
    need(locked_root, "Reference has no root shadow record"); write(r / "etc/shadow", locked_shadow.str());
    fs::remove(r / "var/lib/systemd/random-seed");
    if (fs::is_directory(r / "etc/ssh")) for (const auto& e : fs::directory_iterator(r / "etc/ssh")) if (str(e.path().filename()).starts_with("ssh_host_") && e.is_regular_file()) fs::remove(e.path());
    auto boot_mount = kind == "raspi-arm64" ? "/boot/firmware" : "/boot/efi";
    std::istringstream lines(read(r / "etc/fstab")); std::string line, old_uuid, root_spec; bool hasroot = false;
    while (std::getline(lines, line)) {
        auto pos = line.find_first_not_of(" \t"); if (pos == std::string::npos || line[pos] == '#') continue;
        std::istringstream f(line); std::string spec, point, type; f >> spec >> point >> type;
        need(point == "/" || point == boot_mount, "Complex fstab/swap layouts are unsupported");
        if (point == "/") {
            need(type == "ext4", "Reference root must be ext4"); need(!hasroot, "Duplicate reference root mount"); hasroot = true; root_spec = spec;
            if (spec.starts_with("UUID=")) old_uuid = spec.substr(5);
            else if (kind != "raspi-arm64" && (spec.starts_with("PARTUUID=") || spec.starts_with("LABEL="))) {
                // Official installed images commonly use PARTUUID. Resolve it only from
                // the exact offline root mount, and verify that fstab names that partition.
                auto mounted = json::parse(output({"findmnt", "--json", "--target", root.string(), "-o", "TARGET,SOURCE,UUID"})).at("filesystems");
                need(mounted.size() == 1 && fs::canonical(mounted[0].at("target").get<std::string>()) == fs::canonical(root), "Mount the pristine root partition directly to resolve PARTUUID/LABEL safely");
                auto equal = spec.find('='); auto tag = spec.substr(0, equal);
                need(output({"blkid", "-s", tag, "-o", "value", mounted[0].at("source")}) == spec.substr(equal+1), "Reference fstab identifies a different root partition");
                old_uuid = mounted[0].at("uuid").get<std::string>(); need(valid_uuid(old_uuid), "Offline root filesystem has no usable UUID");
            }
        }
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
        need(valid_uuid(old_uuid), "UEFI reference must use a root UUID in fstab");
        for (const auto& p : {r / grubpath, efi_directory(b, meta.at("distro")) / "grub.cfg"}) {
            require_inside(p == r / grubpath ? r : b, p);
            need(!fs::is_symlink(p), "Unsafe GRUB config symlink"); auto text = read(p);
            need(text.find(old_uuid) != std::string::npos, "Standard GRUB configs must reference the root UUID; embedded/custom layouts are unsupported");
            if (p == r / grubpath) text = normalize_grub_roots(text, old_uuid, root_spec);
            replace_all(text, old_uuid, "@ROOT_UUID@"); write(p, text);
        }
        auto fallback = b / "EFI/BOOT/grub.cfg"; require_inside(b, fallback);
        if (fs::is_regular_file(fallback)) { auto text = read(fallback); replace_all(text, old_uuid, "@ROOT_UUID@"); write(fallback, text); }
        // PID 1's generic firstboot wizard can otherwise block sysinit before our account wizard.
        // Mask only that service in this private profile; machine-id still regenerates normally.
        fs::create_directories(r / "etc/systemd/system");
        auto generic_firstboot = r / "etc/systemd/system/systemd-firstboot.service";
        fs::remove(generic_firstboot); fs::create_symlink("/dev/null", generic_firstboot);
        // Both x86_64 and generic ARM64 UEFI use the local-console first-user wizard.
        fs::create_directories(r / "usr/lib/ubuntu-reset"); fs::create_directories(r / "etc/systemd/system/multi-user.target.wants");
        fs::copy_file(firstboot_assets / "firstboot.sh", r / "usr/lib/ubuntu-reset/firstboot.sh", fs::copy_options::overwrite_existing);
        fs::permissions(r / "usr/lib/ubuntu-reset/firstboot.sh", fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec | fs::perms::others_read | fs::perms::others_exec);
        fs::copy_file(firstboot_assets / "ubuntu-reset-firstboot.service", r / "etc/systemd/system/ubuntu-reset-firstboot.service", fs::copy_options::overwrite_existing);
        fs::create_directories(r / "etc/systemd/system/display-manager.service.d");
        write(r / "etc/systemd/system/display-manager.service.d/ubuntu-reset-firstboot.conf",
              "[Unit]\nRequires=ubuntu-reset-firstboot.service\nAfter=ubuntu-reset-firstboot.service\n");
        auto link = r / "etc/systemd/system/multi-user.target.wants/ubuntu-reset-firstboot.service";
        fs::remove(link); fs::create_symlink("../ubuntu-reset-firstboot.service", link);
        write(r / "var/lib/ubuntu-reset-firstboot-ready", "console-setup-v1\n");
    }
    if (fs::is_directory(r / "etc/cloud")) {
        require_inside(r, r / "etc/cloud/cloud-init.disabled"); write(r / "etc/cloud/cloud-init.disabled", "Disabled for local factory account setup\n");
    }
    if (meta.at("distro") == "ubuntu" && kind != "raspi-arm64" && fs::is_directory(r / "etc/netplan")) {
        require_inside(r, r / "etc/netplan/90-factory.yaml");
        write(r / "etc/netplan/90-factory.yaml", "network:\n  version: 2\n  ethernets:\n    factory:\n      match: {name: 'e*'}\n      dhcp4: true\n      optional: true\n");
        fs::permissions(r / "etc/netplan/90-factory.yaml", fs::perms::owner_read | fs::perms::owner_write);
    }
    if (meta.at("distro") == "fedora") {
        require_inside(r, r / "etc/kernel/cmdline"); write(r / "etc/kernel/cmdline", "root=UUID=@ROOT_UUID@ ro\n");
        write(r / ".autorelabel", "-F\n");
    }
    // Empty runtime mountpoint directories are needed for the next boot.
    for (const auto& dir : {"dev", "proc", "sys", "run", "boot/firmware", "boot/efi"}) fs::create_directories(r / dir);
    meta["provenance"] = provenance; meta["semantics"] = "initial system; all personal accounts/data removed";
    write(stage.path / "profile.json", meta.dump(2) + "\n");
    reference(r, b, kind, true);
    run({"mksquashfs", str(stage.path), str(dest), "-noappend", "-comp", "zstd", "-processors", "2", "-no-progress"});
}
static void register_profile(const fs::path& profile, const Storage& st, const json& provenance) {
    no_pending(); auto id = random_id(); auto final = st.work / ("factory-" + id + ".squashfs");
    fs::rename(profile, final); fs::permissions(final, fs::perms::owner_read | fs::perms::owner_write);
    json state{{"schema", 2}, {"distro", st.hw["distro"]}, {"version_id", st.hw["version_id"]}, {"id", id}, {"platform", st.hw["platform"]}, {"root_uuid", st.hw["root"]["uuid"]},
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
static void import_disk_image(const fs::path& image, const fs::path& profile, const fs::path& work, const std::string& platform, const std::string& distro_expected, const json& provenance) {
    Loop loop(image); run({"udevadm", "settle"});
    auto parts = json::parse(output({"lsblk", "--json", "--tree", "-o", "PATH,FSTYPE", loop.device}));
    std::vector<std::string> roots, boots, btrfs;
    for (const auto& p : parts.at("blockdevices")[0].value("children", json::array())) {
        if (p["fstype"] == "ext4") roots.push_back(p["path"]);
        if (p["fstype"] == "btrfs") btrfs.push_back(p["path"]);
        if (p["fstype"] == "vfat") boots.push_back(p["path"]);
    }
    need(boots.size() == 1 && ((roots.size() >= 1 && roots.size() <= 2 && btrfs.empty()) || (roots.empty() && btrfs.size() == 1)), "Source needs an installed ext4 or Fedora Btrfs system plus one FAT ESP");
    Mount boot(boots[0], "vfat", "ro,nosuid,nodev,noexec");
    TempDir merged(work); auto tree = merged.path / "root", esp = merged.path / "esp"; fs::create_directory(tree); fs::create_directory(esp);
    run({"rsync", "-rt", boot.temp.path.string()+"/", esp.string()+"/"});
    std::string root_uuid; bool found_root = false, found_boot = false;
    auto copy_tree = [&](const fs::path& source, const fs::path& dest) {
        fs::create_directories(dest); run({"rsync", "-aHAXx", "--numeric-ids", "--exclude=/dev/***", "--exclude=/proc/***", "--exclude=/sys/***", "--exclude=/run/***", source.string()+"/", dest.string()+"/"});
    };
    if (!btrfs.empty()) {
        Mount source(btrfs[0], "btrfs", "ro,subvolid=5,nosuid,nodev,noexec");
        need(fs::is_regular_file(source.temp.path / "root/usr/lib/os-release"), "Fedora Btrfs import requires the standard root/boot/home/var subvolumes");
        need(distribution(read(source.temp.path / "root/usr/lib/os-release")).at("id") == "fedora", "Btrfs source is not Fedora");
        copy_tree(source.temp.path / "root", tree);
        for (const auto& sub : {"boot", "home", "var"}) { need(fs::is_directory(source.temp.path / sub), "Missing Fedora image subvolume"); copy_tree(source.temp.path / sub, tree / sub); }
        root_uuid = output({"blkid", "-s", "UUID", "-o", "value", btrfs[0]}); found_root = found_boot = true;
    } else for (const auto& dev : roots) {
        Mount source(dev, "ext4", "ro,noload,nosuid,nodev,noexec");
        if (fs::is_regular_file(source.temp.path / "usr/lib/os-release")) {
            need(!found_root, "Multiple source roots"); copy_tree(source.temp.path, tree); found_root = true;
            root_uuid = output({"blkid", "-s", "UUID", "-o", "value", dev});
        }
    }
    need(found_root && valid_uuid(root_uuid), "No unambiguous installed root in source image");
    auto os = distribution(read(tree / "usr/lib/os-release"));
    need(os.at("id") == distro_expected, "所选镜像与当前系统发行版不同。");
    if (roots.size() == 2) for (const auto& dev : roots) {
        Mount source(dev, "ext4", "ro,noload,nosuid,nodev,noexec");
        if (fs::is_regular_file(source.temp.path / "usr/lib/os-release")) continue;
        need(!found_boot && fs::is_directory(source.temp.path / "grub"), "Extra source partition is not a standard Ubuntu /boot");
        copy_tree(source.temp.path, tree / "boot"); found_boot = true;
    }
    reference(tree, esp, platform, false);
    for (const auto& p : {"etc/default/grub", "etc/kernel/cmdline", "boot/grub/grub.cfg", "boot/grub2/grub.cfg", "dev", "proc", "sys"}) require_inside(tree, tree / p);
    // Normalize source-only split boot/subvolumes into the explicitly supported flat ext4 target.
    // Nothing is written to the currently installed root during this operation.
    auto distro = os.at("id").get<std::string>(); auto grubdir = distro == "fedora" ? "grub2" : "grub";
    write(tree / "etc/fstab", "UUID="+root_uuid+" / ext4 defaults 0 1\nUUID="+output({"blkid", "-s", "UUID", "-o", "value", boots[0]})+" /boot/efi vfat defaults 0 2\n");
    std::string menu = "set timeout=3\nsearch --no-floppy --fs-uuid --set=root "+root_uuid+"\n"; bool have_kernel = false;
    for (const auto& e : fs::directory_iterator(tree / "usr/lib/modules")) {
        auto version = e.path().filename().string(); need(std::regex_match(version, std::regex("[A-Za-z0-9._+-]+")), "Invalid source kernel version");
        if (!fs::is_regular_file(tree / "boot" / ("vmlinuz-"+version))) continue;
        auto initrd = (distro == "fedora" ? "initramfs-" : "initrd.img-") + version + (distro == "fedora" ? ".img" : "");
        need(fs::is_regular_file(tree / "boot" / initrd), "Missing source initramfs");
        menu += "menuentry 'Factory initial system' {\nlinux /boot/vmlinuz-"+version+" root=UUID="+root_uuid+" ro console=tty0 console=ttyS0,115200\ninitrd /boot/"+initrd+"\n}\n"; have_kernel = true;
    }
    need(have_kernel, "Source has no installed kernel"); fs::create_directories(tree / "boot" / grubdir);
    write(tree / "boot" / grubdir / "grub.cfg", menu);
    auto pointer = "search --fs-uuid --set=root "+root_uuid+"\nset prefix=($root)/boot/"+grubdir+"\nconfigfile $prefix/grub.cfg\n";
    write(efi_directory(esp, distro) / "grub.cfg", pointer);
    if (fs::is_regular_file(esp / "EFI/BOOT/grub.cfg")) write(esp / "EFI/BOOT/grub.cfg", pointer);
    if (distro == "fedora") {
        need(host_arch() == (platform == "x86_64-uefi" ? "amd64" : "arm64"), "Fedora initramfs conversion requires native source architecture");
        // Official generic dracut images contain Btrfs subvolume cmdlines. Rebuild from
        // the pristine staged userspace, never from this machine's personal configuration.
        fs::create_directories(tree / "dev"); fs::create_directories(tree / "proc");
        for (const auto& node : {"null", "zero", "random", "urandom"}) {
            auto minor = std::string(node) == "null" ? "3" : std::string(node) == "zero" ? "5" : std::string(node) == "random" ? "8" : "9";
            if (!fs::exists(tree / "dev" / node)) run({"mknod", "-m", "666", (tree / "dev" / node).string(), "c", "1", minor});
        }
        fs::create_symlink("/proc/self/fd", tree / "dev/fd");
        run({"mount", "-t", "proc", "-o", "nosuid,nodev,noexec", "proc", (tree / "proc").string()});
        try { for (const auto& e : fs::directory_iterator(tree / "usr/lib/modules")) {
            auto version = e.path().filename().string();
            if (fs::is_regular_file(tree / "boot" / ("vmlinuz-"+version))) run({"chroot", tree.string(), "/usr/bin/dracut", "--force", "--no-hostonly", "--no-hostonly-cmdline", "/boot/initramfs-"+version+".img", version});
        }
        } catch (...) { command({"umount", (tree / "proc").string()}, false); throw; }
        run({"umount", (tree / "proc").string()});
        write(tree / "etc/default/grub", "GRUB_ENABLE_BLSCFG=false\nGRUB_TIMEOUT=3\nGRUB_CMDLINE_LINUX=\"\"\n");
        write(tree / "etc/kernel/cmdline", "root=UUID="+root_uuid+" ro\n");
    }
    make_profile(tree, esp, platform, profile, provenance);
}
void make_profile_image(const fs::path& selected, const std::string& platform, const std::string& distro, const fs::path& out) {
    root_only(); need(supported_platform(platform) && platform != "raspi-arm64", "Image conversion needs a generic UEFI platform");
    need(!fs::exists(out) && fs::is_regular_file(selected) && !fs::is_symlink(selected), "Use a regular offline raw image and a new output path"); secure(out.parent_path());
    import_disk_image(selected, out, out.parent_path(), platform, distro, {{"type", "offline installed image conversion"}, {"sha256", sha256(selected)}});
}
static void check_profile(const fs::path& profile, const Storage& st) {
    Mount source(profile, "squashfs", "ro,loop,nosuid,nodev,noexec"); auto meta = json::parse(read(source.temp.path / "profile.json"));
    need((meta.at("schema") == 1 || meta.at("schema") == 2) && meta.at("platform") == st.hw["platform"], "Profile platform does not match target");
    auto actual = reference(source.temp.path / "root", source.temp.path / "boot", st.hw["platform"], true);
    need(actual.at("distro") == st.hw.at("distro"), "Recovery source must match the running distribution; cross-distribution replacement is unsupported");
    need(meta.at("schema") == 2 ? meta.at("distro") == actual.at("distro") : actual.at("distro") == "ubuntu", "Profile metadata does not match its root distribution");
    need(read(source.temp.path / "root/etc/fstab").find("@ROOT_UUID@") != std::string::npos, "Factory profile is not relocatable");
}
static void snapshot_local_file(const fs::path& from, const fs::path& to) {
    int in = open(from.c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC); need(in >= 0, "无法打开本地镜像文件");
    int out = -1;
    try {
        struct stat st{}; need(fstat(in, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0, "请选择普通镜像文件，不要选择磁盘设备或目录");
        need(static_cast<uint64_t>(st.st_size) <= (128ULL << 30), "本地镜像超过 128 GiB 限制");
        need(fs::space(to.parent_path()).available > static_cast<uint64_t>(st.st_size) + (1ULL << 30), "恢复分区空间不足，无法暂存所选镜像");
        out = open(to.c_str(), O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600); need(out >= 0, "无法暂存镜像");
        std::vector<char> buffer(1024 * 1024); uint64_t total = 0;
        for (;;) {
            auto n = ::read(in, buffer.data(), buffer.size()); if (n < 0 && errno == EINTR) continue;
            need(n >= 0, "读取镜像失败"); if (!n) break;
            total += static_cast<uint64_t>(n); need(total <= static_cast<uint64_t>(st.st_size), "复制期间镜像文件发生变化，请停止其他程序写入该文件后重试");
            ssize_t offset = 0;
            while (offset < n) { auto count = ::write(out, buffer.data() + offset, static_cast<size_t>(n - offset));
                if (count < 0 && errno == EINTR) continue;
                need(count > 0, "写入暂存镜像失败"); offset += count;
            }
        }
        need(total == static_cast<uint64_t>(st.st_size), "复制期间镜像文件大小发生变化，请重试");
        need(fsync(out) == 0, "镜像暂存写入失败"); close(out); out = -1; close(in);
    } catch (...) { close(in); if (out >= 0) close(out); throw; }
}
void prepare_image(const fs::path& selected, const fs::path& location, const std::string& expected_sha256, const std::string& expected_sha512) {
    root_only(); no_pending(); auto format = image_format(selected); Storage st(location); TempDir temp(st.work);
    auto snapshot = temp.path / "input", raw = temp.path / "disk.raw", profile = temp.path / "prepared.squashfs";
    need(expected_sha256.empty() || valid_sha(expected_sha256), "Invalid expected SHA-256");
    need(expected_sha512.empty() || std::regex_match(expected_sha512, std::regex("[0-9a-f]{128}")), "Invalid expected SHA-512");
    snapshot_local_file(selected, snapshot);
    need(expected_sha256.empty() || sha256(snapshot) == expected_sha256, "Official image changed before import (SHA-256 mismatch)");
    need(expected_sha512.empty() || sha512(snapshot) == expected_sha512, "Official image changed before import (SHA-512 mismatch)");
    json provenance{{"type", "local image"}, {"filename", selected.filename().string()}, {"sha256", sha256(snapshot)}, {"format", format},
                    {"authentication", "User-selected local file; computed hash is integrity tracking, not proof of publisher identity"}};
    if (!expected_sha256.empty()) provenance["expected_sha256"] = expected_sha256;
    if (!expected_sha512.empty()) provenance["expected_sha512"] = expected_sha512;
    if (!expected_sha256.empty() || !expected_sha512.empty()) provenance["snapshot_checksum_verified"] = true;
    if (format == "raw") {
        int fd = open(snapshot.c_str(), O_RDONLY | O_CLOEXEC); char magic[4]{}; auto count = ::read(fd, magic, 4); close(fd);
        if (count == 4 && std::string(magic,4) == std::string("QFI\xfb",4)) format = "qcow2";
    }
    if (format == "profile") {
        check_profile(snapshot, st); register_profile(snapshot, st, provenance); return;
    }
    if (format == "raw-xz") {
        auto listing = output({"xz", "--robot", "--list", snapshot.string()});
        std::istringstream lines(listing); std::string line; uint64_t expanded = 0;
        while (std::getline(lines, line)) if (line.starts_with("totals\t")) {
            std::istringstream cols(line); std::string a,b,c,d; cols >> a >> b >> c >> d >> expanded;
        }
        need(expanded && expanded < (128ULL << 30) && fs::space(st.work).available > expanded * 2 + (2ULL << 30), "镜像展开或生成恢复资料所需空间不足，或镜像过大");
        int fd = open(raw.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600); need(fd >= 0, "无法展开本地镜像");
        auto rc = command({"xz", "--decompress", "--stdout", snapshot.string()}, false, fd, false, false, false, expanded).code; close(fd); need(rc == 0, "镜像解压失败");
    } else if (format == "qcow2") {
        auto info = json::parse(output({"qemu-img", "info", "--output=json", "-f", "qcow2", snapshot.string()}));
        need(!info.contains("backing-filename") && !info.contains("full-backing-filename") && !info.value("encrypted", false), "请先导出独立、未加密的完整虚拟磁盘镜像，不能使用增量快照");
        auto data = info.value("format-specific", json::object()).value("data", json::object());
        need(!data.contains("data-file"), "不支持依赖外部数据文件的虚拟磁盘");
        uint64_t size = info.at("virtual-size");
        need(size < (128ULL << 30) && fs::space(st.work).available > size * 2 + (2ULL << 30), "虚拟磁盘展开空间不足，或镜像过大");
        run({"qemu-img", "convert", "-f", "qcow2", "-O", "raw", snapshot.string(), raw.string()});
    } else raw = snapshot;
    import_disk_image(raw, profile, st.work, st.hw.at("platform"), st.hw.at("distro"), provenance); register_profile(profile, st, provenance);
}
json detected_sources() {
    try { auto os = distribution(read("/usr/lib/os-release")); auto model = fs::exists("/proc/device-tree/model") ? read("/proc/device-tree/model") : ""; return source_capabilities(os.at("id"), os.at("version_id"), select_platform(machine(), fs::exists("/sys/firmware/efi"), model, os.at("id"))); }
    catch (const Error& e) { return {{"automatic_available", false}, {"reason", e.what()}, {"iso_automatic_install", false}}; }
}
void prepare_automatic(const fs::path& location) {
    root_only(); auto sources = detected_sources(); need(sources.at("automatic_available").get<bool>(), sources.at("reason"));
    std::cout << sources.at("reason").get<std::string>() << std::endl;
    Storage st(location); TempDir temporary(st.work);
    auto image = resolve_recovery(st.hw.at("distro"), st.hw.at("version_id"), st.hw.at("platform"), proxy);
    std::this_thread::sleep_for(std::chrono::seconds(6));
    auto path = download_official(image, temporary.path, [](const TransferProgress& p) { std::cerr << "\r" << p.percent() << "%  " << p.text() << "       " << std::flush; return true; }, proxy);
    prepare_image(path, location, image.checksum_algorithm == "sha256" ? image.checksum : "", image.checksum_algorithm == "sha512" ? image.checksum : "");
}
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
    auto rc = command({"xz", "--decompress", "--stdout", str(compressed)}, false, fd, false, false, false, expanded).code; close(fd); need(rc == 0, "Image decompression failed");
    json provenance{{"url", base + file}, {"sha256", sum}, {"verified", "Ubuntu CD image GPG signature"}};
    {
        Loop loop(image); run({"udevadm", "settle"}); auto partitions = json::parse(output({"lsblk", "--json", "--tree", "-o", "PATH,FSTYPE", loop.device}));
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
    check_profile(profile, st);
    register_profile(profile, st, {{"url", url}, {"sha256", sum}});
}
fs::path preflight(const json& s) {
    validate_state(s); auto hw = hardware();
    need(hw["platform"] == s["platform"] && hw.at("distro").get<std::string>() == state_distro(s) && hw["root"]["uuid"] == s["root_uuid"] && hw["boot"]["uuid"] == s["boot_uuid"], "Target layout/distribution changed; prepare a new recovery profile");
    auto mounts = json::parse(output({"findmnt", "--json", "--source", "UUID=" + s.at("store_uuid").get<std::string>(), "-o", "TARGET"}));
    need(mounts.at("filesystems").size() == 1, "Mount the prepared recovery partition first");
    auto path = fs::path(mounts["filesystems"][0].at("target").get<std::string>()) / s.at("file").get<std::string>();
    secure(path); need(sha256(path) == s.at("sha256").get<std::string>(), "Recovery profile is damaged or changed");
    // Re-audit old profiles before preparing an initramfs or writing the request marker.
    Mount source(path, "squashfs", "ro,loop,nosuid,nodev,noexec");
    auto actual = reference(source.temp.path / "root", source.temp.path / "boot", s.at("platform"), true);
    need(actual.at("distro").get<std::string>() == state_distro(s), "Recovery source distribution changed");
    return path;
}
void plan(const json& s) {
    root_only(); std::cout << "正在检查恢复磁盘与镜像完整性，这一步可能需要一些时间。\n" << std::flush;
    Mount source(preflight(s), "squashfs", "ro,loop,nosuid,nodev,noexec");
    std::cout << "ROOT: this preview includes removal of all personal users and files.\n" << std::flush;
    run({"rsync", "-aHAXxnci", "--numeric-ids", "--delete", "--exclude=/dev/***", "--exclude=/proc/***", "--exclude=/sys/***", "--exclude=/run/***", "--exclude=/boot/firmware/***", "--exclude=/boot/efi/***", "--exclude=/var/lib/ubuntu-reset/***", str(source.temp.path / "root") + "/", "/"});
    auto boot = s["platform"] == "raspi-arm64" ? "/boot/firmware/" : "/boot/efi/";
    std::cout << "BOOT:\n" << std::flush; run({"rsync", "-rtnci", "--delete", str(source.temp.path / "boot") + "/", boot});
    std::cout << "Preview only. UUID placeholders are replaced offline. Pending state is removed after successful restore.\n";
}
void arm(const json& s, const std::string& confirmation, bool reboot) {
    root_only(); need(confirmation == "ERASE-ALL-USERS", "Requires --confirm ERASE-ALL-USERS"); no_pending(); auto profile = preflight(s);
    if (state_distro(s) != "fedora") {
    need(fs::is_regular_file("/etc/initramfs-tools/hooks/ubuntu-reset") && fs::is_regular_file("/etc/initramfs-tools/scripts/local-bottom/ubuntu-reset"), "Install the recovery hooks first");
    secure("/etc/initramfs-tools/hooks/ubuntu-reset"); secure("/etc/initramfs-tools/scripts/local-bottom/ubuntu-reset");
    }
    atomic_write("/etc/ubuntu-reset/boot.conf", boot_config(s)); auto k = kernel();
    bool fedora = state_distro(s) == "fedora";
    if (fedora) run({"dracut", "--force", "--add", "linux-factory-reset", "--kver", k});
    else run({"update-initramfs", "-u", "-k", k});
    auto initrd = fs::path("/boot") / ((fedora ? "initramfs-" : "initrd.img-") + k + (fedora ? ".img" : ""));
    auto contents = output({fedora ? "lsinitrd" : "lsinitramfs", str(initrd)});
    need(contents.find(fedora ? "pre-pivot" : "scripts/local-bottom/ubuntu-reset") != std::string::npos && contents.find("conf/ubuntu-reset.conf") != std::string::npos
         && contents.find("usr/lib/ubuntu-reset/relocate.sh") != std::string::npos, "Recovery files are absent from initramfs");
    for (const auto& tool : {"awk", "grep", "sed", "rsync", "sha256sum", "readlink", "sync"})
        need(contents.find(std::string("usr/bin/") + tool) != std::string::npos, "Recovery utility missing from initramfs: " + std::string(tool));
    need(contents.find("usr/sbin/losetup") != std::string::npos, "Read-only loop utility is absent from initramfs");
    if (s["platform"] == "raspi-arm64") {
        auto config = read("/boot/firmware/config.txt");
        need(stock_pi_boot(config), "Unsupported/ambiguous Pi boot recipe");
        need(sha256("/boot/firmware/current/initrd.img") == sha256(initrd), "flash-kernel did not update the active Pi initrd");
    } else {
        if (!fedora) need(read("/boot/grub/grub.cfg").find("initrd.img-" + k) != std::string::npos, "Current kernel initrd is not listed in GRUB");
        install_rescue(s, profile, initrd, k);
    }
    atomic_write(REQUEST, s.at("id").get<std::string>() + "\n");
    std::cout << "Reset armed with independent UEFI rescue and temporary rollback backup. Cancel before reboot: sudo ubuntu-reset cancel\n" << std::flush;
    if (reboot) run({"systemctl", "reboot"});
}
void cancel() {
    root_only();
    if (fs::exists(STATE)) {
        auto state = load_state();
        if (state.at("platform") != "raspi-arm64") {
            auto profile = preflight(state); auto rescue = profile.parent_path() / ("rescue-" + state.at("id").get<std::string>());
            if (fs::exists(rescue)) {
                secure(rescue);
                need(!fs::exists(rescue / "phase"), "Restore has already started; use the independent rescue rollback entry");
                auto order = read(rescue / "original-order"); while (!order.empty() && order.back() == '\n') order.pop_back();
                need(std::regex_match(order, std::regex("[0-9A-Fa-f,]+")), "Invalid original BootOrder");
                run({"efibootmgr", "--bootorder", order});
                if (fs::exists(rescue / "entry")) { auto entry = read(rescue / "entry"); while (!entry.empty() && entry.back() == '\n') entry.pop_back(); need(std::regex_match(entry, std::regex("[0-9A-Fa-f]{4}")), "Invalid rescue entry"); run({"efibootmgr", "--bootnum", entry, "--delete-bootnum"}); }
                fs::remove_all("/boot/efi/EFI/LinuxFactoryReset"); fs::remove_all(rescue);
            }
        }
    }
    if (fs::exists(REQUEST) || fs::is_symlink(REQUEST)) { secure(REQUEST); fs::remove(REQUEST); sync(); }
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
            {"note", "BIOS defaults are separate from Debian/Ubuntu reset and require hardware support."}};
}
void firmware_reset(const std::string& confirmation) {
    root_only(); need(confirmation == "RESET-BIOS-DEFAULTS", "Requires --confirm RESET-BIOS-DEFAULTS"); auto info = firmware_status();
    need(info.at("supported").get<bool>(), "No unambiguous documented factory BIOS reset interface");
    write(info["interfaces"][0].get<std::string>(), "factory\n");
    std::cout << "Firmware accepted the request; reboot is required. Verify defaults in firmware setup.\n";
}
}
