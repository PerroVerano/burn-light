#include "DiscScanner.hpp"
#include "DiskArbitrationGuard.hpp"
#include <iostream>
#include <regex>
#include <sys/wait.h>
#include <unistd.h>
using namespace ac;
int main(int argc, char **argv) {
    pid_t fake = -1;
    try {
        installSignals();
        bool synthetic = argc == 2 && std::string(argv[1]) == "--synthetic";
        require(synthetic, "Only --synthetic is enabled: real-daemon fault injection is disabled after native recovery failures");
        if (synthetic) {
            fake = fork(); require(fake >= 0, "Dummy daemon fork failed");
            if (!fake) { for (;;) pause(); }
        } else {
            auto disc = DiscScanner::preflight(); require(geteuid() == 0, "Guard recovery test needs root");
            std::smatch optical; require(std::regex_search(disc.drutil, optical, std::regex("Name:\\s*(/dev/disk[0-9]+)")), "No verified optical node");
            checkedRun({"/usr/sbin/diskutil", "unmountDisk", optical[1]});
        }
        auto daemon = synthetic ? std::to_string(fake) : trim(checkedRun({"/usr/bin/pgrep", "-x", "diskarbitrationd"}));
        require(daemon.find('\n') == daemon.npos, "Test expects one daemon");
        for (int fault : {0, 1, SIGINT, SIGKILL}) {
            std::cerr << "Testing fault=" << fault << '\n';
            int ready[2]; require(pipe(ready) == 0, "Test pipe failed"); auto child = fork(); require(child >= 0, "Test fork failed");
            if (!child) {
                close(ready[0]);
                try {
                    DiskArbitrationGuard guard(true, synthetic ? fake : -1);
                    char yes = 'Y'; write(ready[1], &yes, 1); close(ready[1]);
                    std::cerr << "Child ready for fault=" << fault << '\n';
                    if (!fault) throw std::runtime_error("deliberate generator exception");
                    if (fault == 1) {
                        // 未 exec 的子进程持有 lease writer：恢复必须依赖 release/CONT，不能只等 EOF。
                        auto holder = fork(); require(holder >= 0, "Lease holder fork failed");
                        if (!holder) { for (;;) pause(); }
                        guard.restore();
                        kill(holder, SIGKILL); waitpid(holder, nullptr, 0);
                        _exit(0);
                    }
                    for (;;) { usleep(10000); checkInterrupted(); }
                } catch (...) { _exit(0); }
            }
            close(ready[1]); char ack = 0; auto n = read(ready[0], &ack, 1); close(ready[0]);
            require(n == 1 && ack == 'Y', "Test could not suspend daemon");
            std::cerr << "Parent received ready; injecting " << fault << '\n';
            if (fault > 1) { require(kill(child, fault) == 0, "Cannot deliver test signal"); }
            int status = 0; bool exited = false;
            for (int retry = 0; retry < 100; ++retry) {
                if (waitpid(child, &status, WNOHANG) == child) { exited = true; break; }
                usleep(100000);
            }
            if (!exited) { kill(child, SIGKILL); waitpid(child, &status, 0); }
            bool restored = false;
            for (int i = 0; i < 40; ++i) {
                auto state = trim(checkedRun({"/bin/ps", "-p", daemon, "-o", "state="}));
                if (!state.empty() && state.find('T') == state.npos) { restored = true; break; }
                usleep(100000);
            }
            require(restored, "Disk Arbitration remained stopped after fault " + std::to_string(fault));
            require(exited, "Fault handler exceeded 10 seconds (test killed child and watchdog restored daemon)");
            std::cout << "PASS restoration after " << (fault == 0 ? "exception" : fault == 1 ? "inherited lease writer" : fault == SIGINT ? "SIGINT" : "SIGKILL") << '\n';
        }
        std::cout << "All 4 synthetic recovery checks passed; no system service or optical write performed\n";
        if (fake > 1) { kill(fake, SIGKILL); waitpid(fake, nullptr, 0); }
        return 0;
    } catch (const std::exception &e) { if (fake > 1) { kill(fake, SIGCONT); kill(fake, SIGKILL); waitpid(fake, nullptr, 0); } std::cerr << e.what() << '\n'; return 1; }
}
