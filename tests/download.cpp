#include "download.hpp"
#include <iostream>
using namespace uf;
int main(int argc, char** argv) {
    try {
        need(argc == 2, "fixture directory required"); fs::path dir = argv[1];
        auto image = resolve_image("ubuntu", "26.04", "desktop", "amd64");
        need(image.filename == "ubuntu-26.04.1-desktop-amd64.iso", "TLS fixture metadata was not used");
        auto original = read(dir / "26.04" / image.filename);
        auto destination = dir / "downloads"; fs::create_directory(destination);
        bool updated = false;
        auto file = download_official(image, destination, [&](const TransferProgress& p) {updated = true; need(p.percent() <= 99, "unverified success progress"); return true;});
        need(updated && read(file) == original, "HTTPS download or progress failed");
        bool overwrite_rejected = false; try {download_official(image, destination);} catch (const Error&) {overwrite_rejected = true;}
        need(overwrite_rejected && read(file) == original, "existing destination overwritten");
        fs::remove(file); auto wrong = image; wrong.checksum = std::string(64, 'f');
        bool rejected = false; try {download_official(wrong, destination);} catch (const Error&) {rejected = true;}
        need(rejected && !fs::exists(file), "corrupt checksum was published");
        bool cancelled = false;
        try {download_official(image, destination, [](const TransferProgress& p) {return p.downloaded == 0;});} catch (const Cancelled&) {cancelled = true;}
        need(cancelled && !fs::exists(file), "in-flight cancellation published image");
        for (const auto& entry : fs::directory_iterator(destination)) need(entry.path().filename().string().find(".part-") == std::string::npos, "partial file leak");
        std::cout << "Real libcurl HTTPS: metadata, download, hash, no-overwrite, cancellation and cleanup passed.\n"; return 0;
    } catch (const std::exception& e) {std::cerr << e.what() << '\n'; return 1;}
}
