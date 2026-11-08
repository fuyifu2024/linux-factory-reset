#include "reset.hpp"
#include <regex>
#include <sstream>
#include <unistd.h>

namespace uf {
// Independent copies on the recovery volume survive replacement of /boot and /usr.
// Secure Boot is deliberately not disabled; unsigned rescue loaders are rejected.
void install_rescue(const json& s, const fs::path& profile, const fs::path& initrd, const std::string& kernel) {
    if (s.at("platform") == "raspi-arm64") return;
    for (const auto& e : fs::directory_iterator("/sys/firmware/efi/efivars")) {
        if (e.path().filename().string().starts_with("SecureBoot-")) {
            auto value = read(e.path()); need(value.size() >= 5 && value[4] == 0,
                "Independent rescue currently requires Secure Boot disabled. No firmware setting has been changed.");
        }
    }
    auto report = output({"efibootmgr"}); std::smatch match;
    need(std::regex_search(report, match, std::regex("BootOrder: ([0-9A-Fa-f,]+)")), "Cannot record original UEFI BootOrder");
    auto order = match[1].str();
    auto recovery = profile.parent_path() / ("rescue-" + s.at("id").get<std::string>());
    need(!fs::exists(recovery), "Rescue entry already exists; cancel the previous arm operation first");
    fs::create_directory(recovery); fs::permissions(recovery, fs::perms::owner_all);
    auto esp = fs::path("/boot/efi/EFI/LinuxFactoryReset");
    need(!fs::exists(esp), "An existing LinuxFactoryReset EFI directory needs cancellation/inspection first");
    auto source_kernel = fs::path("/boot") / ("vmlinuz-" + kernel);
    need(fs::is_regular_file(source_kernel), "Running kernel image unavailable for independent rescue");
    need(fs::space(recovery).available > fs::file_size(source_kernel) + fs::file_size(initrd) + (1ULL << 30), "Recovery partition too small for rescue kernel/initrd");
    fs::copy_file(source_kernel, recovery / "vmlinuz"); fs::copy_file(initrd, recovery / "initrd");
    atomic_write(recovery / "original-order", order + "\n");
    auto id = s.at("id").get<std::string>(); auto rel = "/ubuntu-reset/rescue-" + id;
    auto suffix = s.at("platform") == "x86_64-uefi" ? "x64" : "aa64";
    auto loader = "grub" + std::string(suffix) + ".efi";
    std::string cfg = "search --no-floppy --fs-uuid --set=store " + s.at("store_uuid").get<std::string>() + "\n";
    cfg += "if [ ! -f ($store)" + rel + "/active ]; then\nsearch --fs-uuid --set=normal " + s.at("boot_uuid").get<std::string>() +
           "\nchainloader ($normal)/EFI/" + state_distro(s) + "/" + loader + "\nboot\nfi\nset timeout=10\nset default=0\n";
    for (bool rollback : {false, true}) {
        cfg += "menuentry 'Linux Factory Reset: " + std::string(rollback ? "roll back previous system" : "recover / continue") + "' {\n";
        cfg += "linux ($store)" + rel + "/vmlinuz root=UUID=" + s.at("root_uuid").get<std::string>() + " ro lfr.rescue=" + id + (rollback ? " lfr.rollback=1" : "") + " console=tty0 console=ttyS0,115200\n";
        cfg += "initrd ($store)" + rel + "/initrd\n}\n";
    }
    atomic_write(recovery / "grub.cfg", cfg);
    fs::create_directories(esp);
    auto target = s.at("platform") == "x86_64-uefi" ? "x86_64-efi" : "arm64-efi";
    run({"grub-mkstandalone", "-O", target, "--modules=part_gpt part_msdos ext2 fat linux search search_fs_uuid normal configfile chain", "-o", (esp / loader).string(), "boot/grub/grub.cfg=" + (recovery / "grub.cfg").string()});
    auto bootdev = output({"findmnt", "-nro", "SOURCE", "/boot/efi"});
    auto disk = output({"lsblk", "-dnro", "PKNAME", bootdev}); auto number = read(fs::path("/sys/class/block") / fs::path(bootdev).filename() / "partition");
    while (!number.empty() && number.back() == '\n') number.pop_back();
    need(std::regex_match(disk, std::regex("[A-Za-z0-9._-]+")) && std::regex_match(number, std::regex("[0-9]+")), "Cannot identify ESP disk/partition");
    auto before = output({"efibootmgr"});
    run({"efibootmgr", "--create-only", "--disk", "/dev/" + disk, "--part", number, "--label", "Linux Factory Reset " + id.substr(0,8), "--loader", "\\EFI\\LinuxFactoryReset\\" + loader});
    auto after = output({"efibootmgr"}); std::istringstream lines(after); std::string line, entry;
    while (std::getline(lines, line)) if (line.find("Linux Factory Reset " + id.substr(0,8)) != std::string::npos) {
        need(line.size() > 8 && line.starts_with("Boot") && before.find(line.substr(0,8)) == std::string::npos && entry.empty(), "Ambiguous rescue boot entry"); entry = line.substr(4,4);
    }
    need(std::regex_match(entry, std::regex("[0-9A-Fa-f]{4}")), "Rescue entry was not created");
    atomic_write(recovery / "entry", entry + "\n");
    // Activate only after kernel, initrd, loader and metadata have reached storage.
    atomic_write(recovery / "active", id + "\n"); sync();
    run({"efibootmgr", "--bootorder", entry + "," + order});
}
}
