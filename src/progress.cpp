#include "reset.hpp"
#include <iostream>

namespace uf {
std::string stage_label(const std::string& code) {
    static const std::map<std::string, std::string> labels{
        {"checking", "检查环境和恢复盘"}, {"copying", "暂存所选镜像"}, {"verifying", "校验镜像完整性"},
        {"decompressing", "展开压缩镜像"}, {"converting", "转换虚拟磁盘镜像"}, {"inspecting", "检查镜像分区与系统内容"},
        {"normalizing", "整理初始系统与首次开户资料"}, {"packing", "生成压缩恢复资料"}, {"registering", "校验并保存恢复资料"},
        {"comparing", "比较恢复前后的文件变更"}, {"initramfs", "生成离线恢复启动环境"},
        {"rescue", "准备独立救援入口"}, {"arming", "安排下次启动恢复"}, {"cancelling", "取消待执行恢复"},
        {"downloading", "下载官方恢复镜像"}};
    auto found = labels.find(code); return found == labels.end() ? "" : found->second;
}
void operation_stage(const std::string& code) {
    need(!stage_label(code).empty(), "Unknown operation stage");
    std::cerr << "\nLFR_STAGE " << json{{"stage", code}}.dump() << '\n' << std::flush;
}
}
