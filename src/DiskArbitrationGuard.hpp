#pragma once
#include "Util.hpp"
#include <sys/types.h>
namespace ac {
class DiskArbitrationGuard {
    int lease_ = -1;
    pid_t watchdog_ = -1;
    std::vector<pid_t> daemonPids_;
public:
    explicit DiskArbitrationGuard(bool enabled = true, pid_t testDaemon = -1);
    ~DiskArbitrationGuard();
    void restore() noexcept;
    DiskArbitrationGuard(const DiskArbitrationGuard &) = delete;
};
}
