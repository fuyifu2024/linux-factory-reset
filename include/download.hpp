#pragma once
#include "reset.hpp"
#include <chrono>
#include <cstdint>
namespace uf {
struct TransferProgress {
    uint64_t downloaded = 0, total = 0;
    double bytes_per_second = 0;
    int percent() const;
    std::string text() const;
};
using Progress = std::function<bool(const TransferProgress&)>;
struct Cancelled : Error { Cancelled() : Error("下载已取消，未设置恢复标记。") {} };
struct OfficialImage {
    std::string distro, release, edition, arch, filename, url, checksum;
    uint64_t size = 0;
    json describe() const;
};
// Only authenticated HTTPS transport is accepted. Checksum is required for image publication.
std::string fetch_metadata(const std::string& url, const std::string& proxy = "");
OfficialImage parse_sums(const std::string& text, const std::string& distro, const std::string& release,
                         const std::string& edition, const std::string& arch, const std::string& base);
OfficialImage parse_fedora(const std::string& text, const std::string& arch, const std::string& edition);
OfficialImage resolve_image(const std::string& distro, const std::string& release, const std::string& edition,
                            const std::string& arch, const std::string& proxy = "");
fs::path download_official(const OfficialImage& image, const fs::path& directory,
                          Progress progress = {}, const std::string& proxy = "");
json download_catalog();
std::string host_arch();
}
