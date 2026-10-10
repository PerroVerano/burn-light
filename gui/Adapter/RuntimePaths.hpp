#pragma once
#include <mach-o/dyld.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace burnlight {
// 只用于 gui/ 中单独编译的 CLI；原源码与原 build/ 保持不变。
inline std::string runtime(const char *name) {
    uint32_t length = 0; _NSGetExecutablePath(nullptr, &length);
    std::vector<char> path(length);
    if (_NSGetExecutablePath(path.data(), &length) != 0) throw std::runtime_error("Cannot resolve bundled runtime");
    auto result = std::filesystem::canonical(path.data()).parent_path() / name;
    if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(result))) throw std::runtime_error("Bundled runtime missing: " + result.string());
    return result.string();
}
}
#define CDRECORD_PATH burnlight::runtime("appendcd-cdrecord")
#define INITIAL_CDRECORD_PATH burnlight::runtime("appendcd-initial-cdrecord")
#define READCD_PATH burnlight::runtime("readcd")
