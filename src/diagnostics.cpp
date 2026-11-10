#include "reset.hpp"
#include <cerrno>
#include <fcntl.h>
#include <regex>
#include <sstream>
#include <sys/utsname.h>
#include <unistd.h>

namespace uf {
static std::string identifier(const std::string& value) {
    return std::regex_match(value, std::regex("[A-Za-z0-9._+-]{1,128}")) ? value : "unknown";
}
static json exists_readonly(const fs::path& path) {
    std::error_code error;
    bool present = fs::exists(path, error);
    return error ? json(nullptr) : json(present);
}
static std::string filesystem_type(const fs::path& path) {
    try {
        auto data = json::parse(output({"findmnt", "--json", "--mountpoint", path.string(), "-o", "FSTYPE"}));
        return identifier(data.at("filesystems").at(0).at("fstype").get<std::string>());
    } catch (const std::exception&) { return "unknown"; }
}
json diagnostics(const fs::path& state_path, bool check_permissions) {
    // Explicit allowlist: never copy raw state, exceptions, UUIDs, usernames,
    // hostnames, device paths, home directories, URLs or log contents.
    json os{{"id", "unknown"}, {"version_id", "unknown"}};
    try {
        std::istringstream lines(read("/usr/lib/os-release")); std::string line;
        while (std::getline(lines, line)) {
            auto at = line.find('='); if (at == std::string::npos) continue;
            auto key = line.substr(0, at); if (key != "ID" && key != "VERSION_ID") continue;
            auto value = line.substr(at + 1);
            if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\'')))
                value = value.substr(1, value.size() - 2);
            os[key == "ID" ? "id" : "version_id"] = identifier(value);
        }
    } catch (const std::exception&) {}
    struct utsname u{}; bool have_uname = uname(&u) == 0;
    auto prepared = preparation_status(state_path, check_permissions);
    auto efi = exists_readonly("/sys/firmware/efi");
    auto boot = efi.is_boolean() && efi.get<bool>() ? "/boot/efi" : "/boot/firmware";
    json tools = json::object();
    for (const auto& name : {"curl", "rsync", "mksquashfs", "unsquashfs", "xz", "file", "findmnt", "lsblk", "blkid", "losetup", "efibootmgr", "grub-mkstandalone", "update-initramfs", "dracut", "zenity", "pkexec"}) {
        bool available = false;
        for (const auto& dir : {"/usr/bin/", "/usr/sbin/", "/bin/", "/sbin/"})
            if (access((std::string(dir) + name).c_str(), X_OK) == 0) available = true;
        tools[name] = available;
    }
    return {{"report_schema", 1}, {"application_version", VERSION}, {"os", os},
        {"architecture", have_uname ? identifier(u.machine) : "unknown"},
        {"kernel", have_uname ? identifier(u.release) : "unknown"}, {"uefi", efi},
        {"filesystems", {{"root", filesystem_type("/")}, {"boot", filesystem_type(boot)}}},
        {"preparation", {{"ready", prepared.at("ready")}, {"code", prepared.at("code")}}},
        {"reset_pending", exists_readonly(REQUEST)}, {"tools", tools}};
}
void export_diagnostics(const fs::path& path, const json& report) {
    auto text = report.dump(2) + "\n";
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    need(fd >= 0, "Cannot create diagnostic report. Choose a new filename in a writable directory; existing files are never overwritten.");
    try {
        size_t offset = 0;
        while (offset < text.size()) {
            auto count = ::write(fd, text.data() + offset, text.size() - offset);
            if (count < 0 && errno == EINTR) continue;
            need(count > 0, "Cannot write diagnostic report"); offset += static_cast<size_t>(count);
        }
        need(fsync(fd) == 0, "Cannot flush diagnostic report");
        int rc = close(fd); fd = -1; need(rc == 0, "Cannot close diagnostic report");
    } catch (...) { if (fd >= 0) close(fd); unlink(path.c_str()); throw; }
}
}
