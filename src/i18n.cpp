#include "reset.hpp"
#include "catalogs.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fcntl.h>
#include <locale.h>
#include <sys/stat.h>
#include <unistd.h>

namespace uf {
namespace {
std::string active = "en";
std::string env(const char* name) { auto value = getenv(name); return value ? value : ""; }
// Preserve startup settings: GTK child locale overrides must not redefine "automatic".
const std::vector<std::string> system_languages{env("LANGUAGE"), env("LC_ALL"), env("LC_MESSAGES"), env("LANG")};
const std::map<std::string, json>& catalogs() {
    static const std::map<std::string, json> data{{"en", json::parse(catalog_data::en)}, {"zh", json::parse(catalog_data::zh)},
        {"hi", json::parse(catalog_data::hi)}, {"es", json::parse(catalog_data::es)}, {"ar", json::parse(catalog_data::ar)}};
    return data;
}
fs::path preference_path() {
    auto base = env("XDG_CONFIG_HOME"); if (base.empty()) { auto home = env("HOME"); if (home.empty()) return {}; base = home + "/.config"; }
    fs::path path(base); if (!path.is_absolute()) return {}; return path / "linux-factory-reset/language";
}
std::string saved_language() {
    if (geteuid() == 0) return ""; // Privileged operations receive an explicit, validated language argument.
    try {
        auto path = preference_path(); struct stat st{};
        if (path.empty() || lstat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0077) || st.st_size > 16) return "";
        auto value = read(path); while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
        if (value == "auto" || catalogs().contains(value)) return value;
    } catch (const std::exception&) {}
    return "";
}
std::string environment_language() {
    auto candidates = system_languages; candidates.insert(candidates.begin(), env("LFR_LANGUAGE"));
    for (const auto& value : candidates) {
        size_t start = 0;
        while (start < value.size()) {
            auto end = value.find(':', start); auto normalized = normalize_language(value.substr(start, end - start));
            if (!normalized.empty()) return normalized;
            if (end == std::string::npos) break; start = end + 1;
        }
    }
    return "en";
}
}
std::string normalize_language(const std::string& value) {
    auto base = value.substr(0, value.find_first_of("_.-@:"));
    std::transform(base.begin(), base.end(), base.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (base == "c" || base == "posix") return "en";
    return base == "en" || base == "zh" || base == "hi" || base == "es" || base == "ar" ? base : "";
}
void set_language(const std::string& requested) {
    auto selected = requested;
    if (selected.empty()) {
        auto override = env("LFR_LANGUAGE");
        if (override == "auto" || !normalize_language(override).empty()) selected = override;
        if (selected.empty()) selected = saved_language();
    }
    if (selected.empty() || selected == "auto") { active = environment_language(); return; }
    auto normalized = normalize_language(selected);
    if (normalized.empty()) throw Error("Unsupported language; use en, zh, hi, es, ar or auto");
    active = normalized;
}
std::string language() { return active; }
json available_languages() {
    return json::array({{{"code", "en"}, {"name", "English"}, {"direction", "ltr"}}, {{"code", "zh"}, {"name", "简体中文"}, {"direction", "ltr"}},
        {{"code", "hi"}, {"name", "हिन्दी"}, {"direction", "ltr"}}, {{"code", "es"}, {"name", "Español"}, {"direction", "ltr"}},
        {{"code", "ar"}, {"name", "العربية"}, {"direction", "rtl"}}});
}
std::string tr(const std::string& key, const std::map<std::string, std::string>& values) {
    const auto& data = catalogs();
    const auto& selected = data.at(active).contains(key) ? data.at(active) : data.at("en");
    if (!selected.contains(key)) throw Error("Unknown translation key: " + key);
    auto pattern = selected.at(key).get<std::string>(); std::string result; size_t start = 0;
    while (start < pattern.size()) {
        auto open = pattern.find('{', start); if (open == std::string::npos) { result += pattern.substr(start); break; }
        result += pattern.substr(start, open - start); auto close = pattern.find('}', open);
        if (close == std::string::npos) throw Error("Malformed translation template");
        auto name = pattern.substr(open + 1, close - open - 1); auto value = values.find(name);
        if (value == values.end()) throw Error("Missing translation value: " + name);
        result += value->second; start = close + 1;
    }
    return result;
}
std::string localize_message(const std::string& message) {
    static const auto aliases = [] {
        std::map<std::string, std::string> result;
        for (const auto& code : {"en", "zh"}) for (const auto& [key, value] : catalogs().at(code).items()) {
            auto text = value.get<std::string>(); if (text.find('{') == std::string::npos) result.emplace(text, key);
        }
        return result;
    }();
    auto found = aliases.find(message); return found == aliases.end() ? message : tr(found->second);
}
void save_language(const std::string& selected) {
    if (geteuid() == 0 || (selected != "auto" && !catalogs().contains(selected))) throw Error("Invalid language preference");
    auto path = preference_path(); need(!path.empty(), "Cannot locate user configuration directory");
    if (fs::create_directories(path.parent_path())) fs::permissions(path.parent_path(), fs::perms::owner_all);
    struct stat parent{}; need(lstat(path.parent_path().c_str(), &parent) == 0 && S_ISDIR(parent.st_mode) && parent.st_uid == geteuid() && !(parent.st_mode & 0022), "Unsafe language preference directory");
    std::string temporary = (path.parent_path() / ".language-XXXXXX").string(); int fd = mkstemp(temporary.data()); need(fd >= 0, "Cannot save language preference");
    try {
        auto text = selected + "\n"; need(::write(fd, text.data(), text.size()) == static_cast<ssize_t>(text.size()) && fsync(fd) == 0, "Cannot save language preference");
        int status = close(fd); fd = -1; need(status == 0, "Cannot close language preference"); fs::rename(temporary, path);
    } catch (...) { if (fd >= 0) close(fd); unlink(temporary.c_str()); throw; }
}
void configure_dialog_language() {
    static const std::map<std::string, std::string> locales{{"en", "en_US.UTF-8"}, {"zh", "zh_CN.UTF-8"}, {"hi", "hi_IN.UTF-8"}, {"es", "es_ES.UTF-8"}, {"ar", "ar_EG.UTF-8"}};
    auto candidate = locales.at(active);
    auto installed = [&] { auto probe = newlocale(LC_CTYPE_MASK | LC_MESSAGES_MASK, candidate.c_str(), nullptr); if (!probe) return false; freelocale(probe); return true; };
    bool have_locale = installed();
    // GTK 3 and older GTK 4 derive text direction from locale/GTK translations.
    // Minimal Debian installations ship locale sources, but not generated locales.
    // Generate only this user's Arabic locale, never change the system's default.
    if (!have_locale && active == "ar") {
        auto base = env("XDG_CACHE_HOME"); if (base.empty() && !env("HOME").empty()) base = env("HOME") + "/.cache";
        need(geteuid() != 0 && fs::path(base).is_absolute(), "Arabic GUI needs an Arabic UTF-8 locale (install locales or glibc-langpack-ar)");
        auto cache = fs::path(base) / "linux-factory-reset/locales";
        if (fs::create_directories(cache)) fs::permissions(cache, fs::perms::owner_all);
        struct stat st{}; need(lstat(cache.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == geteuid() && !(st.st_mode & 0022), "Unsafe locale cache directory");
        auto target = cache / candidate;
        if (!fs::exists(target)) {
            TempDir temporary(cache); auto staged = temporary.path / candidate;
            need(command({"localedef", "--no-archive", "-i", "ar_EG", "-f", "UTF-8", staged.string()}, false).code == 0,
                "Arabic GUI needs locale data: install locales (Debian/Ubuntu) or glibc-langpack-ar (Fedora)");
            std::error_code error; fs::rename(staged, target, error);
            need(!error || fs::is_directory(target), "Cannot publish the private Arabic locale");
        }
        static const auto original_path = env("LOCPATH");
        auto path = cache.string() + (original_path.empty() ? "" : ":" + original_path);
        setenv("LOCPATH", path.c_str(), 1); have_locale = installed();
        need(have_locale, "Cannot load the private Arabic UTF-8 locale");
    }
    auto selected = have_locale ? candidate : "C.UTF-8";
    setenv("LC_ALL", selected.c_str(), 1);
    setenv("LANGUAGE", active.c_str(), 1);
}
}
