#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <csignal>

namespace ac {
namespace fs = std::filesystem;
constexpr size_t BlockSize = 2048;
using Block = std::array<uint8_t, BlockSize>;
extern volatile sig_atomic_t interrupted;
void installSignals();
void checkInterrupted();
void guardHeartbeat();
void guardChild(pid_t pid);
void require(bool condition, const std::string &message);
uint32_t number(const std::string &text);
std::string trim(std::string text);
struct ProcessResult { int status; std::string output; };
ProcessResult run(const std::vector<std::string> &args, int timeoutSeconds = 60, bool echo = false,
                  const std::map<std::string, std::string> &environment = {});
std::string checkedRun(const std::vector<std::string> &args, int timeoutSeconds = 60, bool echo = false,
                       const std::map<std::string, std::string> &environment = {});
void writeText(const fs::path &path, const std::string &text);
std::string readText(const fs::path &path);
std::string shaFile(const fs::path &path);
std::string hashBytes(const std::function<void(const std::function<void(const void *, size_t)> &)> &feed);
std::string utf16Name(const uint8_t *bytes, size_t size);
std::string windowsKey(const std::string &name);
void validateName(const std::string &name);
class TempDir {
public:
    fs::path path;
    TempDir();
    ~TempDir();
    TempDir(const TempDir &) = delete;
};
}
