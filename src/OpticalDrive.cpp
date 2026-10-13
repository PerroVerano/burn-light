#include "OpticalDrive.hpp"
#include <algorithm>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <iostream>
#include <regex>
#include <unistd.h>

namespace ac {
namespace {
struct IOHandle {
    io_object_t value = 0;
    ~IOHandle() { if (value) IOObjectRelease(value); }
};
struct CFHandle {
    CFTypeRef value = nullptr;
    ~CFHandle() { if (value) CFRelease(value); }
};
std::string cfString(CFTypeRef value) {
    if (!value || CFGetTypeID(value) != CFStringGetTypeID()) return {};
    auto s = static_cast<CFStringRef>(value);
    std::vector<char> b(CFStringGetMaximumSizeForEncoding(CFStringGetLength(s), kCFStringEncodingUTF8) + 1);
    require(CFStringGetCString(s, b.data(), b.size(), kCFStringEncodingUTF8), "Cannot decode optical device identity");
    return trim(b.data());
}
std::string dictionaryString(CFTypeRef dictionary, CFStringRef key) {
    if (!dictionary || CFGetTypeID(dictionary) != CFDictionaryGetTypeID()) return {};
    return cfString(CFDictionaryGetValue(static_cast<CFDictionaryRef>(dictionary), key));
}
std::vector<DriveIdentity> enumerate() {
    // 仅枚举 cdrecord 原生 transport 使用的光学服务类，普通磁盘永远不进入候选集。
    const char *classes[]{"IOCompactDiscServices", "IODVDServices", "IOBDServices"};
    std::map<uint64_t, DriveIdentity> unique;
    for (int bus = 0; bus < 3; ++bus) {
        IOHandle services;
        require(IOServiceGetMatchingServices(kIOMainPortDefault, IOServiceMatching(classes[bus]), &services.value) == KERN_SUCCESS,
                "Cannot enumerate native optical services");
        for (int index = 0;; ++index) {
            IOHandle service{IOIteratorNext(services.value)};
            if (!service.value) break;
            DriveIdentity d;
            require(IORegistryEntryGetRegistryEntryID(service.value, &d.registryId) == KERN_SUCCESS && d.registryId,
                    "Cannot obtain stable optical registry identity");
            io_string_t path{};
            require(IORegistryEntryGetPath(service.value,kIOServicePlane,path)==KERN_SUCCESS,"Cannot obtain optical registry path");
            d.registryPath=path;
            // 上游 3.02a09 的 iokit_open 循环内外均递增 idx；保留其现有地址编号，不能按 drutil 顺序猜测。
            require(index < 8, "cdrtools IOKit transport supports at most eight optical devices per service class");
            d.device = std::to_string(bus) + "," + std::to_string(index * 2) + ",0";
            CFHandle characteristics{IORegistryEntryCreateCFProperty(service.value, CFSTR("Device Characteristics"), nullptr, 0)};
            d.vendor = dictionaryString(characteristics.value, CFSTR("Vendor Name"));
            d.product = dictionaryString(characteristics.value, CFSTR("Product Name"));
            d.firmware = dictionaryString(characteristics.value, CFSTR("Product Revision Level"));
            require(!d.product.empty(), "Optical device has no usable product identity");
            IOHandle children;
            require(IORegistryEntryCreateIterator(service.value, kIOServicePlane, kIORegistryIterateRecursively, &children.value) == KERN_SUCCESS,
                    "Cannot associate optical service with its media");
            for (;;) {
                IOHandle child{IOIteratorNext(children.value)};
                if (!child.value) break;
                if (!IOObjectConformsTo(child.value, "IOCDMedia") && !IOObjectConformsTo(child.value, "IODVDMedia") &&
                    !IOObjectConformsTo(child.value, "IOBDMedia")) continue;
                CFHandle whole{IORegistryEntryCreateCFProperty(child.value, CFSTR("Whole"), nullptr, 0)};
                if (whole.value != kCFBooleanTrue) continue;
                CFHandle name{IORegistryEntryCreateCFProperty(child.value, CFSTR("BSD Name"), nullptr, 0)};
                auto node = cfString(name.value);
                if (node.empty()) continue;
                // 独占访问后的旧 IOMedia 可尚未退出 registry；只接受 BSD 当前发布的同一对象。
                IOHandle publishedMatches;
                require(IOServiceGetMatchingServices(kIOMainPortDefault, IOBSDNameMatching(kIOMainPortDefault, 0, node.c_str()), &publishedMatches.value)==KERN_SUCCESS,
                        "Cannot resolve published optical BSD node");
                IOHandle published{IOIteratorNext(publishedMatches.value)};
                IOHandle duplicate{IOIteratorNext(publishedMatches.value)};
                require(!duplicate.value,"Ambiguous published optical BSD node");
                uint64_t childId=0, publishedId=0;
                require(IORegistryEntryGetRegistryEntryID(child.value,&childId)==KERN_SUCCESS && childId,
                        "Cannot obtain optical media identity");
                if(!published.value)continue;
                require(IORegistryEntryGetRegistryEntryID(published.value,&publishedId)==KERN_SUCCESS && publishedId,
                        "Cannot obtain published optical media identity");
                if(childId!=publishedId)continue;
                require(std::regex_match(node, std::regex("disk[0-9]+")) && d.bsdDevice.empty(), "Ambiguous optical whole-media BSD node");
                d.bsdDevice = "/dev/" + node;
                CFHandle type{IORegistryEntryCreateCFProperty(child.value, CFSTR("Type"), nullptr, 0)};
                d.mediaType = cfString(type.value);
                d.mediaRegistryId=childId;
            }
            // 同一服务若匹配多个类，只保留最具体的光学类，避免重复候选。
            unique[d.registryId] = d;
        }
    }
    std::vector<DriveIdentity> result;
    for (const auto &[id, d] : unique) result.push_back(d);
    return result;
}
}
std::vector<DriveIdentity> opticalDrives() { return enumerate(); }
DriveIdentity selectOpticalDrive(const std::vector<DriveIdentity> &drives, const std::string &selected) {
    require(!drives.empty(), "No native optical drive detected");
    if (!selected.empty()) {
        require(std::regex_match(selected, std::regex("[0-2],[0-9]+,0")), "--device needs an optical bus,target,lun address; use appendcd drives");
        auto i = std::find_if(drives.begin(), drives.end(), [&](const auto &d) { return d.device == selected; });
        require(i != drives.end(), "Selected address is not a discovered optical drive; use appendcd drives");
        return *i;
    }
    std::string choices;
    for (const auto &d : drives) choices += "\n  " + d.device + " " + d.vendor + " " + d.product + " " + d.firmware;
    require(drives.size() == 1, "Multiple optical drives detected; specify --device bus,target,lun:" + choices);
    return drives.front();
}
void requireSameDrive(const DriveIdentity &expected, const DriveIdentity &current, bool sameMedium) {
    require(expected.registryId && current.registryId == expected.registryId && expected.device == current.device &&
            expected.vendor == current.vendor && expected.product == current.product && expected.firmware == current.firmware &&
            expected.registryPath==current.registryPath,
            "Optical device changed or its cdrtools address was reassigned; rescan before proceeding");
    // 获得/释放 SCSI 独占访问会重建 IOMedia，不能把它的 registry ID 当作盘片身份。
    if (sameMedium) require(!expected.bsdDevice.empty() && expected.bsdDevice == current.bsdDevice && expected.mediaType == current.mediaType,
                            "Optical media changed since preparation");
}
DriveIdentity waitForOpticalMedia(const DriveIdentity &selected,
                                 const std::function<std::vector<DriveIdentity>()> &discover,
                                 const std::function<void()> &pause, unsigned attempts) {
    require(selected.registryId && attempts<=40,"Invalid optical media wait binding/limit");
    if(!selected.bsdDevice.empty())return selected;
    // SCSI 独占访问结束后 IOMedia 异步重新发布；只等待同一服务，不重试任何 writer。
    std::cerr<<"Waiting for selected optical media publication before preflight\n";
    for(unsigned i=0;i<attempts;++i) {
        checkInterrupted();
        if(pause)pause();else usleep(250000);
        checkInterrupted();
        auto current=selectOpticalDrive(discover(),selected.device);
        requireSameDrive(selected,current,false);
        if(!current.bsdDevice.empty())return current;
    }
    throw std::runtime_error("Selected optical drive has no readable media; insert an appendable CD-R/DVD-R");
}
void revalidateDrive(const DriveIdentity &drive, bool sameMedium) {
    auto current = selectOpticalDrive(enumerate(), drive.device);
    requireSameDrive(drive, current, sameMedium);
}
void printDrives(const std::vector<DriveIdentity> &drives) {
    require(!drives.empty(), "No native optical drive detected");
    for (const auto &d : drives) std::cout << "Device: " << d.device << "\nDrive: " << d.vendor << ' ' << d.product << ' ' << d.firmware
        << "\nOptical BSD node: " << (d.bsdDevice.empty() ? "none" : d.bsdDevice) << "\nMedia: " << (d.mediaType.empty() ? "none" : d.mediaType)
        << "\nRegistry ID: " << d.registryId << '\n';
}
}
