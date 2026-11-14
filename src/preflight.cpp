#include "reset.hpp"
#include "download.hpp"
#include <algorithm>
#include <regex>
#include <sstream>
#include <sys/statvfs.h>
#include <unistd.h>

namespace uf {
namespace { constexpr uint64_t GiB = 1ULL << 30, MiB = 1ULL << 20; }
std::string secure_boot_state(const fs::path& variables) {
    try {
        auto data = read(variables / "SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c");
        if (data.size() != 5) return "unknown";
        return data[4] == 0 ? "disabled" : data[4] == 1 ? "enabled" : "unknown";
    } catch (const std::exception&) { return "unknown"; }
}
json summarize_environment(json checks, bool storage_requested) {
    size_t failures = 0, unknown = 0; bool storage_ok = false;
    for (const auto& check : checks) {
        auto state = check.at("status").get<std::string>();
        if (state == "fail") ++failures;
        if (state == "unknown") ++unknown;
        if (check.at("id") == "storage" && state == "pass") storage_ok = true;
    }
    bool compatible = failures == 0 && unknown == 0;
    return {{"schema", 2}, {"application_version", VERSION}, {"read_only", true}, {"compatible", compatible},
        {"storage_checked", storage_requested && storage_ok}, {"restoration_verified", false},
        {"failure_count", failures}, {"unknown_count", unknown}, {"checks", checks},
        {"code", compatible ? "checks-passed" : "checks-blocked"},
        {"reason", compatible ? "准备前检查通过；镜像内容和实际引导写入仍需后续验证。" : "准备前检查未通过，请处理以下阻碍及无法确认的项目。"},
        {"note", "只读检查不会下载镜像、创建恢复目录、设置恢复标记或修改固件。空间为初步估算，不包含镜像导入/展开暂存；后续仍按实际镜像检查。"}};
}
std::string environment_text(const json& report) {
    std::ostringstream text; text << "准备前检查（只读）\n\n" << report.at("reason").get<std::string>() << "\n\n";
    for (const auto& check : report.at("checks")) {
        auto status = check.at("status").get<std::string>();
        auto label = status == "pass" ? "通过" : status == "fail" ? "阻碍" : status == "unknown" ? "无法确认" : "未检查";
        text << '[' << label << "] " << check.at("label").get<std::string>() << "：" << check.at("detail").get<std::string>() << '\n';
    }
    text << '\n' << report.at("note").get<std::string>();
    if (geteuid() != 0) text << "\n读取权限受限时，可用 sudo linux-factory-reset check --storage /恢复盘挂载点 只读重试。";
    return text.str();
}
json check_environment(const fs::path& storage) {
    json checks = json::array(), hw, root_mount, boot_mount; std::string distro, boot = "/boot/efi";
    auto add = [&](const std::string& id, const std::string& label, auto action) {
        std::string status = "pass", detail;
        try { detail = action(); }
        catch (const Error& e) { status = "fail"; detail = e.what(); }
        catch (const std::exception& e) { status = "unknown"; detail = e.what(); }
        checks.push_back({{"id", id}, {"label", label}, {"status", status}, {"detail", detail}});
    };
    auto mount_info = [](const fs::path& path) {
        auto rows = json::parse(output({"findmnt", "--json", "--target", path.string(), "-o", "TARGET,SOURCE,FSTYPE,UUID,OPTIONS"})).at("filesystems");
        need(rows.size() == 1, "无法确认挂载身份"); return rows.at(0);
    };
    add("distribution", "发行版", [&] { auto os = distribution(read("/usr/lib/os-release")); distro = os.at("id").get<std::string>(); return distro + " " + os.at("version_id").get<std::string>(); });
    bool uefi = fs::is_directory("/sys/firmware/efi");
    try { if (!uefi && host_arch() == "arm64" && distro == "ubuntu" && read("/proc/device-tree/model").find("Raspberry Pi 5") != std::string::npos) boot = "/boot/firmware"; } catch (const std::exception&) {}
    add("root", "根分区", [&] {
        root_mount = mount_info("/");
        need(root_mount.at("target") == "/" && root_mount.at("fstype") == "ext4", "需要平铺 ext4 根分区；Btrfs、独立子卷不支持");
        need(root_mount.at("uuid").is_string() && valid_uuid(root_mount.at("uuid")), "根分区缺少可用 UUID");
        need(output({"lsblk", "-dnro", "TYPE", root_mount.at("source")}) == "part", "根分区必须是普通分区，不能是 LVM、加密或 RAID"); return std::string("ext4 普通分区");
    });
    add("boot", "启动分区", [&] {
        boot_mount = mount_info(boot);
        need(boot_mount.at("target") == boot && boot_mount.at("fstype") == "vfat", "需要已挂载的独立 FAT 启动分区");
        need(boot_mount.at("uuid").is_string() && valid_uuid(boot_mount.at("uuid")), "启动分区缺少可用 UUID");
        need(output({"lsblk", "-dnro", "TYPE", boot_mount.at("source")}) == "part", "启动分区必须是普通分区"); return boot;
    });
    add("layout", "完整布局与引导规则", [&] { hw = hardware(); return hw.at("platform").get<std::string>(); });
    add("pending", "待执行恢复", [&] { need(!fs::exists(REQUEST) && !fs::is_symlink(REQUEST), "已有待执行恢复，请先取消后再准备资料"); return std::string("没有待执行恢复"); });
    add("tools", "必需工具", [&] {
        Args tools{"curl", "rsync", "mksquashfs", "unsquashfs", "xz", "findmnt", "lsblk", "blkid", "losetup", "mount", "umount", "du", "qemu-img"};
        if (distro == "fedora") { tools.push_back("dracut"); tools.push_back("lsinitrd"); }
        else { tools.push_back("update-initramfs"); tools.push_back("lsinitramfs"); }
        if (uefi) { tools.push_back("efibootmgr"); tools.push_back(distro == "fedora" ? "grub2-mkstandalone" : "grub-mkstandalone"); }
        if (boot == "/boot/firmware") { tools.push_back("gpgv"); tools.push_back("flash-kernel"); }
        std::string missing;
        for (const auto& tool : tools) {
            bool found = false;
            for (const auto& path : {"/usr/bin/", "/usr/sbin/", "/bin/", "/sbin/"}) if (access((std::string(path) + tool).c_str(), X_OK) == 0) found = true;
            if (!found) missing += (missing.empty() ? "" : ", ") + tool;
        }
        need(missing.empty(), "缺少工具，请修复安装包依赖：" + missing); return std::string("工具已找到；实际运行仍在后续步骤验证");
    });
    if (uefi) {
        fs::path variables = "/sys/firmware/efi/efivars";
        add("secure_boot", "Secure Boot", [&] {
            auto state = secure_boot_state(variables);
            if (state == "unknown") throw std::runtime_error("状态缺失、不可读或格式异常；无法确认，未按关闭处理");
            need(state == "disabled", "当前启用；此版本的独立救援要求已关闭 Secure Boot，程序不会自动修改固件"); return std::string("已关闭");
        });
        add("efi_variables", "UEFI 变量访问", [&] {
            struct statvfs info{};
            if (statvfs(variables.c_str(), &info) != 0) throw std::runtime_error("无法读取 UEFI 变量挂载状态");
            need(!(info.f_flag & ST_RDONLY), "UEFI 变量文件系统是只读挂载");
            if (access(variables.c_str(), W_OK) != 0) throw std::runtime_error("UEFI 变量目录无写入权限；以管理员身份只读检查");
            auto order = output({"efibootmgr"}); need(std::regex_search(order, std::regex("BootOrder: [0-9A-Fa-f,]+")), "无法读取原 UEFI 启动顺序");
            return std::string("挂载可写且启动顺序可读；未试写变量，不保证固件剩余容量");
        });
    } else checks.push_back({{"id", "secure_boot"}, {"label", "UEFI 专用检查"}, {"status", "not-checked"}, {"detail", "未使用 UEFI；平台是否受支持由布局检查判定"}});
    add("root_space", "系统盘工作空间", [&] { auto free = fs::space("/").available; need(free >= 512 * MiB, "系统盘可用空间不足 512 MiB，无法可靠生成恢复启动环境"); return std::to_string(free / MiB) + " MiB 可用"; });
    add("boot_space", "启动盘工作空间", [&] {
        need(!boot_mount.is_null() && boot_mount.at("target") == boot, "无法确认独立启动盘，未借用根分区空间作为结果");
        auto free = fs::space(boot).available; need(free >= 64 * MiB, "启动盘可用空间不足 64 MiB"); return std::to_string(free / MiB) + " MiB 可用";
    });
    uint64_t free_bytes = 0, estimate = 0;
    if (!storage.empty()) add("storage", "恢复盘与初步空间估算", [&] {
        auto selected = fs::canonical(storage); need(fs::is_directory(selected), "请选择已挂载恢复盘内的目录");
        auto info = mount_info(selected);
        need(!root_mount.is_null() && !boot_mount.is_null(), "先解决系统盘和启动盘身份问题");
        recovery_mount(selected, info, {{"root", root_mount}, {"boot", boot_mount}});
        free_bytes = fs::space(selected).available; estimate = 2 * GiB;
        if (uefi) for (const fs::path& path : {fs::path("/"), fs::path(boot)}) {
            auto space = fs::space(path); auto used = space.capacity - space.free;
            need(used <= UINT64_MAX - estimate, "空间估算溢出，无法确认容量"); estimate += used;
        }
        need(free_bytes >= estimate, "恢复盘剩余 " + std::to_string(free_bytes / MiB) + " MiB，初步至少需要 " + std::to_string(estimate / MiB) + " MiB；请释放空间或更换恢复盘");
        return "可用 " + std::to_string(free_bytes / MiB) + " MiB；初步预留 " + std::to_string(estimate / MiB) + " MiB，另需镜像导入暂存空间";
    });
    else checks.push_back({{"id", "storage"}, {"label", "恢复盘"}, {"status", "not-checked"}, {"detail", "未指定恢复盘；可加 --storage 检查"}});
    auto report = summarize_environment(checks, !storage.empty());
    if (!hw.is_null()) report["target"] = {{"distro", hw.at("distro")}, {"version_id", hw.at("version_id")}, {"platform", hw.at("platform")}};
    if (!storage.empty()) { report["recovery_free_bytes"] = free_bytes; report["recovery_required_bytes_estimate"] = estimate; }
    return report;
}
void require_environment(const fs::path& storage) {
    operation_stage("checking"); auto report = check_environment(storage);
    need(report.at("compatible").get<bool>(), environment_text(report));
}
}
