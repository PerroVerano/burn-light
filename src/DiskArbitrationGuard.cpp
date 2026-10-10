#include "DiskArbitrationGuard.hpp"
#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <sstream>
#include <sys/wait.h>
#include <sys/sysctl.h>
#include <sys/proc.h>
#include <sys/mman.h>
#include <poll.h>
#include <chrono>
#include <unistd.h>

namespace ac {
struct SharedGuardState { volatile sig_atomic_t childGroup; volatile sig_atomic_t released; };
static SharedGuardState *guardState = nullptr;
static int heartbeatFd = -1;
void guardChild(pid_t pid) { if (guardState) guardState->childGroup = pid; }
void guardHeartbeat() {
    if (heartbeatFd < 0) return;
    static auto last = std::chrono::steady_clock::now() - std::chrono::seconds(2);
    auto now = std::chrono::steady_clock::now();
    if (now - last >= std::chrono::seconds(1)) { char b = 'H'; (void)write(heartbeatFd, &b, 1); last = now; }
}
DiskArbitrationGuard::DiskArbitrationGuard(bool enabled, pid_t testDaemon) {
    if (!enabled) return;
    require(testDaemon > 1 || geteuid() == 0, "Optical access needs administrator privileges. Run sudo appendcd inspect/list/build/burn");
    auto processes = testDaemon > 1 ? std::to_string(testDaemon) : checkedRun({"/usr/bin/pgrep", "-x", "diskarbitrationd"});
    std::istringstream in(processes); std::vector<pid_t> pids; std::string token;
    while (in >> token) {
        pid_t pid = pid_t(number(token)); require(pid > 1, "Unsafe daemon PID");
        auto state = trim(checkedRun({"/bin/ps", "-p", token, "-o", "state="}));
        require(!state.empty() && state.find('T') == state.npos, "Disk Arbitration was already stopped; refusing to take ownership");
        pids.push_back(pid);
    }
    require(!pids.empty(), "No Disk Arbitration daemon found");
    require(heartbeatFd < 0 && guardState == nullptr, "Nested Disk Arbitration guards are not allowed");
    auto state = mmap(nullptr, sizeof(SharedGuardState), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
    require(state != MAP_FAILED, "Cannot allocate watchdog shared state");
    guardState = static_cast<SharedGuardState *>(state); guardState->childGroup = 0; guardState->released = 0;
    int lease[2], ready[2];
    if (pipe(lease) != 0) { restore(); throw std::runtime_error("Guard lease pipe failed"); }
    if (pipe(ready) != 0) { close(lease[0]); close(lease[1]); restore(); throw std::runtime_error("Guard readiness pipe failed"); }
    fcntl(lease[1], F_SETFD, FD_CLOEXEC);
    fcntl(lease[1], F_SETFL, O_NONBLOCK);
    auto owner = getpid();
    watchdog_ = fork();
    if (watchdog_ < 0) { for (int f : {lease[0], lease[1], ready[0], ready[1]}) close(f); restore(); throw std::runtime_error("Guard watchdog fork failed"); }
    if (!watchdog_) {
        // watchdog 独立于终端进程组；父进程异常退出或 SIGKILL 后 pipe EOF 仍会触发 CONT。
        setsid();
        for (int s : {SIGINT, SIGTERM, SIGHUP, SIGPIPE}) signal(s, SIG_IGN);
        close(lease[1]); close(ready[0]);
        std::vector<pid_t> stopped; bool ok = true;
        for (auto pid : pids) {
            if (guardState->released) { ok = false; break; }
            if (kill(pid, SIGSTOP) != 0) { ok = false; break; }
            stopped.push_back(pid);
            bool confirmed = false;
            for (int retry = 0; retry < 40; ++retry) {
                int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, pid}; struct kinfo_proc info{}; size_t size = sizeof(info);
                if (sysctl(mib, 4, &info, &size, nullptr, 0) == 0 && size && info.kp_proc.p_stat == SSTOP) { confirmed = true; break; }
                usleep(50000);
            }
            if (!confirmed) { ok = false; break; }
        }
        char ack = ok ? 'Y' : 'N';
        if (write(ready[1], &ack, 1) != 1) ok = false;
        close(ready[1]);
        bool stale = false;
        if (ok) {
            auto lastBeat = std::chrono::steady_clock::now();
            for (;;) {
                if (guardState->released) break;
                struct pollfd p{lease[0], POLLIN | POLLHUP, 0}; int event = poll(&p, 1, 1000);
                if (event > 0) {
                    char b[128]; auto n = read(lease[0], b, sizeof(b));
                    if (n == 0) { stale = getppid() != owner; break; }
                    if (n > 0) lastBeat = std::chrono::steady_clock::now();
                }
                if (getppid() != owner || std::chrono::steady_clock::now() - lastBeat > std::chrono::seconds(90)) { stale = true; break; }
            }
        }
        if (stale && guardState->childGroup > 1) {
            // 父进程失联时先结束其 backend，再恢复 DA，避免二者同时访问光驱。
            kill(-guardState->childGroup, SIGTERM); usleep(500000); kill(-guardState->childGroup, SIGKILL);
        }
        bool restored = true;
        for (auto pid : stopped) if (kill(pid, SIGCONT) != 0 && errno != ESRCH) restored = false;
        close(lease[0]);
        _exit(restored ? 0 : 1);
    }
    daemonPids_ = pids;
    close(lease[0]); close(ready[1]); lease_ = lease[1];
    char ack = 'N'; ssize_t n;
    struct pollfd readiness{ready[0], POLLIN | POLLHUP, 0};
    int readyEvent;
    do { readyEvent = poll(&readiness, 1, 10000); } while (readyEvent < 0 && errno == EINTR && !interrupted);
    n = readyEvent > 0 ? read(ready[0], &ack, 1) : -1;
    close(ready[0]);
    if (n != 1 || ack != 'Y') { restore(); throw std::runtime_error("Cannot suspend Disk Arbitration; daemon restoration attempted"); }
    heartbeatFd = lease_; guardHeartbeat();
    std::cerr << "Disk Arbitration suspended; independent recovery watchdog PID " << watchdog_ << '\n';
}
DiskArbitrationGuard::~DiskArbitrationGuard() { restore(); }
void DiskArbitrationGuard::restore() noexcept {
    heartbeatFd = -1;
    if (guardState && guardState != MAP_FAILED) guardState->released = 1;
    // 正常异常/SIGINT 清理直接发 CONT，不把系统服务恢复依赖于 waitpid 或 pipe EOF。
    bool restored = true;
    for (auto pid : daemonPids_) if (kill(pid, SIGCONT) != 0 && errno != ESRCH) restored = false;
    if (lease_ >= 0) { close(lease_); lease_ = -1; }
    if (watchdog_ > 0) {
        int status = 0; bool reaped = false;
        for (int retry = 0; retry < 100; ++retry) {
            auto result = waitpid(watchdog_, &status, WNOHANG);
            if (result == watchdog_ || (result < 0 && errno == ECHILD)) { reaped = true; break; }
            usleep(50000);
        }
        if (!reaped) {
            kill(watchdog_, SIGKILL);
            while (waitpid(watchdog_, &status, 0) < 0 && errno == EINTR) {}
            for (auto pid : daemonPids_) if (kill(pid, SIGCONT) != 0 && errno != ESRCH) restored = false;
            std::cerr << "Watchdog cleanup exceeded 5 seconds; direct CONT fallback attempted\n";
        }
        if (restored && (!reaped || (WIFEXITED(status) && WEXITSTATUS(status) == 0))) std::cerr << "Disk Arbitration CONT sent\n";
        else std::cerr << "ERROR: watchdog could not confirm restoration; run sudo kill -CONT $(pgrep -x diskarbitrationd)\n";
        watchdog_ = -1;
    }
    daemonPids_.clear();
    if (guardState && guardState != MAP_FAILED) { munmap(guardState, sizeof(SharedGuardState)); guardState = nullptr; }
}
}
