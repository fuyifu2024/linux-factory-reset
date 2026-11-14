#include "reset.hpp"
#include <array>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <poll.h>
#include <openssl/evp.h>
#include <regex>
#include <sstream>
#include <sys/random.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
namespace uf {
void need(bool ok, const std::string& message) { if (!ok) throw Error(message); }
std::string read(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    need(f.good(), "Cannot read " + path.string());
    std::ostringstream s; s << f.rdbuf();
    need(!f.bad(), "Read error: " + path.string()); return s.str();
}
void write(const fs::path& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    need(f.good(), "Cannot write " + path.string()); f << text; f.close();
    need(!f.fail(), "Write error: " + path.string());
}
static std::string hex(const unsigned char* data, size_t length) {
    constexpr auto chars = "0123456789abcdef";
    std::string value; value.reserve(length * 2);
    for (size_t i = 0; i < length; ++i) { value += chars[data[i] >> 4]; value += chars[data[i] & 15]; }
    return value;
}
static std::string digest(const fs::path& path, const EVP_MD* algorithm, const std::function<void(uint64_t)>& progress) {
    std::ifstream f(path, std::ios::binary); need(f.good(), "Cannot hash " + path.string());
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    need(ctx && EVP_DigestInit_ex(ctx.get(), algorithm, nullptr) == 1, "Checksum initialization failed");
    std::array<char, 1024 * 1024> buffer{};
    uint64_t processed = 0;
    while (f) { f.read(buffer.data(), buffer.size());
        processed += static_cast<uint64_t>(f.gcount()); if (progress) progress(processed);
        need(EVP_DigestUpdate(ctx.get(), buffer.data(), static_cast<size_t>(f.gcount())) == 1, "Checksum update failed"); }
    need(!f.bad(), "Hash read error");
    unsigned char bytes[EVP_MAX_MD_SIZE]; unsigned size = 0;
    need(EVP_DigestFinal_ex(ctx.get(), bytes, &size) == 1, "Checksum finalization failed");
    return hex(bytes, size);
}
std::string sha256(const fs::path& path, const std::function<void(uint64_t)>& progress) { return digest(path, EVP_sha256(), progress); }
std::string sha512(const fs::path& path, const std::function<void(uint64_t)>& progress) { return digest(path, EVP_sha512(), progress); }
Result command(const Args& args, bool capture, int stdout_fd, bool desktop, bool merge_errors, bool live_output, uint64_t output_limit,
               const std::function<void(const std::string&)>& observer) {
    need(!args.empty(), "Empty command");
    fs::path exe;
    if (args[0].starts_with("/")) exe = args[0];
    else for (const auto& dir : {"/usr/sbin", "/usr/bin", "/sbin", "/bin"}) {
        fs::path p = fs::path(dir) / args[0];
        if (access(p.c_str(), X_OK) == 0) { exe = p; break; }
    }
    need(!exe.empty(), "Missing dependency: " + args[0]);
    int pipes[2] = {-1, -1};
    if (capture) need(pipe2(pipes, O_CLOEXEC) == 0, "Cannot create command pipe");
    auto child = fork();
    if (child < 0) { if (capture) { close(pipes[0]); close(pipes[1]); } throw Error("Cannot fork command"); }
    if (child == 0) {
        if (capture) { close(pipes[0]); if (dup2(pipes[1], STDOUT_FILENO) < 0) _exit(126); close(pipes[1]); }
        else if (stdout_fd >= 0 && dup2(stdout_fd, STDOUT_FILENO) < 0) _exit(126);
        if (merge_errors && dup2(STDOUT_FILENO, STDERR_FILENO) < 0) _exit(126);
        if (output_limit) { struct rlimit size{static_cast<rlim_t>(output_limit), static_cast<rlim_t>(output_limit)}; if (setrlimit(RLIMIT_FSIZE, &size) != 0) _exit(126); }
        std::vector<char*> av;
        for (const auto& arg : args) av.push_back(const_cast<char*>(arg.c_str()));
        av.push_back(nullptr);
        char path[] = "PATH=/usr/sbin:/usr/bin:/sbin:/bin";
        char locale[] = "LC_ALL=C.UTF-8";
        char home[] = "HOME=/root";
        char* env[] = {path, locale, home, nullptr};
        execve(exe.c_str(), av.data(), desktop ? environ : env);
        _exit(127);
    }
    std::string text;
    if (capture) {
        close(pipes[1]); std::array<char, 8192> buf{};
        for (;;) {
            if (observer) {
                pollfd pending{pipes[0], POLLIN, 0}; int ready;
                do { ready = poll(&pending, 1, 250); } while (ready < 0 && errno == EINTR);
                // UI observers are advisory: a display failure must not interrupt the backend.
                try { observer(""); } catch (...) {}
                if (ready == 0) continue;
                if (ready < 0) break;
            }
            auto count = ::read(pipes[0], buf.data(), buf.size());
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) break;
            if (observer) { try { observer(std::string(buf.data(), static_cast<size_t>(count))); } catch (...) {} }
            if (live_output) { std::cout.write(buf.data(), count); std::cout.flush(); }
            if (text.size() < 64 * 1024 * 1024) text.append(buf.data(), static_cast<size_t>(count));
        }
        close(pipes[0]);
    }
    int status = 0; while (waitpid(child, &status, 0) < 0) { if (errno != EINTR) throw Error("Cannot wait for command"); }
    return {WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status), text};
}
std::string output(const Args& args) {
    auto result = command(args); need(result.code == 0, "Command failed: " + args[0]);
    while (!result.text.empty() && (result.text.back() == '\n' || result.text.back() == '\r')) result.text.pop_back();
    return result.text;
}
void run(const Args& args) { need(command(args, false).code == 0, "Command failed: " + args[0]); }
void root_only() { need(geteuid() == 0, "Administrator authentication is required (sudo)"); }
void secure(const fs::path& input) {
    auto path = fs::absolute(input).lexically_normal();
    for (;;) {
        struct stat st{}; need(lstat(path.c_str(), &st) == 0, "Cannot inspect " + path.string());
        need(!S_ISLNK(st.st_mode) && st.st_uid == 0 && !(st.st_mode & 0022), "Unsafe privileged path: " + path.string());
        if (path == path.root_path()) break;
        path = path.parent_path();
    }
}
void atomic_write(const fs::path& path, const std::string& text, unsigned mode) {
    fs::create_directories(path.parent_path()); secure(path.parent_path());
    if (fs::exists(path) || fs::is_symlink(path)) secure(path);
    std::string name = (path.parent_path() / ".new-XXXXXX").string();
    int fd = mkstemp(name.data()); need(fd >= 0, "Cannot create atomic output");
    try {
        size_t offset = 0;
        while (offset < text.size()) {
            auto n = ::write(fd, text.data() + offset, text.size() - offset);
            if (n < 0 && errno == EINTR) continue;
            need(n > 0, "Atomic write failed"); offset += static_cast<size_t>(n);
        }
        need(fchmod(fd, mode) == 0 && fsync(fd) == 0, "Cannot flush output");
        close(fd); fd = -1; fs::rename(name, path);
        int dir = open(path.parent_path().c_str(), O_DIRECTORY | O_RDONLY);
        need(dir >= 0, "Cannot open state directory"); int rc = fsync(dir); close(dir);
        need(rc == 0, "Cannot flush state directory");
    } catch (...) { if (fd >= 0) close(fd); unlink(name.c_str()); throw; }
}
std::string random_id() {
    unsigned char bytes[16]; need(getrandom(bytes, sizeof(bytes), 0) == sizeof(bytes), "Cannot generate profile ID");
    return hex(bytes, sizeof(bytes));
}
bool valid_uuid(const std::string& value) { return std::regex_match(value, std::regex("[A-Za-z0-9][A-Za-z0-9-]{3,63}")); }
bool valid_sha(const std::string& value) { return std::regex_match(value, std::regex("[0-9a-f]{64}")); }
bool supported_platform(const std::string& kind) {
    return kind == "raspi-arm64" || kind == "x86_64-uefi" || kind == "arm64-uefi";
}
json distribution(const std::string& text) {
    std::istringstream lines(text); std::map<std::string, std::string> values; std::string line;
    while (std::getline(lines, line)) {
        auto equal = line.find('='); if (equal == std::string::npos || line.starts_with("#")) continue;
        auto key = line.substr(0, equal), value = line.substr(equal + 1);
        if (!value.empty() && value.back() == '\r') value.pop_back();
        if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\''))) value = value.substr(1, value.size() - 2);
        need(!values.contains(key), "Duplicate os-release key: " + key); values[key] = value;
    }
    need(values["ID"] == "ubuntu" || values["ID"] == "debian" || values["ID"] == "fedora", "Supported target IDs: Debian, Ubuntu, Fedora");
    return {{"id", values["ID"]}, {"version_id", values["VERSION_ID"]}};
}
std::string select_platform(const std::string& arch, bool uefi, const std::string& model, const std::string& distro) {
    need(distro == "ubuntu" || distro == "debian" || distro == "fedora", "Unsupported target distribution");
    if ((arch == "aarch64" || arch == "arm64") && uefi) return "arm64-uefi";
    if (arch == "x86_64" && uefi) return "x86_64-uefi";
    if (arch == "aarch64" && distro == "ubuntu" && model.find("Raspberry Pi 5") != std::string::npos) return "raspi-arm64";
    throw Error("Requires ARM64/x86_64 UEFI+GRUB, or the Ubuntu Raspberry Pi 5 firmware adapter; native U-Boot/other board boot flows need a separate adapter");
}
std::string state_distro(const json& s) {
    if (s.at("schema") == 1) return "ubuntu"; // alpha.1 profile/state compatibility.
    auto id = s.at("distro").get<std::string>();
    need(id == "ubuntu" || id == "debian" || id == "fedora", "Invalid profile distribution"); return id;
}
fs::path efi_directory(const fs::path& boot, const std::string& distro) {
    need(distro == "ubuntu" || distro == "debian" || distro == "fedora", "Invalid EFI distributor");
    return boot / "EFI" / distro;
}
void exclusive_esp(const fs::path& boot, const std::string& distro) {
    for (const auto& entry : fs::directory_iterator(boot / "EFI")) {
        auto name = entry.path().filename().string();
        for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        need(name == "boot" || name == distro || name == "linuxfactoryreset",
             "Shared/OEM EFI partition is unsupported; recovery will not delete another OS or vendor's boot files");
    }
}
void validate_state(const json& s) {
    need(s.at("schema") == 1 || s.at("schema") == 2, "Unsupported state schema");
    auto kind = s.at("platform").get<std::string>();
    need(supported_platform(kind), "Unknown platform");
    need(s.at("schema") != 1 || kind != "arm64-uefi", "ARM64 UEFI requires schema 2");
    auto id_distro = state_distro(s);
    need(kind != "raspi-arm64" || id_distro == "ubuntu", "Native Pi adapter requires an Ubuntu profile");
    for (const auto& key : {"root_uuid", "boot_uuid", "store_uuid"}) need(valid_uuid(s.at(key)), "Invalid filesystem UUID");
    need(s.at("root_uuid") != s.at("boot_uuid") && s.at("root_uuid") != s.at("store_uuid") && s.at("boot_uuid") != s.at("store_uuid"), "Overlapping target and recovery filesystems");
    auto id = s.at("id").get<std::string>();
    need(std::regex_match(id, std::regex("[0-9a-f]{32}")), "Invalid profile ID");
    need(valid_sha(s.at("sha256")), "Invalid profile digest");
    need(s.at("file") == "ubuntu-reset/factory-" + id + ".squashfs", "Invalid recovery path");
}
json load_state(const fs::path& path, bool check_permissions) {
    need(fs::exists(path) || fs::is_symlink(path),
         "Recovery data has not been prepared. Select and prepare a local recovery image first. No reset is scheduled.");
    if (check_permissions) secure(path);
    auto s = json::parse(read(path)); validate_state(s); return s;
}
json preparation_status(const fs::path& path, bool check_permissions) {
    try {
        if (!fs::exists(path) && !fs::is_symlink(path))
            return {{"ready", false}, {"reason", "请先选择并准备恢复镜像，然后再查看变更。当前没有可比较的恢复资料。"}, {"code", "not-prepared"}};
        auto state = load_state(path, check_permissions); return {{"ready", true}, {"reason", "恢复资料已准备，可查看变更"}, {"code", "prepared"}};
    }
    catch (const std::exception& e) { return {{"ready", false}, {"reason", "恢复资料记录无法读取，请重新准备镜像。\n" + std::string(e.what())}, {"code", "invalid-state"}}; }
}
std::string image_format(const fs::path& file) {
    auto name = file.filename().string();
    for (auto& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (name.ends_with(".iso")) throw Error("这是安装 ISO，不能直接覆盖系统。ISO 自动安装流程尚未实现；当前请选择完整已安装系统的 IMG/RAW 镜像。");
    if (name.ends_with(".img.xz") || name.ends_with(".raw.xz")) return "raw-xz";
    if (name.ends_with(".img") || name.ends_with(".raw")) return "raw";
    if (name.ends_with(".qcow2")) return "qcow2";
    if (name.ends_with(".squashfs")) return "profile";
    throw Error("请选择 IMG、RAW、IMG.XZ、RAW.XZ、QCOW2 或本项目的恢复资料文件；不要选择虚拟磁盘设备。");
}
json source_capabilities(const std::string& distro, const std::string& version, const std::string& platform) {
    bool uefi = platform == "x86_64-uefi" || platform == "arm64-uefi";
    bool automatic = (distro == "ubuntu" && ((version == "26.04" && platform == "raspi-arm64") || (uefi && (version == "24.04" || version == "26.04")))) || (distro == "debian" && version == "13" && uefi) || (distro == "fedora" && (version == "44" || version == "45") && uefi);
    return {{"automatic_available", automatic}, {"distro", distro}, {"version", version}, {"platform", platform},
            {"automatic_release", automatic ? version : ""},
            {"reason", automatic ? (platform != "raspi-arm64" ? "自动获取官方基础/服务器镜像（无桌面），版本与当前系统一致，以官网 HTTPS 校验值验证。恢复后创建新管理员；云端账号初始化将被禁用。不是原桌面的历史副本。" : "可自动选择 Ubuntu 官方来源并验签，无需填写网址") : "当前设备还没有内置并验证的官方自动恢复源。请选择本地 IMG/RAW 等镜像；无需自建网址。"},
            {"iso_automatic_install", false}, {"local_formats", {"img", "raw", "img.xz", "raw.xz", "qcow2", "squashfs"}}};
}
fs::path recovery_mount(const fs::path& selected, const json& filesystem, const json& target) {
    need(filesystem.at("uuid") != target.at("root").at("uuid") && filesystem.at("uuid") != target.at("boot").at("uuid"),
         "你选择的是当前系统盘或启动盘中的文件夹。恢复资料需要存放在另一块恢复磁盘上；普通系统文件夹不能代替它。虚拟机请先添加第二块虚拟硬盘，格式化为 ext4 并挂载，再选择该盘里的文件夹。尚未修改系统或设置恢复标记。");
    need(filesystem.at("fstype") == "ext4", "所选位置不在 ext4 恢复磁盘上。请选择另一块已挂载的 ext4 磁盘；目前不会自动格式化磁盘。");
    auto options = filesystem.at("options").get<std::string>(); options.insert(0, 1, ','); options.push_back(',');
    need(options.find(",rw,") != std::string::npos, "所选恢复磁盘是只读的，请以可写方式挂载后重试。");
    auto mount = fs::path(filesystem.at("target").get<std::string>()).lexically_normal();
    auto location = fs::absolute(selected).lexically_normal();
    auto parent = location;
    while (parent != mount && parent != parent.root_path()) parent = parent.parent_path();
    need(parent == mount, "无法确认所选文件夹所属磁盘，请重新选择。");
    return mount;
}
std::string boot_config(const json& s) {
    validate_state(s); std::string text;
    for (const auto& [shell, key] : std::map<std::string, std::string>{{"UF_ID", "id"}, {"UF_SHA", "sha256"}, {"UF_STORE", "store_uuid"}, {"UF_ROOT", "root_uuid"}, {"UF_BOOT", "boot_uuid"}, {"UF_FILE", "file"}, {"UF_PLATFORM", "platform"}})
        text += shell + "='" + s.at(key).get<std::string>() + "'\n";
    text += "UF_DISTRO='" + state_distro(s) + "'\n";
    return text;
}
void https(const std::string& url, bool official) {
    need(std::regex_match(url, std::regex("https://[A-Za-z0-9.-]+(?::[0-9]+)?/[^\\r\\n\\t ]*")), "Only HTTPS URLs without embedded credentials are supported");
    if (official) need(url.starts_with("https://cdimage.ubuntu.com/"), "Official metadata must come from cdimage.ubuntu.com");
}
std::string signed_checksum(const std::string& text, const std::string& filename) {
    std::istringstream lines(text); std::string line, found;
    while (std::getline(lines, line)) {
        if (line.size() < 67) continue;
        auto name = line.substr(66); // GNU sha256sum: digest + space + mode character.
        if (name == filename) { need(found.empty() && valid_sha(line.substr(0, 64)), "Ambiguous signed checksum"); found = line.substr(0, 64); }
    }
    need(!found.empty(), "Image is absent from signed Ubuntu checksums"); return found;
}


void validate_grub_roots(const std::string& text) {
    std::istringstream lines(text); std::string line; bool kernel = false; const std::regex arg("\\broot=([^ \\t]+)");
    while (std::getline(lines, line)) {
        std::istringstream fields(line); std::string token; fields >> token;
        if (token == "linux" || token == "linuxefi" || token == "linux16") {
            std::smatch match; need(std::regex_search(line, match, arg) && match[1] == "UUID=@ROOT_UUID@", "Recovery kernel still references a source device; prepare a new profile");
            need(!std::regex_search(match.suffix().str(), arg), "Ambiguous recovery kernel root arguments"); kernel = true;
        }
    }
    need(kernel, "Recovery profile has no supported Linux kernel entry");
}

std::string normalize_grub_roots(const std::string& text, const std::string& source_uuid, const std::string& source_spec) {
    need(valid_uuid(source_uuid), "Invalid source root UUID");
    std::istringstream lines(text); std::ostringstream result; std::string line; bool kernel = false;
    const std::regex arg("\\broot=([^ \\t]+)");
    while (std::getline(lines, line)) {
        std::istringstream fields(line); std::string token; fields >> token;
        if (token == "linux" || token == "linuxefi" || token == "linux16") {
            std::smatch match; need(std::regex_search(line, match, arg), "Kernel entry has no root argument");
            auto value = match[1].str(); need(value == source_spec || value == "UUID=" + source_uuid, "GRUB kernel entry refers to another root filesystem");
            need(!std::regex_search(match.suffix().str(), arg), "Ambiguous multiple kernel root arguments");
            line = std::regex_replace(line, arg, "root=UUID=@ROOT_UUID@"); kernel = true;
        }
        result << line << '\n';
    }
    need(kernel, "No supported GRUB Linux kernel entry found"); return result.str();
}

std::string relocate(const std::string& input, const std::string& root, const std::string& boot) {
    need(valid_uuid(root) && valid_uuid(boot), "Invalid relocation UUID");
    auto text = input;
    for (auto [from, to] : {std::pair<std::string, std::string>{"@ROOT_UUID@", root}, {"@BOOT_UUID@", boot}}) {
        size_t pos = 0; while ((pos = text.find(from, pos)) != std::string::npos) { text.replace(pos, from.size(), to); pos += to.size(); }
    }
    return text;
}
bool stock_pi_boot(const std::string& config) {
    std::istringstream lines(config); std::string line, section = "all";
    std::map<std::string, std::string> boot;
    while (std::getline(lines, line)) {
        auto comment = line.find('#'); if (comment != std::string::npos) line.resize(comment);
        auto first = line.find_first_not_of(" \t\r"); if (first == std::string::npos) continue;
        line = line.substr(first, line.find_last_not_of(" \t\r") - first + 1);
        if (line.starts_with("[")) { section = line; continue; }
        if (line.starts_with("include ")) return false;
        if (section != "all" && section != "[all]" && section != "[pi5]") continue;
        if (line.starts_with("initramfs ")) boot["initramfs"] = line;
        auto equal = line.find('='); if (equal != std::string::npos) boot[line.substr(0, equal)] = line.substr(equal + 1);
    }
    return boot["os_prefix"] == "current/" && boot["kernel"] == "vmlinuz" && boot["cmdline"] == "cmdline.txt"
           && boot["initramfs"] == "initramfs initrd.img followkernel" && !boot.contains("auto_initramfs");
}
TempDir::TempDir(const fs::path& parent) {
    auto pattern = (parent / "ubuntu-reset-XXXXXX").string();
    need(mkdtemp(pattern.data()) != nullptr, "Cannot create workspace in " + parent.string()); path = pattern;
}
TempDir::~TempDir() { if (!path.empty()) { std::error_code e; fs::remove_all(path, e); } }
Mount::Mount(const fs::path& device, const std::string& type, const std::string& options) : temp("/run") {
    auto flags = type == "vfat" ? options + ",iocharset=ascii,utf8=1,codepage=437" : options;
    run({"mount", "-t", type, "-o", flags, device.string(), temp.path.string()});
}
Mount::~Mount() {
    // If unmount fails, don't recurse into the still-mounted reference tree.
    try { if (command({"umount", temp.path.string()}, false).code != 0) temp.path.clear(); }
    catch (...) { temp.path.clear(); }
}
}
