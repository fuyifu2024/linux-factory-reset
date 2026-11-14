// CI-only runtime probe of the same GTK generation used by the installed Zenity.
#include "reset.hpp"
#include <cstdlib>
#include <dlfcn.h>
#include <iostream>

int main(int argc, char** argv) {
    try {
        uf::need(argc == 2, "language argument required"); uf::set_language(argv[1]); uf::configure_dialog_language();
        setenv("GSK_RENDERER", "cairo", 1); setenv("LIBGL_ALWAYS_SOFTWARE", "1", 1);
        bool gtk4 = uf::read("/usr/bin/zenity").find("libgtk-4.so") != std::string::npos;
        auto library = dlopen(gtk4 ? "libgtk-4.so.1" : "libgtk-3.so.0", RTLD_NOW | RTLD_LOCAL);
        uf::need(library != nullptr, "Cannot load Zenity's GTK runtime");
        auto init = dlsym(library, "gtk_init"), get_direction = dlsym(library, "gtk_widget_get_default_direction");
        uf::need(init && get_direction, "GTK initialization/direction API unavailable");
        if (gtk4) reinterpret_cast<void(*)()>(init)();
        else reinterpret_cast<void(*)(int*,char***)>(init)(nullptr, nullptr);
        auto direction = reinterpret_cast<int(*)()>(get_direction)();
        auto expected = uf::language() == "ar" ? 2 : 1;
        uf::need(direction == expected, "Actual GTK widget direction does not match selected interface language");
        std::cout << uf::language() << " GTK" << (gtk4 ? 4 : 3) << ' ' << (direction == 2 ? "rtl" : "ltr") << '\n';
        // GTK owns process-lifetime objects; do not dlclose the library before exit.
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
