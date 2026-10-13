#pragma once
#include "IsoCore.hpp"
#include "OpticalDrive.hpp"
#include <set>

namespace ac {
struct PhysicalTrack {
    uint32_t number = 0, session = 0, start = 0, nwa = 0, free = 0, size = 0, lastRecorded = 0;
    bool reserved = false, blank = false, damage = false, nwaValid = false, lraValid = false;
    uint32_t incremental = 0, dataMode = 0, trackMode = 0;
    bool emptyReserved() const;
};
struct RecoveryPlan {
    uint32_t track = 0, start = 0, blocks = 0, reservedCount = 0;
};
struct DiscInfo {
    DriveIdentity drive;
    uint32_t driveIndex = 0, writeSpeed = 0;
    std::string media = "DVD-R";
    std::string device, drutil, driveInfo, driveInfoXml, inquiry, minfo, toc, msinfo, atip, fulltoc, backendProbe;
    uint32_t sessions = 0, tracks = 0, last = 0, next = 0;
    uint32_t freeBlocks = 0;
    uint32_t readableEnd = 0;
    bool appendable = false;
    bool blank = false;
    uint32_t physicalFirst = 0, physicalLast = 0, sessionStatus = 0;
    std::vector<PhysicalTrack> physical;
    std::vector<Blocks::Range> recorded;
    void requireNormalTarget() const;
    void requireInitialTarget() const;
    bool closedSession() const { return sessionStatus==0 || (media=="CD-R" && sessionStatus==2); }
    RecoveryPlan recoveryPlan() const;
    RecoveryPlan finishPlan() const;
    void print() const;
    void save(const fs::path &directory, const std::string &prefix) const;
};
class DiscScanner {
public:
    static DiscInfo preflight(const std::string &selectedDevice = "", bool allowBlank = false);
    static DiscInfo scan(const DiscInfo &preflight, const std::string &selectedDevice = "");
    static void revalidate(const DiscInfo &disc);
    static DiscInfo parseStatus(const DriveIdentity &drive, const std::string &status, bool allowBlank = false);
    static void applyInitialProbe(DiscInfo &disc, const std::string &probe);
    static void applySessionAddresses(DiscInfo &disc, const std::string &msinfo);
    static void applyRecordedTracks(DiscInfo &disc);
    static void checkInquiry(const DriveIdentity &drive, const std::string &inquiry);
    static void checkWriteCapabilities(DriveIdentity &drive, const std::string &info, const std::string &infoXml);
};
std::vector<std::string> recordingOptions(const DiscInfo &disc, const std::string &filesystem = "iso9660+joliet");
std::string recordingBackend(const DiscInfo &disc, const std::string &filesystem);
class OpticalBlocks : public Blocks {
    DiscInfo disc_;
    fs::path directory_;
    int fd_ = -1;
    std::set<uint32_t> cached_;
public:
    OpticalBlocks(const DiscInfo &disc, const fs::path &directory);
    ~OpticalBlocks() override;
    Block read(uint32_t lba) override;
    void prefetch(uint32_t lba, uint32_t count) override;
    void verifyCachedMetadata(Blocks &fresh);
    std::vector<Range> recordedRanges() const override { return disc_.recorded; }
    fs::path snapshot() const { return directory_ / "disc-cache.iso"; }
};
void recordSession(const DiscInfo &disc, const fs::path &image, bool recovery = false);
void recordInitialSession(const DiscInfo &disc, const fs::path &image);
void finalizeSession(const DiscInfo &disc, const fs::path &evidence);
}
