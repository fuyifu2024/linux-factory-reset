#pragma once
#include <filesystem>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace uf {
namespace fs = std::filesystem;
using json = nlohmann::json;
using Args = std::vector<std::string>;
struct Error : std::runtime_error { using std::runtime_error::runtime_error; };
struct Result { int code; std::string text; };
inline constexpr auto VERSION = "0.3.0-alpha.1";
inline const fs::path STATE = "/etc/ubuntu-reset/state.json";
inline const fs::path REQUEST = "/var/lib/ubuntu-reset/request";
void need(bool condition, const std::string& message);
std::string read(const fs::path& path);
void write(const fs::path& path, const std::string& data);
std::string sha256(const fs::path& path);
Result command(const Args& args, bool capture = true, int stdout_fd = -1, bool desktop = false,
               bool merge_errors = false, bool live_output = false);
std::string output(const Args& args);
void run(const Args& args);
void root_only();
void secure(const fs::path& path);
void atomic_write(const fs::path& path, const std::string& text, unsigned mode = 0600);
std::string random_id();
bool valid_uuid(const std::string& value);
bool valid_sha(const std::string& value);
void validate_state(const json& value);
json load_state(const fs::path& path = STATE, bool check_permissions = true);
json preparation_status(const fs::path& path = STATE, bool check_permissions = true);
std::string image_format(const fs::path& file);
json source_capabilities(const std::string& distro, const std::string& version, const std::string& platform);
fs::path recovery_mount(const fs::path& selected, const json& filesystem, const json& target);
std::string boot_config(const json& state);
void https(const std::string& url, bool official = false);
std::string signed_checksum(const std::string& sums, const std::string& filename);
std::string relocate(const std::string& input, const std::string& root, const std::string& boot);
void set_download_proxy(const std::string& value);
bool stock_pi_boot(const std::string& config);
bool supported_platform(const std::string& kind);
json distribution(const std::string& os_release);
std::string select_platform(const std::string& arch, bool uefi, const std::string& model, const std::string& distro);
std::string state_distro(const json& state);
fs::path efi_directory(const fs::path& boot, const std::string& distro);
void require_inside(const fs::path& tree, const fs::path& path);
void validate_reference(const fs::path& root, const fs::path& boot, const std::string& kind, bool prepared);
struct TempDir {
    fs::path path;
    explicit TempDir(const fs::path& parent);
    ~TempDir();
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
};
struct Mount {
    TempDir temp;
    Mount(const fs::path& device, const std::string& type, const std::string& options = "ro");
    ~Mount();
    Mount(const Mount&) = delete;
    Mount& operator=(const Mount&) = delete;
};
json hardware();
json firmware_status();
void firmware_reset(const std::string& confirm);
void make_profile(const fs::path& root, const fs::path& boot, const std::string& platform,
                  const fs::path& destination, const json& provenance,
                  const fs::path& firstboot_assets = "/usr/lib/ubuntu-reset");
void prepare_pi(const std::string& release, const fs::path& storage);
void prepare_profile(const std::string& url, const std::string& checksum, const fs::path& storage);
void prepare_image(const fs::path& image, const fs::path& storage);
void prepare_automatic(const fs::path& storage);
json detected_sources();
fs::path preflight(const json& state);
void plan(const json& state);
void arm(const json& state, const std::string& confirmation, bool reboot);
void cancel();
int gui(bool preview);
}
