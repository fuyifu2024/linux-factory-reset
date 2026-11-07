#include "reset.hpp"
#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <memory>
#include <openssl/evp.h>
#include <regex>
#include <sstream>
#include <sys/random.h>
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
std::string sha256(const fs::path& path) {
    std::ifstream f(path, std::ios::binary); need(f.good(), "Cannot hash " + path.string());
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    need(ctx && EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) == 1, "SHA-256 initialization failed");
    std::array<char, 1024 * 1024> buffer{};
    while (f) { f.read(buffer.data(), buffer.size());
        need(EVP_DigestUpdate(ctx.get(), buffer.data(), static_cast<size_t>(f.gcount())) == 1, "SHA-256 update failed"); }
    need(!f.bad(), "Hash read error");
    unsigned char bytes[EVP_MAX_MD_SIZE]; unsigned size = 0;
    need(EVP_DigestFinal_ex(ctx.get(), bytes, &size) == 1, "SHA-256 finalization failed");
    return hex(bytes, size);
}
Result command(const Args& args, bool capture, int stdout_fd, bool desktop) {
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
            auto count = ::read(pipes[0], buf.data(), buf.size());
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) break;
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
void validate_state(const json& s) {
    need(s.at("schema") == 1, "Unsupported state schema");
    need(s.at("platform") == "raspi-arm64" || s.at("platform") == "x86_64-uefi", "Unknown platform");
    for (const auto& key : {"root_uuid", "boot_uuid", "store_uuid"}) need(valid_uuid(s.at(key)), "Invalid filesystem UUID");
    need(s.at("root_uuid") != s.at("boot_uuid") && s.at("root_uuid") != s.at("store_uuid") && s.at("boot_uuid") != s.at("store_uuid"), "Overlapping target and recovery filesystems");
    auto id = s.at("id").get<std::string>();
    need(std::regex_match(id, std::regex("[0-9a-f]{32}")), "Invalid profile ID");
    need(valid_sha(s.at("sha256")), "Invalid profile digest");
    need(s.at("file") == "ubuntu-reset/factory-" + id + ".squashfs", "Invalid recovery path");
}
json load_state() { secure(STATE); auto s = json::parse(read(STATE)); validate_state(s); return s; }
std::string boot_config(const json& s) {
    validate_state(s); std::string text;
    for (const auto& [shell, key] : std::map<std::string, std::string>{{"UF_ID", "id"}, {"UF_SHA", "sha256"}, {"UF_STORE", "store_uuid"}, {"UF_ROOT", "root_uuid"}, {"UF_BOOT", "boot_uuid"}, {"UF_FILE", "file"}, {"UF_PLATFORM", "platform"}})
        text += shell + "='" + s.at(key).get<std::string>() + "'\n";
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
    run({"mount", "-t", type, "-o", options, device.string(), temp.path.string()});
}
Mount::~Mount() {
    // If unmount fails, don't recurse into the still-mounted reference tree.
    try { if (command({"umount", temp.path.string()}, false).code != 0) temp.path.clear(); }
    catch (...) { temp.path.clear(); }
}
}
