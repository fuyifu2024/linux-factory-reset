#include "reset.hpp"
#include <iostream>
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
        write(tmp.path / "hash", "abc");
        check(sha256(tmp.path / "hash") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA256 known vector");
        check(random_id().size() == 32 && random_id() != random_id(), "random identifiers");
        check(valid_uuid("01234567-89ab-cdef-0123-456789abcdef") && valid_uuid("ABCD-1234"), "UUID formats");
        check(!valid_uuid("a';reboot #") && !valid_uuid("../../dev/sda"), "UUID injection rejected");
        auto s = state(); validate_state(s); check(boot_config(s).find("UF_ROOT='1111-2222'") != std::string::npos, "boot config");
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
        write(r / "etc/passwd", "root:x:0:0:root:/root:/bin/bash\n"); write(r / "etc/shadow", "root:!:1:0:99999:7:::\n");
        write(r / "etc/fstab", "LABEL=writable / ext4 defaults 0 1\nLABEL=system-boot /boot/firmware vfat defaults 0 2\n");
        write(r / "var/lib/dpkg/status", "Package: base-files\nStatus: install ok installed\n");
        write(r / "etc/machine-id", "must-not-survive\n"); write(r / "etc/ssh/ssh_host_ed25519_key", "private");
        write(b / "config.txt", "[all]\n"); write(b / "cmdline.txt", "root=LABEL=writable rootwait quiet\n");
        auto image = tmp.path / "factory.squashfs";
        make_profile(r, b, "raspi-arm64", image, {{"type", "synthetic test fixture"}});
        check(fs::file_size(image) > 0, "compressed profile built");
        auto unpack = tmp.path / "unpack";
        run({"unsquashfs", "-no-progress", "-d", unpack.string(), image.string()});
        check(read(unpack / "root/etc/machine-id").empty(), "machine identity removed");
        check(!fs::exists(unpack / "root/etc/ssh/ssh_host_ed25519_key"), "SSH host identity removed");
        check(read(unpack / "root/etc/fstab").find("@ROOT_UUID@") != std::string::npos, "fstab prepared for target");
        check(read(unpack / "boot/cmdline.txt").find("root=UUID=@ROOT_UUID@") != std::string::npos, "Pi boot pointer relocatable");
        check(fs::is_directory(unpack / "root/dev"), "runtime mountpoints present");
        write(r / "etc/passwd", "root:x:0:0:root:/root:/bin/bash\nalice:x:1000:1000:Alice:/home/alice:/bin/bash\n");
        rejects([&] {make_profile(r, b, "raspi-arm64", tmp.path / "bad.squashfs", {});}, "personal reference rejected");
        std::cout << checks << " C++ checks passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
