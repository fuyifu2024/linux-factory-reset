#include "reset.hpp"
#include "download.hpp"
#include <iostream>
#include <set>

using namespace uf;
int main() {
    try {
        need(normalize_language("es_MX.UTF-8") == "es" && normalize_language("zh-Hans-CN") == "zh" && normalize_language("HI_in") == "hi", "Locale variants are normalized");
        need(normalize_language("ar_EG.UTF-8") == "ar" && normalize_language("C.UTF-8") == "en" && normalize_language("unsupported").empty(), "Unsupported and C locales have deterministic behavior");
        need(available_languages().size() == 5, "Exactly five interface languages are shipped");
        std::set<std::string> titles;
        for (const auto& entry : available_languages()) {
            auto code = entry.at("code").get<std::string>(); set_language(code);
            auto ui = json::parse(output({UFRESET_TEST_APP, "gui", "--preview", "--language", code}));
            need(ui.at("ui_language") == code && ui.at("text_direction") == entry.at("direction"), "Explicit language controls GUI metadata");
            need(ui.at("destructive_confirmation") == ERASE_CONFIRMATION && ui.at("actions").at(0) == tr("action.download"), "Translation cannot replace confirmation token or action identity");
            titles.insert(ui.at("title").get<std::string>());
            auto warning = tr("restore.warning", {{"token", ERASE_CONFIRMATION}});
            need(warning.find(ERASE_CONFIRMATION) != std::string::npos && warning.find("{token}") == std::string::npos, "Every warning displays the exact required confirmation token");
            auto report = command({UFRESET_TEST_APP, "check", "--language", code});
            auto parsed = json::parse(report.text);
            need(parsed.at("read_only").get<bool>() && parsed.at("checks").at(0).at("label") == tr("check.distribution"), "Preflight JSON keeps machine keys and localizes human labels");
            auto formatted = tr("download.arch", {{"arch", "arm64{literal}"}});
            need(formatted.find("arm64{literal}") != std::string::npos, "Inserted values are not recursively interpreted as translation templates");
            auto help = output({UFRESET_TEST_APP, "--language", code, "--help"});
            need(help.find(tr("cli.erase")) != std::string::npos && help.find("--confirm ERASE-ALL-USERS") != std::string::npos, "Localized help preserves command syntax and deletion semantics");
        }
        need(titles.size() == 5, "All five languages contain their own translated interface");
        set_language("en");
        need(command({UFRESET_TEST_APP, "gui", "--preview", "--language", "unsupported"}).code != 0, "Invalid explicit language is refused");
        need(command({UFRESET_TEST_APP, "--language", "es", "gui", "--language", "ar"}).code != 0, "Repeated language options cannot silently override each other");
        std::cout << "Five languages: GUI, preflight, help, placeholders and invariant deletion token passed.\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
