#pragma once
#include "Util.hpp"

namespace ac {
struct DriveIdentity {
    std::string vendor, product, firmware, device, bsdDevice, mediaType, registryPath;
    uint64_t registryId = 0, mediaRegistryId = 0;
    bool canWriteCDR = false, canWriteDVDR = false, canTAO = false;
};
std::vector<DriveIdentity> opticalDrives();
DriveIdentity selectOpticalDrive(const std::vector<DriveIdentity> &drives, const std::string &selected = "");
DriveIdentity waitForOpticalMedia(const DriveIdentity &selected,
                                 const std::function<std::vector<DriveIdentity>()> &discover = opticalDrives,
                                 const std::function<void()> &pause = {}, unsigned attempts = 40);
void requireSameDrive(const DriveIdentity &expected, const DriveIdentity &current, bool sameMedium);
void revalidateDrive(const DriveIdentity &drive, bool sameMedium = true);
void printDrives(const std::vector<DriveIdentity> &drives);
}
