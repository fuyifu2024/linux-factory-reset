#include "reset.hpp"
#include "download.hpp"
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

using namespace uf;
static int checks = 0;
static void check(bool ok, const std::string& label) { need(ok, "TEST FAILED: " + label); ++checks; }
template<class F> static void rejects(F action, const std::string& label) {
    bool failed = false; try { action(); } catch (const std::exception&) { failed = true; }
    check(failed, label);
}
static json state() {
    return {{"schema", 1}, {"platform", "raspi-arm64"}, {"root_uuid", "1111-2222"}, {"boot_uuid", "3333-4444"},
            {"store_uuid", "5555-6666"}, {"id", std::string(32, 'a')}, {"sha256", std::string(64, 'b')},
            {"file", "ubuntu-reset/factory-" + std::string(32, 'a') + ".squashfs"}};
}
int main() {
    try {
        TempDir tmp("/tmp");
        auto missing = tmp.path / "missing-state.json";
        check(preparation_status(missing, false).at("code") == "not-prepared", "preview without recovery data has a guided state");
        try { load_state(missing, false); check(false, "missing state rejected"); }
        catch (const Error& e) { check(std::string(e.what()).find("has not been prepared") != std::string::npos, "no low-level Cannot inspect error for missing state"); }
        check(!fs::exists(missing), "checking preview state never creates recovery data");
        write(tmp.path / "broken-state", "broken json");
        check(!preparation_status(tmp.path / "broken-state", false).at("ready").get<bool>(), "corrupt preparation metadata does not enable preview");
        write(tmp.path / "state.json", state().dump());
        check(preparation_status(tmp.path / "state.json", false).at("ready").get<bool>(), "valid preparation metadata enables preview");
        auto bounded_file = tmp.path / "bounded-output";
        int bounded_fd = open(bounded_file.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        check(bounded_fd >= 0, "bounded output fixture opened");
        auto bounded = command({"head", "-c", "128", "/dev/zero"}, false, bounded_fd, false, false, false, 64); close(bounded_fd);
        check(bounded.code != 0 && fs::file_size(bounded_file) <= 64, "child cannot exceed decompression output limit");
        auto diagnostic = command({"sh", "-c", "printf actual-child-error >&2; exit 7"}, true, -1, false, true);
        check(diagnostic.code == 7 && diagnostic.text == "actual-child-error", "GUI receives actual backend stderr with exit code");
        auto ui = json::parse(output({UFRESET_TEST_APP, "gui", "--preview"}));
        check(!ui.at("requires_url_in_normal_flow").get<bool>(), "no required custom URL in ordinary flow");
        check(ui.at("preview_requires_prepared_source").get<bool>(), "preview guard is advertised by the actual GUI");
        for (const auto& item : ui.at("actions")) check(item.get<std::string>().find("树莓派") == std::string::npos, "no board-specific main-menu action");
        check(image_format("official.img") == "raw" && image_format("vm.raw") == "raw", "local raw image classification");
        check(image_format("official.img.xz") == "raw-xz" && image_format("vm.qcow2") == "qcow2", "compressed and VM image classification");
        check(image_format("factory.squashfs") == "profile", "local prepared profile classification");
        rejects([] {image_format("installer.iso");}, "installer ISO not falsely treated as installed root");
        rejects([] {image_format("/dev/vda");}, "block device not treated as an image file");
        check(source_capabilities("debian", "13", "arm64-uefi").at("automatic_available").get<bool>(), "Debian 13 generic ARM64 source selected automatically");
        check(!source_capabilities("debian", "12", "arm64-uefi").at("automatic_available").get<bool>(), "unimplemented older source not presented as ready");
        check(source_capabilities("ubuntu", "26.04", "raspi-arm64").at("automatic_available").get<bool>(), "native source selected by detection, not menu branding");
        json volume{{"uuid", "abcd-5678"}, {"target", "/media/recovery"}, {"fstype", "ext4"}, {"options", "rw,relatime"}};
        json target{{"root", {{"uuid", "root-0001"}}}, {"boot", {{"uuid", "boot-0002"}}}};
        check(recovery_mount("/media/recovery/folder/subfolder", volume, target) == "/media/recovery", "folder chooser accepts a recovery-disk subdirectory");
        auto system_volume = volume; system_volume["uuid"] = "root-0001";
        rejects([&] {recovery_mount("/media/recovery/folder", system_volume, target);}, "system folder cannot masquerade as recovery storage");
        auto readonly = volume; readonly["options"] = "ro,errors=remount-ro";
        rejects([&] {recovery_mount("/media/recovery", readonly, target);}, "read-only recovery storage refused");
        check(image_format("SYSTEM.IMG.XZ") == "raw-xz", "uppercase image suffix accepted");
        TransferProgress progress{1024, 2048, 512};
        check(progress.percent() == 50 && progress.text().find("/s") != std::string::npos && progress.text().find("秒") != std::string::npos, "percent speed and ETA are visible");
        check(TransferProgress{1,0,0}.percent() == 0 && TransferProgress{10,10,1}.percent() == 99, "unknown sizes and verification keep progress below success");
        auto official = parse_sums(std::string(64,'c') + " *ubuntu-26.04-desktop-arm64.iso\n" + std::string(64,'a') + " *ubuntu-26.04.1-desktop-arm64.iso\n", "ubuntu", "26.04", "desktop", "arm64", "https://cdimage.ubuntu.com/releases/26.04/release/");
        check(official.filename == "ubuntu-26.04.1-desktop-arm64.iso" && official.arch == "arm64" && !official.describe().at("automatic_restore").get<bool>(), "official ISO metadata never enables reset");
        rejects([&] {parse_sums(std::string(64,'a') + " *../../escape.iso\n", "debian", "stable", "netinst", "amd64", "https://cdimage.debian.org/");}, "unsafe checksum filename rejected");
        json releases = json::array();
        for (const auto& version : {"45 Beta", "43", "44"}) releases.push_back({{"version", version}, {"arch", "aarch64"}, {"subvariant", "Workstation"}, {"link", "https://download.fedoraproject.org/pub/Fedora-" + std::string(version == std::string("45 Beta") ? "45" : version) + ".iso"}, {"sha256", std::string(64,'b')}, {"size", "1024"}});
        auto fedora = parse_fedora(releases.dump(), "arm64", "workstation");
        check(fedora.release == "44" && fedora.size == 1024, "Fedora selects newest stable ARM64, ignores beta");
        releases[2]["link"] = "https://unofficial.example/fedora.iso";
        rejects([&] {parse_fedora(releases.dump(), "arm64", "workstation");}, "Fedora metadata cannot replace the official source host");
        releases[2]["link"] = "https://download.fedoraproject.org/pub/Fedora-44.iso";
        releases.push_back(releases[2]);
        rejects([&] {parse_fedora(releases.dump(), "arm64", "workstation");}, "ambiguous same-version Fedora image rejected");
        // A cancellation before networking must delete the private .part and publish nothing.
        bool cancelled = false;
        try {download_official(official, tmp.path, [](const TransferProgress&) {return false;});} catch (const Cancelled&) {cancelled = true;}
        check(cancelled && !fs::exists(tmp.path / official.filename), "cancel does not publish an image");
        for (const auto& file : fs::directory_iterator(tmp.path)) check(file.path().filename().string().find(".part-") == std::string::npos, "cancel removes partial files");
        auto safe = tmp.path / "containment", outside = tmp.path / "outside"; fs::create_directory(safe); fs::create_directory(outside);
        fs::create_directory_symlink(outside, safe / "escape");
        rejects([&] {require_inside(safe, safe / "escape/new-file");}, "ancestor symlink cannot escape staging root");
        fs::create_directory(safe / "usr"); fs::create_directory(safe / "usr/lib"); fs::create_directory_symlink("usr/lib", safe / "lib");
        require_inside(safe, safe / "lib/new-file"); ++checks;
        write(tmp.path / "hash", "abc");
        check(sha256(tmp.path / "hash") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA256 known vector");
        check(sha512(tmp.path / "hash") == "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f", "SHA512 known vector");
        check(random_id().size() == 32 && random_id() != random_id(), "random identifiers");
        check(valid_uuid("01234567-89ab-cdef-0123-456789abcdef") && valid_uuid("ABCD-1234"), "UUID formats");
        check(!valid_uuid("a';reboot #") && !valid_uuid("../../dev/sda"), "UUID injection rejected");
        auto s = state(); validate_state(s); check(boot_config(s).find("UF_ROOT='1111-2222'") != std::string::npos, "boot config");
        check(state_distro(s) == "ubuntu", "legacy schema 1 distro compatibility");
        auto debian_state = s; debian_state["schema"] = 2; debian_state["distro"] = "debian"; debian_state["platform"] = "arm64-uefi";
        validate_state(debian_state);
        check(boot_config(debian_state).find("UF_DISTRO='debian'") != std::string::npos, "Debian ARM64 state reaches offline boot");
        auto invalid = debian_state; invalid["distro"] = "../evil";
        rejects([&] {boot_config(invalid);}, "EFI distributor injection rejected");
        invalid = debian_state; invalid["platform"] = "raspi-arm64";
        rejects([&] {validate_state(invalid);}, "Debian cannot use the Ubuntu native-Pi adapter");
        check(distribution("ID=debian\nVERSION_ID=\"13\"\n").at("id") == "debian", "Debian os-release");
        check(distribution("ID='ubuntu'\nVERSION_ID='24.04'\n").at("version_id") == "24.04", "quoted Ubuntu os-release");
        rejects([] {distribution("ID=mint\nID_LIKE=debian\n");}, "unvalidated derivative rejected");
        rejects([] {distribution("ID=debian\nID=ubuntu\n");}, "ambiguous distribution rejected");
        check(select_platform("aarch64", true, "Ampere server", "debian") == "arm64-uefi", "Debian ARM server detected by UEFI");
        check(select_platform("aarch64", true, "QEMU Virtual Machine", "ubuntu") == "arm64-uefi", "generic Ubuntu ARM VM");
        check(select_platform("aarch64", true, "Raspberry Pi 5", "debian") == "arm64-uefi", "UEFI Pi uses generic Debian path");
        check(select_platform("x86_64", true, "", "debian") == "x86_64-uefi", "Debian x86 UEFI");
        check(select_platform("aarch64", false, "Raspberry Pi 5", "ubuntu") == "raspi-arm64", "native Ubuntu Pi compatibility");
        rejects([] {select_platform("aarch64", false, "Generic U-Boot board", "debian");}, "non-UEFI board is not falsely supported");
        auto bad = s; bad["file"] = "../../etc/shadow"; rejects([&] {validate_state(bad);}, "profile path traversal");
        bad = s; bad["root_uuid"] = bad["store_uuid"]; rejects([&] {validate_state(bad);}, "overlapping filesystems");
        bad = s; bad["id"] = "';reboot"; rejects([&] {boot_config(bad);}, "shell injection in state");
        bad = s; bad["schema"] = 20; rejects([&] {validate_state(bad);}, "unknown schema");
        bad = s; bad["sha256"] = "ffff"; rejects([&] {validate_state(bad);}, "short digest");
        bad = s; bad["platform"] = "anything"; rejects([&] {validate_state(bad);}, "unknown platform");
        https("https://cdimage.ubuntu.com/releases/26.04.1/release/a.img.xz", true); ++checks;
        for (const auto& url : {"http://host/a", "https://user:secret@host/a", "https://host/a\n--output", "file:///etc/shadow"})
            rejects([&] {https(url);}, "unsafe download URL");
        rejects([] { https("https://other.example/a", true); }, "untrusted official host");
        set_download_proxy("http://127.0.0.1:7890"); ++checks;
        rejects([] {set_download_proxy("http://secret@127.0.0.1:7890");}, "proxy credentials rejected");
        const std::string recipe = "[all]\nos_prefix=current/\n[tryboot]\nos_prefix=new/\n[all]\nkernel=vmlinuz\ncmdline=cmdline.txt\ninitramfs initrd.img followkernel\n";
        check(stock_pi_boot(recipe), "stock Pi normal boot recipe");
        check(!stock_pi_boot(recipe + "[pi5]\nos_prefix=unknown/\n"), "Pi conditional boot override rejected");
        check(!stock_pi_boot(recipe + "include custom.txt\n"), "unknown Pi include rejected");
        std::string sum(64, 'd'); check(signed_checksum(sum + " *a.img.xz\n", "a.img.xz") == sum, "signed binary checksum format");
        check(signed_checksum(sum + "  a.img.xz\n", "a.img.xz") == sum, "signed text checksum format");
        rejects([&] {signed_checksum(sum + "  a\n" + sum + "  a\n", "a");}, "duplicate checksum");
        rejects([&] {signed_checksum(sum + "  different\n", "missing");}, "missing checksum");
        auto grub_normal = normalize_grub_roots("set root='hd0,gpt1'\nlinux /boot/vmlinuz root=PARTUUID=part-1234 ro\n", "root-1234", "PARTUUID=part-1234");
        check(grub_normal.find("set root='hd0,gpt1'") != std::string::npos && grub_normal.find("root=UUID=@ROOT_UUID@ ro") != std::string::npos, "kernel PARTUUID normalized without changing GRUB root variable");
        rejects([] {normalize_grub_roots("linux /vmlinuz root=UUID=foreign-1111\n", "root-1234", "PARTUUID=part-1234");}, "foreign kernel root refused");
        rejects([] {normalize_grub_roots("linux /vmlinuz root=UUID=root-1234 root=UUID=root-1234\n", "root-1234", "UUID=root-1234");}, "ambiguous kernel root refused");
        check(relocate("UUID=@ROOT_UUID@ /\nUUID=@BOOT_UUID@ /boot\n", "1111-2222", "3333-4444") == "UUID=1111-2222 /\nUUID=3333-4444 /boot\n", "UUID relocation");
        check(output({"printf", "%s", "$(touch SHOULD_NOT_EXIST);literal"}) == "$(touch SHOULD_NOT_EXIST);literal", "arguments never evaluated by shell");
        check(command({"false"}).code != 0, "command failure propagation");
        auto r = tmp.path / "reference", b = tmp.path / "firmware";
        for (const auto& name : {"usr/bin", "usr/lib/modules/7.0-raspi", "etc", "var/lib/dpkg", "var/lib/dbus", "var/lib/systemd", "etc/ssh", "home"}) fs::create_directories(r / name);
        fs::create_directory(b);
        fs::create_directory(b / "current");
        write(b / "current/vmlinuz", "synthetic test kernel"); write(b / "current/initrd.img", "synthetic test initrd");
        std::string elf(64, '\0'); elf.replace(0, 4, "\177ELF", 4); elf[4] = 2; elf[5] = 1; elf[18] = static_cast<char>(183);
        write(r / "usr/bin/bash", elf);
        write(r / "usr/lib/os-release", "ID=ubuntu\nVERSION_ID=26.04\n");
        write(r / "etc/passwd", "root:x:0:0:root:/root:/bin/bash\n"); write(r / "etc/shadow", "root:synthetic-unsafe-factory-credential:1:0:99999:7:::\n");
        write(r / "etc/fstab", "LABEL=writable / ext4 defaults 0 1\nLABEL=system-boot /boot/firmware vfat defaults 0 2\n");
        write(r / "var/lib/dpkg/status", "Package: base-files\nStatus: install ok installed\n");
        write(r / "etc/machine-id", "must-not-survive\n"); write(r / "etc/ssh/ssh_host_ed25519_key", "private");
        write(b / "config.txt", "[all]\n"); write(b / "cmdline.txt", "root=LABEL=writable rootwait quiet\n");
        auto image = tmp.path / "factory.squashfs";
        make_profile(r, b, "raspi-arm64", image, {{"type", "synthetic test fixture"}});
        check(fs::file_size(image) > 0, "compressed profile built");
        auto unpack = tmp.path / "unpack";
        run({"unsquashfs", "-no-progress", "-d", unpack.string(), image.string()});
        check(read(unpack / "root/etc/shadow").starts_with("root:!:"), "factory root password is locked before any first boot");
        check(read(unpack / "root/etc/machine-id").empty(), "machine identity removed");
        check(!fs::exists(unpack / "root/etc/ssh/ssh_host_ed25519_key"), "SSH host identity removed");
        check(read(unpack / "root/etc/fstab").find("@ROOT_UUID@") != std::string::npos, "fstab prepared for target");
        check(read(unpack / "boot/cmdline.txt").find("root=UUID=@ROOT_UUID@") != std::string::npos, "Pi boot pointer relocatable");
        check(fs::is_directory(unpack / "root/dev"), "runtime mountpoints present");
        write(r / "etc/passwd", "root:x:0:0:root:/root:/bin/bash\nalice:x:1000:1000:Alice:/home/alice:/bin/bash\n");
        rejects([&] {make_profile(r, b, "raspi-arm64", tmp.path / "bad.squashfs", {});}, "personal reference rejected");
        // Build real Squashfs profiles for both distros/architectures from synthetic,
        // non-executable file trees. No firmware or running root is modified.
        for (const auto& distro_version : {std::pair<std::string, std::string>{"ubuntu", "24.04"}, {"debian", "12"}, {"debian", "13"}}) {
            for (const std::string platform : {"arm64-uefi", "x86_64-uefi"}) {
                const auto& [distro, version] = distro_version;
                auto base = tmp.path / (distro + version + platform);
                auto ur = base / "reference", ub = base / "esp";
                for (const auto& dir : {"etc", "usr/bin", "usr/sbin", "usr/lib", "var/lib/dpkg", "boot/grub", "home"}) fs::create_directories(ur / dir);
                fs::create_directories(ur / "usr/lib/modules/fixture");
                write(ur / "boot/vmlinuz-fixture", "synthetic kernel, never executed");
                write(ur / "boot/initrd.img-fixture", "synthetic initrd, never executed");
                auto efi = efi_directory(ub, distro); fs::create_directories(efi);
                auto image_elf = elf; image_elf[18] = platform == "arm64-uefi" ? static_cast<char>(183) : static_cast<char>(62);
                write(ur / "usr/bin/bash", image_elf);
                write(ur / "usr/lib/os-release", "ID=" + distro + "\nVERSION_ID=\"" + version + "\"\n");
                write(ur / "etc/passwd", "root:x:0:0:root:/root:/bin/bash\n");
                write(ur / "etc/shadow", "root:!:1:0:99999:7:::\n");
                write(ur / "etc/group", "root:x:0:\nsudo:x:27:\n");
                write(ur / "etc/fstab", "UUID=01234567-abcd / ext4 defaults 0 1\nUUID=ABCD-1234 /boot/efi vfat defaults 0 2\n");
                write(ur / "var/lib/dpkg/status", "Package: base-files\nStatus: install ok installed\n");
                for (const auto& tool : {"usr/bin/sudo", "usr/bin/passwd", "usr/sbin/useradd", "usr/sbin/usermod"}) write(ur / tool, image_elf);
                write(ur / "boot/grub/grub.cfg", "linux /boot/vmlinuz root=UUID=01234567-abcd ro\n");
                write(efi / "grub.cfg", "search.fs_uuid 01234567-abcd root\nset prefix=($root)/boot/grub\n");
                auto loader = efi / (platform == "arm64-uefi" ? "grubaa64.efi" : "grubx64.efi");
                write(loader, "synthetic loader fixture; never executed");
                auto profile = base / "factory.squashfs";
                make_profile(ur, ub, platform, profile, {{"type", "synthetic distro fixture"}}, UFRESET_TEST_ASSETS);
                auto extracted = base / "extracted"; run({"unsquashfs", "-no-progress", "-d", extracted.string(), profile.string()});
                auto meta = json::parse(read(extracted / "profile.json"));
                check(meta.at("schema") == 2 && meta.at("distro") == distro && meta.at("platform") == platform, "distro + architecture profile identity");
                check(meta.at("version_id") == version, "reference distribution version retained");
                validate_reference(extracted / "root", extracted / "boot", platform, true); ++checks;
                check(read(efi_directory(extracted / "boot", distro) / "grub.cfg").find("@ROOT_UUID@") != std::string::npos, "correct distributor EFI pointer relocatable");
                check(read(extracted / "root/boot/grub/grub.cfg").find("@ROOT_UUID@") != std::string::npos, "UEFI root menu relocatable");
                check(fs::read_symlink(extracted / "root/etc/systemd/system/systemd-firstboot.service") == "/dev/null", "stock firstboot cannot block the project first-user flow");
                check(fs::is_symlink(extracted / "root/etc/systemd/system/multi-user.target.wants/ubuntu-reset-firstboot.service"), "generic UEFI first-user wizard enabled");
                check(read(ur / "etc/fstab").find("01234567-abcd") != std::string::npos, "original reference left unchanged");
                write(extracted / "root/etc/shadow", "root:synthetic-unlocked:1:0:99999:7:::\n");
                rejects([&] {validate_reference(extracted / "root", extracted / "boot", platform, true);}, "prebuilt profile cannot retain root credential");
                write(extracted / "root/etc/shadow", "root:!:1:0:99999:7:::\n");
                auto mask = extracted / "root/etc/systemd/system/systemd-firstboot.service"; fs::remove(mask);
                rejects([&] {validate_reference(extracted / "root", extracted / "boot", platform, true);}, "legacy profile with competing firstboot wizard rejected");
                fs::create_symlink("/dev/null", mask);
                write(extracted / "root/boot/grub/grub.cfg", "linux /vmlinuz root=PARTUUID=source-1234\n# @ROOT_UUID@ in a comment is insufficient\n");
                rejects([&] {validate_reference(extracted / "root", extracted / "boot", platform, true);}, "a comment placeholder does not make the kernel relocatable");
                write(extracted / "root/boot/grub/grub.cfg", "root=UUID=unrelocated\n");
                rejects([&] {validate_reference(extracted / "root", extracted / "boot", platform, true);}, "prepared GRUB without placeholder rejected");
                fs::create_directory_symlink(outside, ur / "etc/systemd");
                rejects([&] {make_profile(ur, ub, platform, base / "escape.squashfs", {}, UFRESET_TEST_ASSETS);}, "malicious directory symlink rejected before staging writes");
                fs::remove(ur / "etc/systemd");
                fs::remove(loader);
                rejects([&] {validate_reference(ur, ub, platform, false);}, "wrong or absent architecture EFI loader rejected");
                write(loader, "fixture");
                fs::remove(ur / "usr/bin/sudo");
                rejects([&] {validate_reference(ur, ub, platform, false);}, "minimal reference without sudo rejected before recovery");
            }
        }
        std::cout << checks << " C++ checks passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
