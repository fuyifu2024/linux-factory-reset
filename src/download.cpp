#include "download.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <curl/curl.h>
#include <fcntl.h>
#include <iomanip>
#include <memory>
#include <regex>
#include <sstream>
#include <thread>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

namespace uf {
namespace {
constexpr uint64_t image_limit = 16ULL << 30;
struct Curl {
    CURL* value;
    Curl() { static const int init = [] { need(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK, "Cannot initialize HTTPS"); return 0; }(); (void)init;
        value = curl_easy_init(); need(value, "Cannot create HTTPS request"); }
    ~Curl() { curl_easy_cleanup(value); }
};
struct Transfer {
    int fd = -1; std::string metadata, failure; uint64_t written = 0, limit = 0;
    Progress callback; bool cancelled = false;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now(), last = start;
};
size_t receive(char* bytes, size_t size, size_t count, void* opaque) noexcept {
    auto& t = *static_cast<Transfer*>(opaque);
    try {
        if (size && count > SIZE_MAX / size) { t.failure = "Response size overflow"; return 0; }
        size_t n = size * count;
        if (n > t.limit - t.written) { t.failure = "官方响应超出大小上限"; return 0; }
        if (t.fd >= 0) {
            size_t off = 0;
            while (off < n) { auto rc = ::write(t.fd, bytes + off, n - off); if (rc < 0 && errno == EINTR) continue;
                if (rc <= 0) { t.failure = "写入失败，请检查下载磁盘空间"; return 0; } off += static_cast<size_t>(rc); }
        } else t.metadata.append(bytes, n);
        t.written += n; return n;
    } catch (...) { t.failure = "Cannot receive HTTPS data"; return 0; }
}
int report(void* opaque, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) noexcept {
    auto& t = *static_cast<Transfer*>(opaque);
    if (!t.callback) return 0;
    try {
        auto clock = std::chrono::steady_clock::now();
        // Also poll during DNS/connection and stalled transfers, allowing Cancel to work.
        if (clock - t.last < std::chrono::milliseconds(120) && now != total) return 0;
        t.last = clock; double seconds = std::chrono::duration<double>(clock - t.start).count();
        TransferProgress p{static_cast<uint64_t>(std::max<curl_off_t>(now, 0)), static_cast<uint64_t>(std::max<curl_off_t>(total, 0)),
                           seconds > 0 ? static_cast<double>(now) / seconds : 0};
        if (!t.callback(p)) { t.cancelled = true; return 1; } return 0;
    } catch (...) { t.failure = "Progress display failed"; return 1; }
}
void request(const std::string& url, Transfer& t, const std::string& proxy) {
    https(url); Curl curl; auto* c = curl.value;
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "https"); curl_easy_setopt(c, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L); curl_easy_setopt(c, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 30L); curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L); curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 60L);
    curl_easy_setopt(c, CURLOPT_FAILONERROR, 1L); curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "linux-factory-reset/0.3");
    curl_easy_setopt(c, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(t.limit));
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, receive); curl_easy_setopt(c, CURLOPT_WRITEDATA, &t);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L); curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, report); curl_easy_setopt(c, CURLOPT_XFERINFODATA, &t);
    // Optional explicit local proxy, otherwise libcurl follows the user's environment.
    if (!proxy.empty()) { set_download_proxy(proxy); curl_easy_setopt(c, CURLOPT_PROXY, proxy.c_str()); }
    std::array<char, CURL_ERROR_SIZE> error{}; curl_easy_setopt(c, CURLOPT_ERRORBUFFER, error.data());
    auto code = curl_easy_perform(c);
    if (t.cancelled) throw Cancelled();
    need(t.failure.empty(), t.failure);
    need(code == CURLE_OK, "HTTPS 下载失败：" + std::string(error[0] ? error.data() : curl_easy_strerror(code)));
}
std::string human(double count) {
    constexpr const char* units[]{"B", "KiB", "MiB", "GiB"}; size_t n = 0;
    while (count >= 1024 && n < 3) { count /= 1024; ++n; }
    std::ostringstream out; out << std::fixed << std::setprecision(n ? 1 : 0) << count << ' ' << units[n]; return out.str();
}
void validate(const OfficialImage& i) {
    need(i.distro == "ubuntu" || i.distro == "debian" || i.distro == "fedora", "Unsupported official distribution");
    need(i.arch == "amd64" || i.arch == "arm64", "Unsupported image architecture");
    need(std::regex_match(i.filename, std::regex("[A-Za-z0-9][A-Za-z0-9.+_-]*\\.(iso|qcow2|img\\.xz)")), "Unsafe official image filename");
    need((i.checksum_algorithm == "sha256" && valid_sha(i.checksum)) || (i.checksum_algorithm == "sha512" && std::regex_match(i.checksum, std::regex("[0-9a-f]{128}"))), "官网未提供有效校验值，已停止下载"); https(i.url);
    const auto allowed = i.distro == "ubuntu" ? (i.url.starts_with("https://releases.ubuntu.com/") || i.url.starts_with("https://cdimage.ubuntu.com/")) :
        i.distro == "debian" ? (i.url.starts_with("https://cdimage.debian.org/") || i.url.starts_with("https://cloud.debian.org/images/cloud/")) : i.url.starts_with("https://download.fedoraproject.org/");
    need(allowed, "Image is outside the selected distribution's official source");
    need(i.size <= image_limit, "Official image exceeds 16 GiB limit");
}
}
int TransferProgress::percent() const { return total ? static_cast<int>(std::min(99.0, 100.0 * static_cast<double>(downloaded) / static_cast<double>(total))) : 0; }
std::string TransferProgress::text() const {
    std::ostringstream out; out << human(static_cast<double>(downloaded)) << " / " << (total ? human(static_cast<double>(total)) : "大小未知")
        << "  |  " << human(bytes_per_second) << "/s";
    if (total > downloaded && bytes_per_second > 0) out << "  |  约剩 " << static_cast<uint64_t>(std::min(8640000.0, static_cast<double>(total-downloaded)/bytes_per_second)) << " 秒";
    return out.str();
}
json OfficialImage::describe() const {
    return {{"distro", distro}, {"release", release}, {"edition", edition}, {"arch", arch}, {"filename", filename}, {"url", url},
        {checksum_algorithm, checksum}, {"size", size}, {"trust", "Checksum from official HTTPS metadata; detached PGP signature not checked by ISO downloader"},
        {"format", format}, {"automatic_restore", automatic_restore}, {"signature_verified", signature_verified}};
}
std::string fetch_metadata(const std::string& url, const std::string& proxy) {
    Transfer t; t.limit = 4ULL << 20; request(url, t, proxy); return t.metadata;
}
OfficialImage parse_sums(const std::string& text, const std::string& distro, const std::string& release,
                         const std::string& edition, const std::string& arch, const std::string& base) {
    need(arch == "amd64" || arch == "arm64", "Select amd64 or arm64");
    need(distro == "debian" || distro == "ubuntu", "Unsupported checksum catalog");
    need(edition == "netinst" || edition == "desktop" || edition == "server", "Unsupported image edition");
    const std::string pattern = distro == "debian" ? "debian-[0-9]+(\\.[0-9]+)*-" + arch + "-netinst\\.iso" :
        std::string("ubuntu-[0-9]+\\.[0-9]+(\\.[0-9]+)*-") + (edition == "server" ? "live-server" : "desktop") + "-" + arch + "\\.iso";
    std::istringstream lines(text); std::string line; OfficialImage found; std::vector<int> newest;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < 67 || line[64] != ' ' || (line[65] != ' ' && line[65] != '*')) continue;
        auto filename = line.substr(66);
        if (!std::regex_match(filename, std::regex(pattern))) continue;
        auto start = filename.find('-') + 1; auto version = filename.substr(start, filename.find('-', start) - start);
        if (distro == "ubuntu" && version != release && !version.starts_with(release + ".")) continue;
        std::vector<int> key; std::istringstream parts(version); std::string number;
        while (std::getline(parts, number, '.')) { need(number.size() <= 4, "Invalid release version"); key.push_back(std::stoi(number)); }
        if (!found.filename.empty() && key < newest) continue;
        need(found.filename.empty() || key != newest, "官网校验清单含重复版本，无法安全选择"); newest = key;
        found = {distro, release, edition, arch, filename, base + filename, line.substr(0,64), 0}; validate(found);
    }
    need(!found.filename.empty(), "官网暂未提供所选版本/架构的镜像，请选择其他版本。没有使用第三方来源。"); return found;
}
OfficialImage parse_fedora(const std::string& text, const std::string& arch, const std::string& edition) {
    need(arch == "amd64" || arch == "arm64", "Select amd64 or arm64");
    need(edition == "workstation" || edition == "server", "Select workstation or server");
    auto entries = json::parse(text); need(entries.is_array(), "Invalid Fedora release metadata"); OfficialImage selected; int newest = 0;
    for (const auto& row : entries) {
        auto v = row.value("version", ""); if (!std::regex_match(v, std::regex("[0-9]{1,3}"))) continue; // Never silently select beta.
        if (row.value("arch", "") != (arch == "amd64" ? "x86_64" : "aarch64")) continue;
        if (row.value("subvariant", "") != (edition == "workstation" ? "Workstation" : "Server")) continue;
        auto url = row.value("link", ""); if (!url.ends_with(".iso") || (edition == "server" && url.find("dvd") == std::string::npos)) continue;
        int version = std::stoi(v); if (version < newest) continue;
        auto filename = url.substr(url.find_last_of('/') + 1);
        OfficialImage i{"fedora", v, edition, arch, filename, url, row.value("sha256", ""), 0};
        if (row.contains("size")) i.size = row.at("size").is_string() ? std::stoull(row.at("size").get<std::string>()) : row.at("size").get<uint64_t>();
        validate(i); need(version != newest || selected.filename.empty(), "Ambiguous Fedora release metadata"); selected = i; newest = version;
    }
    need(!selected.filename.empty(), "Fedora 官网没有所选架构的稳定版 ISO"); return selected;
}
OfficialImage resolve_image(const std::string& distro, const std::string& release, const std::string& edition, const std::string& arch, const std::string& proxy) {
    need(arch == "amd64" || arch == "arm64", "Select amd64 or arm64");
    if (distro == "fedora") { need(release == "stable", "Fedora catalog selects the latest stable release"); return parse_fedora(fetch_metadata("https://fedoraproject.org/releases.json", proxy), arch, edition); }
    std::string base;
    if (distro == "ubuntu") {
        need(release == "24.04" || release == "26.04", "Select Ubuntu 24.04 or 26.04");
        need(edition == "desktop" || edition == "server", "Select desktop or server");
        base = (arch == "amd64" ? "https://releases.ubuntu.com/" : "https://cdimage.ubuntu.com/releases/") + release + (arch == "amd64" ? "/" : "/release/");
    } else { need(distro == "debian" && release == "stable" && edition == "netinst", "Select Debian stable netinst"); base = "https://cdimage.debian.org/debian-cd/current/" + arch + "/iso-cd/"; }
    return parse_sums(fetch_metadata(base + "SHA256SUMS", proxy), distro, release, edition, arch, base);
}
fs::path download_official(const OfficialImage& image, const fs::path& directory, Progress progress, const std::string& proxy) {
    validate(image); auto folder = fs::canonical(directory); need(fs::is_directory(folder), "Select a download folder");
    auto destination = folder / image.filename; need(!fs::exists(destination) && !fs::is_symlink(destination), "同名文件已存在，请选择其他目录或检查已有镜像。不会覆盖它。");
    auto free = fs::space(folder).available; need(free > (image.size ? image.size + (256ULL << 20) : (2ULL << 30)), "下载磁盘空间不足");
    // mkstemp cannot follow existing symlinks; mode 0600; temporary file stays on the same volume.
    auto pattern = (folder / ("." + image.filename + ".part-XXXXXX")).string(); std::vector<char> name(pattern.begin(), pattern.end()); name.push_back(0);
    int fd = mkstemp(name.data()); need(fd >= 0, "无法创建下载临时文件"); fs::path partial(name.data());
    try {
        Transfer t; t.fd = fd; t.limit = std::min<uint64_t>(image_limit, free - (256ULL << 20)); t.callback = std::move(progress);
        if (t.callback && !t.callback({0, image.size, 0})) throw Cancelled();
        request(image.url, t, proxy); need(t.written > 0 && (!image.size || t.written == image.size), "镜像大小与官网记录不符");
        need(fsync(fd) == 0, "Cannot flush downloaded image"); close(fd); fd = -1;
        if (t.callback && !t.callback({t.written, t.written, 0})) throw Cancelled();
        auto check_cancel = [&](uint64_t) { if (t.callback && !t.callback({t.written, t.written, 0})) throw Cancelled(); };
        auto hash = image.checksum_algorithm == "sha512" ? sha512(partial, check_cancel) : sha256(partial, check_cancel);
        if (t.callback && !t.callback({t.written, t.written, 0})) throw Cancelled();
        need(hash == image.checksum, "镜像校验失败，已删除下载临时文件");
        // Hard-link publication is atomic and refuses to replace an existing destination.
        need(link(partial.c_str(), destination.c_str()) == 0, "无法保存镜像，可能已有同名文件"); fs::remove(partial);
        return destination;
    } catch (...) { if (fd >= 0) close(fd); std::error_code ec; fs::remove(partial, ec); throw; }
}

OfficialImage resolve_recovery(const std::string& distro, const std::string& version, const std::string& platform, const std::string& proxy) {
    auto capability = source_capabilities(distro, version, platform); need(capability.at("automatic_available").get<bool>(), capability.at("reason"));
    if (distro == "debian") {
        auto arch = platform == "arm64-uefi" ? "arm64" : "amd64";
        std::string base = "https://cloud.debian.org/images/cloud/trixie/latest/";
        auto filename = "debian-13-nocloud-" + std::string(arch) + ".qcow2";
        auto text = fetch_metadata(base + "SHA512SUMS", proxy); std::istringstream lines(text); std::string line, checksum;
        while (std::getline(lines, line)) if (line.size() >= 131 && line[128] == ' ' && (line[129] == ' ' || line[129] == '*') && line.substr(130) == filename) {
            need(checksum.empty(), "Ambiguous Debian recovery checksum"); checksum = line.substr(0,128);
        }
        OfficialImage image{"debian", "13", "nocloud base system (no desktop)", arch, filename, base + filename, checksum, 0};
        image.format = "qcow2"; image.checksum_algorithm = "sha512"; image.automatic_restore = true; validate(image); return image;
    }
    std::string base = "https://cdimage.ubuntu.com/releases/26.04.1/release/", filename = "ubuntu-26.04.1-preinstalled-desktop-arm64+raspi.img.xz";
    auto sums = fetch_metadata(base + "SHA256SUMS", proxy);
    std::this_thread::sleep_for(std::chrono::seconds(6));
    auto signature = fetch_metadata(base + "SHA256SUMS.gpg", proxy); TempDir temporary("/tmp");
    write(temporary.path / "sums", sums); write(temporary.path / "signature", signature);
    run({"gpgv", "--homedir", temporary.path.string(), "--keyring", "/usr/share/keyrings/ubuntu-archive-keyring.gpg", (temporary.path / "signature").string(), (temporary.path / "sums").string()});
    OfficialImage image{"ubuntu", "26.04.1", "preinstalled desktop", "arm64", filename, base + filename, signed_checksum(sums, filename), 0};
    image.format = "img.xz"; image.automatic_restore = true; image.signature_verified = true; validate(image); return image;
}

std::string host_arch() { struct utsname u{}; need(uname(&u) == 0, "uname failed"); return std::string(u.machine) == "aarch64" ? "arm64" : std::string(u.machine) == "x86_64" ? "amd64" : "unsupported"; }
json download_catalog() {
    return {{"host_arch", host_arch()}, {"architectures", {"amd64", "arm64"}}, {"sources", {
        {{"distro", "ubuntu"}, {"releases", {"24.04", "26.04"}}, {"editions", {"desktop", "server"}}, {"download", true}, {"iso_automatic_restore", false}},
        {{"distro", "debian"}, {"releases", {"stable"}}, {"editions", {"netinst"}}, {"download", true}, {"iso_automatic_restore", false}},
        {{"distro", "fedora"}, {"releases", {"stable"}}, {"editions", {"workstation", "server"}}, {"download", true}, {"reset_engine", false}},
        {{"distro", "linuxmint"}, {"download", false}, {"reset_engine", false}, {"reason", "Not integrated; no ARM64 image is assumed"}}}}};
}
}
