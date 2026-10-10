#include "Util.hpp"
#include <CommonCrypto/CommonDigest.h>
#include <CoreFoundation/CoreFoundation.h>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

namespace ac {
volatile sig_atomic_t interrupted = 0;
static void signalHandler(int signal) { interrupted = signal; }
void installSignals() {
    struct sigaction action{};
    action.sa_handler = signalHandler;
    sigemptyset(&action.sa_mask);
    for (int s : {SIGINT, SIGTERM, SIGHUP}) sigaction(s, &action, nullptr);
    signal(SIGPIPE, SIG_IGN);
}
void checkInterrupted() { guardHeartbeat(); require(!interrupted, "Interrupted; cleanup will restore Disk Arbitration"); }
void require(bool c, const std::string &m) { if (!c) throw std::runtime_error(m); }
uint32_t number(const std::string &s) {
    require(!s.empty() && s.find_first_not_of("0123456789") == std::string::npos, "Invalid LBA/number: " + s);
    auto n = std::stoull(s); require(n <= UINT32_MAX, "Number exceeds uint32: " + s); return uint32_t(n);
}
std::string trim(std::string s) {
    auto b = s.find_first_not_of(" \t\r\n");
    return b == s.npos ? "" : s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}
ProcessResult run(const std::vector<std::string> &args, int timeout, bool echo, const std::map<std::string, std::string> &environment) {
    require(!args.empty() && args[0][0] == '/', "Commands must use an absolute executable path");
    checkInterrupted();
    int pipefd[2]; require(pipe(pipefd) == 0, "pipe failed");
    pid_t pid = fork();
    if (pid < 0) { close(pipefd[0]); close(pipefd[1]); throw std::runtime_error("fork failed"); }
    if (!pid) {
        setpgid(0, 0);
        for (int s : {SIGINT, SIGTERM, SIGHUP, SIGPIPE}) signal(s, SIG_DFL);
        close(pipefd[0]); dup2(pipefd[1], STDOUT_FILENO); dup2(pipefd[1], STDERR_FILENO); close(pipefd[1]);
        // 不接受 shell 插值，也不让用户环境改变 root 下的刻录工具行为。
        setenv("LC_ALL", "C", 1);
        for (const char *e : {"CDR_DEVICE", "CDR_SPEED", "CDR_FIFOSIZE", "RSH", "RSCSI", "APPENDCD_DEVICE_REGISTRY_ID"}) unsetenv(e);
        for (const auto &[key, value] : environment) if (setenv(key.c_str(), value.c_str(), 1) != 0) _exit(126);
        std::vector<char *> argv;
        for (const auto &a : args) argv.push_back(const_cast<char *>(a.c_str()));
        argv.push_back(nullptr); execv(argv[0], argv.data()); _exit(127);
    }
    close(pipefd[1]); setpgid(pid, pid);
    guardChild(pid);
    fcntl(pipefd[0], F_SETFL, O_NONBLOCK);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout);
    bool reaped = false, eof = false, terminated = false;
    int status = 0;
    std::string output;
    bool excessiveOutput = false;
    auto killDeadline = deadline;
    while (!reaped || !eof) {
        guardHeartbeat();
        if (!terminated && (interrupted || excessiveOutput || std::chrono::steady_clock::now() > deadline)) {
            kill(-pid, SIGTERM); terminated = true;
            killDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        }
        if (terminated && std::chrono::steady_clock::now() > killDeadline) kill(-pid, SIGKILL);
        char buffer[8192]; ssize_t n;
        int chunks = 0;
        while (++chunks <= 64 && (n = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
            if (output.size() + size_t(n) >= 32 * 1024 * 1024) { excessiveOutput = true; break; }
            output.append(buffer, size_t(n));
            if (echo) { std::cerr.write(buffer, n); std::cerr.flush(); }
        }
        if (n == 0) eof = true;
        if (!reaped && waitpid(pid, &status, WNOHANG) == pid) reaped = true;
        if (!reaped || !eof) { struct pollfd p{pipefd[0], POLLIN, 0}; poll(&p, 1, 100); }
    }
    close(pipefd[0]); guardChild(0); checkInterrupted();
    require(!excessiveOutput, "Command output exceeds limit: " + args[0]);
    require(!terminated, "Command timed out: " + args[0]);
    return {WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status), output};
}
std::string checkedRun(const std::vector<std::string> &args, int timeout, bool echo, const std::map<std::string, std::string> &environment) {
    auto r = run(args, timeout, echo, environment);
    require(r.status == 0, args[0] + " failed (" + std::to_string(r.status) + "):\n" + r.output);
    return r.output;
}
void writeText(const fs::path &p, const std::string &text) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc); f << text;
    require(bool(f), "Cannot write: " + p.string());
}
std::string readText(const fs::path &p) {
    std::ifstream f(p, std::ios::binary); require(bool(f), "Cannot read: " + p.string());
    return {std::istreambuf_iterator<char>(f), {}};
}
std::string hashBytes(const std::function<void(const std::function<void(const void *, size_t)> &)> &feed) {
    CC_SHA256_CTX context; CC_SHA256_Init(&context);
    feed([&](const void *p, size_t size) { CC_SHA256_Update(&context, p, CC_LONG(size)); });
    unsigned char digest[CC_SHA256_DIGEST_LENGTH]; CC_SHA256_Final(digest, &context);
    std::ostringstream o; for (auto c : digest) o << std::hex << std::setfill('0') << std::setw(2) << int(c);
    return o.str();
}
std::string shaFile(const fs::path &p) {
    return hashBytes([&](auto sink) {
        std::ifstream f(p, std::ios::binary); require(bool(f), "Cannot hash: " + p.string());
        std::array<char, 65536> b{};
        while (f) { checkInterrupted(); f.read(b.data(), b.size()); if (f.gcount()) sink(b.data(), size_t(f.gcount())); }
        require(f.eof(), "Hash read error: " + p.string());
    });
}
static std::string cfUtf8(CFStringRef s) {
    require(s != nullptr, "Invalid Unicode filename");
    std::vector<char> out(size_t(CFStringGetMaximumSizeForEncoding(CFStringGetLength(s), kCFStringEncodingUTF8)) + 1);
    require(CFStringGetCString(s, out.data(), out.size(), kCFStringEncodingUTF8), "Cannot convert Unicode filename");
    return out.data();
}
std::string utf16Name(const uint8_t *bytes, size_t size) {
    require(size % 2 == 0, "Odd Joliet name length");
    auto s = CFStringCreateWithBytes(nullptr, bytes, CFIndex(size), kCFStringEncodingUTF16BE, false);
    require(s, "Malformed Joliet UTF-16"); auto out = cfUtf8(s); CFRelease(s); return out;
}
std::string windowsKey(const std::string &name) {
    auto s = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8 *>(name.data()), name.size(), kCFStringEncodingUTF8, false);
    require(s, "Filename is not UTF-8"); auto m = CFStringCreateMutableCopy(nullptr, 0, s); CFRelease(s);
    CFStringNormalize(m, kCFStringNormalizationFormC); CFStringFold(m, kCFCompareCaseInsensitive, nullptr);
    auto out = cfUtf8(m); CFRelease(m); return out;
}
void validateName(const std::string &name) {
    require(!name.empty() && name != "." && name != ".." && name.back() != '.' && name.back() != ' ', "Windows-invalid filename: " + name);
    for (unsigned char c : name) require(c >= 32 && std::string("<>:\"/\\|?*").find(char(c)) == std::string::npos, "Windows-invalid filename: " + name);
    auto s = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8 *>(name.data()), name.size(), kCFStringEncodingUTF8, false);
    require(s, "Filename is not UTF-8"); auto length = CFStringGetLength(s); CFRelease(s);
    require(length <= 103, "Joliet name exceeds 103 UTF-16 units: " + name);
    auto k = windowsKey(name); auto stem = k.substr(0, k.find('.'));
    require(stem != "con" && stem != "prn" && stem != "aux" && stem != "nul" && !(stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '1' && stem[3] <= '9'), "Windows reserved name: " + name);
}
TempDir::TempDir() {
    char name[] = "/private/tmp/appendcd-XXXXXX"; auto p = mkdtemp(name); require(p, "mkdtemp failed"); path = p;
}
TempDir::~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
}
