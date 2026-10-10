#include "DiscScanner.hpp"
#include <algorithm>
#include <iostream>

using namespace ac;
template<class F> static void rejected(F f) {
    bool failed=false;try {f();} catch(const std::exception &) {failed=true;}
    require(failed,"Expected optical target rejection");
}
static DriveIdentity drive() {
    DriveIdentity d;d.vendor="OtherVendor";d.product="Different Writer";d.firmware="A123";
    d.device="1,0,0";d.registryId=4294967297ULL;d.mediaRegistryId=4294967300ULL;
    d.bsdDevice="/dev/disk7";d.mediaType="CD-R";d.canWriteCDR=d.canWriteDVDR=d.canTAO=true;d.registryPath="IOService:/USB & optical/Writer";return d;
}
static std::string status(const std::string &media="CD-R",const std::string &speeds="8x, 16x") {
    return "Type: "+media+" Name: /dev/disk7\nSessions: 1 Tracks: 1\nWrite Speeds: "+speeds+
           "\nSpace Free: 70:00:00 blocks: 300000\nWritability: appendable\n";
}
static bool has(const std::vector<std::string> &a,const std::string &value) {return std::find(a.begin(),a.end(),value)!=a.end();}
int main() {
    try {
        auto a=drive();auto b=a;b.registryId++;b.mediaRegistryId++;b.device="1,2,0";b.bsdDevice="/dev/disk8";
        auto capabilities=" Vendor Product Rev\n OtherVendor Different Writer A123\n CD-Write: -R, -RW\n DVD-Write: -R, -R DL, +R\n Strategies: CD-TAO, DVD-DAO\n";
        auto xml="<ioRegistryEntryPath>IOService:/USB &amp; optical/Writer</ioRegistryEntryPath>";
        DiscScanner::checkWriteCapabilities(a,capabilities,xml);require(a.canWriteCDR&&a.canWriteDVDR&&a.canTAO,"Native write-capability parsing failed");
        rejected([&]{DiscScanner::checkWriteCapabilities(a,capabilities,"<ioRegistryEntryPath>IOService:/Other</ioRegistryEntryPath>");});
        rejected([&]{selectOpticalDrive({});});
        require(selectOpticalDrive({a}).registryId==a.registryId,"Single drive was not selected");
        rejected([&]{selectOpticalDrive({a,b});});
        require(selectOpticalDrive({a,b},"1,2,0").registryId==b.registryId,"Identical-model explicit selection failed");
        rejected([&]{selectOpticalDrive({a,b},"1,4,0");});
        rejected([&]{selectOpticalDrive({a},"/dev/disk0");});
        requireSameDrive(a,a,true);
        rejected([&]{requireSameDrive(a,b,true);});
        auto changed=a;changed.registryId++;rejected([&]{requireSameDrive(a,changed,true);});
        changed=a;changed.firmware="B234";rejected([&]{requireSameDrive(a,changed,true);});
        changed=a;changed.mediaRegistryId++;requireSameDrive(a,changed,true);
        changed.bsdDevice="/dev/disk8";rejected([&]{requireSameDrive(a,changed,true);});
        requireSameDrive(a,changed,false); // 写后重新注册的介质可以变化，光驱身份仍必须相同。
        auto cd=DiscScanner::parseStatus(a,status());require(cd.writeSpeed==8,"Unsupported fixed CD speed was reused");
        cd.minfo="Last session start address: 33464\nLast session leadout start address: 33764\nNext writable address: 40666\n";
        DiscScanner::applySessionAddresses(cd,"backend diagnostic\n33464,40666\n");
        require(cd.last==33464 && cd.next==40666 && cd.readableEnd==33764,"Consistent latest Session addresses were not accepted");
        for (const auto &pair : {"0,40666\n", "33464,40667\n", "33464,40666\n33464,40666\n", "invalid\n", "4294967296,40666\n"}) {
            rejected([&]{DiscScanner::applySessionAddresses(cd,pair);});
            require(cd.last==33464 && cd.next==40666 && cd.readableEnd==33764,"Rejected address probe changed the accepted geometry");
        }
        auto badBoundary=cd;badBoundary.minfo="Last session start address: 33464\nLast session leadout start address: 40667\nNext writable address: 40666\n";
        rejected([&]{DiscScanner::applySessionAddresses(badBoundary,"33464,40666\n");});
        auto flags=recordingOptions(cd);require(has(flags,"-tao")&&has(flags,"-xa")&&has(flags,"-multi")&&!has(flags,"-data")&&has(flags,"speed=8"),"CD mode selection failed");
        require(DiscScanner::parseStatus(a,status("CD-R","10x, 16x")).writeSpeed==10,"Validated CD default changed");
        auto dvd=a;dvd.mediaType="DVD-R";
        auto info=DiscScanner::parseStatus(dvd,status("DVD-R","6x, 8x"));require(info.writeSpeed==6,"DVD supported speed selection failed");
        flags=recordingOptions(info);require(has(flags,"-data")&&has(flags,"-multi")&&!has(flags,"-xa")&&!has(flags,"-tao"),"DVD mode selection failed");
        require(DiscScanner::parseStatus(dvd,status("DVD-R","4x, 8x")).writeSpeed==4,"Validated DVD default changed");
        changed=a;changed.bsdDevice="/dev/disk8";rejected([&]{DiscScanner::parseStatus(changed,status());});
        changed=a;changed.canWriteCDR=false;rejected([&]{DiscScanner::parseStatus(changed,status());});
        changed=a;changed.canTAO=false;rejected([&]{DiscScanner::parseStatus(changed,status());});
        rejected([&]{DiscScanner::parseStatus(a,status("DVD-R"));});
        rejected([&]{DiscScanner::parseStatus(a,status("CD-RW"));});
        auto empty=status();empty.replace(empty.find("Sessions: 1"),11,"Sessions: 0");rejected([&]{DiscScanner::parseStatus(a,empty);});
        auto blank="Type: DVD-R Name: /dev/disk7\nWrite Speeds: 3x, 4x, 8x\nSpace Used: blocks: 0\nSpace Free: blocks: 2297888\nWritability: appendable, blank, overwritable\n";
        rejected([&]{DiscScanner::parseStatus(dvd,blank);});
        auto first=DiscScanner::parseStatus(dvd,blank,true);require(first.blank && first.sessions==0 && first.tracks==0 && first.writeSpeed==4,"Blank DVD-R preflight failed");
        first.physicalFirst=first.physicalLast=1;PhysicalTrack t;t.number=1;t.blank=t.nwaValid=true;t.free=first.freeBlocks;first.physical.push_back(t);
        first.requireInitialTarget();rejected([&]{first.requireNormalTarget();});
        first.physical.front().nwa=16;rejected([&]{first.requireInitialTarget();});
        first.physical.front().nwa=0;first.physical.front().damage=true;rejected([&]{first.requireInitialTarget();});
        auto probe="APPENDCD_DVD_DISK disk_status=0 session_status=0 first=1 last=1\nAPPENDCD_DVD_TRACK number=1 session=1 reserved=0 blank=1 damage=0 nwa_valid=1 start=0 nwa=0 free=2297888 size=2297888 last_recorded=0 lra_valid=0 incremental=0 datamode=1 trackmode=4\n";
        for (int i=0;i<3;++i) { DiscScanner::applyInitialProbe(first,probe);require(first.physical.size()==1,"Repeated initial probe accumulated old Tracks"); }
        auto existing=DiscScanner::parseStatus(dvd,status("DVD-R"));rejected([&]{DiscScanner::applyInitialProbe(existing,probe);});
        require(!DiscScanner::parseStatus(a,status(),true).blank,"Allow-blank misclassified an existing disc");
        DiscScanner::checkInquiry(a,"Device type: Removable CD-ROM\nVendor_info: 'OtherVendor'\nIdentifikation: 'Different Writer'\nRevision: 'A123'\n");
        rejected([&]{DiscScanner::checkInquiry(a,"Device type: Fixed Disk\nVendor_info: 'OtherVendor'\nIdentifikation: 'Different Writer'\nRevision: 'A123'\n");});
        std::cout<<"Automatic optical selection, identity/media/write-capability and CD/DVD parameter checks passed; no hardware accessed.\n";
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
