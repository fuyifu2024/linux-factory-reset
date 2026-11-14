#include "reset.hpp"
#include <iostream>

namespace uf {
std::string stage_label(const std::string& code) {
    static const std::map<std::string, std::string> labels{
        {"checking", "stage.checking"}, {"copying", "stage.copying"}, {"verifying", "stage.verifying"},
        {"decompressing", "stage.decompressing"}, {"converting", "stage.converting"}, {"inspecting", "stage.inspecting"},
        {"normalizing", "stage.normalizing"}, {"packing", "stage.packing"}, {"registering", "stage.registering"},
        {"comparing", "stage.comparing"}, {"initramfs", "stage.initramfs"},
        {"rescue", "stage.rescue"}, {"arming", "stage.arming"}, {"cancelling", "stage.cancelling"},
        {"downloading", "stage.downloading"}};
    auto found = labels.find(code); return found == labels.end() ? "" : tr(found->second);
}
void operation_stage(const std::string& code) {
    need(!stage_label(code).empty(), "Unknown operation stage");
    std::cerr << "\nLFR_STAGE " << json{{"stage", code}}.dump() << '\n' << std::flush;
}
}
