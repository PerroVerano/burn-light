#include "DiscScanner.hpp"
#include <algorithm>
#include <CoreFoundation/CoreFoundation.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <unistd.h>

namespace ac {
static std::string match(const std::string &s, const std::string &pattern, int group = 1) {
    std::smatch m; require(std::regex_search(s, m, std::regex(pattern)), "Cannot parse required media field: " + pattern + "\n" + s);
    return m[group];
}
static void parsePhysical(DiscInfo &d) {
    auto tag=d.media=="CD-R"?"APPENDCD_CD_":"APPENDCD_DVD_";
    auto header = match(d.backendProbe, std::string(tag)+"DISK ([^\\r\\n]+)");
    auto field = [](const std::string &s, const std::string &name) { return number(match(s, "(^| )" + name + "=([0-9]+)( |$)", 2)); };
    require(field(header, "disk_status") == (d.blank ? 0U : 1U), "Physical disk blank/appendable status differs from preflight");
    d.sessionStatus = field(header, "session_status"); d.physicalFirst = field(header, "first"); d.physicalLast = field(header, "last");
    require(d.physicalFirst > 0 && d.physicalLast >= d.physicalFirst && d.physicalLast <= 99, "Invalid physical Track range");
    std::regex records(std::string(tag)+"TRACK ([^\\r\\n]+)");
    for (auto i = std::sregex_iterator(d.backendProbe.begin(), d.backendProbe.end(), records); i != std::sregex_iterator(); ++i) {
        auto s = (*i)[1].str(); PhysicalTrack t;
        t.number=field(s,"number");t.session=field(s,"session");t.start=field(s,"start");t.nwa=field(s,"nwa");t.free=field(s,"free");t.size=field(s,"size");t.lastRecorded=field(s,"last_recorded");
        auto flag = [&](const std::string &name) { auto v=field(s,name);require(v<=1,"Invalid physical Track flag");return v==1; };
        t.reserved=flag("reserved");t.blank=flag("blank");t.damage=flag("damage");t.nwaValid=flag("nwa_valid");t.lraValid=flag("lra_valid");
        t.incremental=field(s,"incremental");t.dataMode=field(s,"datamode");t.trackMode=field(s,"trackmode");
        require(d.media=="CD-R" || t.number == d.physicalFirst+d.physical.size(),"Missing/duplicate physical Track record"); d.physical.push_back(t);
    }
    require(d.media=="CD-R"?d.physical.size()==1:d.physical.size()==d.physicalLast-d.physicalFirst+1,"Incomplete physical Track probe");
}
bool PhysicalTrack::emptyReserved() const {
    return reserved && blank && !damage && !lraValid && incremental==1 && dataMode==1 && trackMode==4 &&
           free==size && size>0 && size<=4096 && size%16==0 && (!nwaValid || nwa==start);
}
void DiscInfo::requireNormalTarget() const {
    require(!blank, "Append requires an existing Session; use init for a blank disc");
    if(media=="CD-R") {
        require(closedSession() && physical.size()==1,"CD-R has an unfinished Session; DVD recovery cannot be used");
        const auto &t=physical.front();
        require(t.blank && !t.reserved && !t.damage && t.nwaValid && t.nwa==next && t.nwa>=t.start &&
                t.free>=freeBlocks && freeBlocks>0 && t.incremental==0,"CD-R has no verified streaming append boundary");
        return;
    }
    require(sessionStatus==0 && physicalFirst==physicalLast && physical.size()==1,
            "An unfinished Session has reserved Tracks; do not retry burn. Inspect it, then use recover FILES");
    const auto &t=physical.front();
    require(t.blank && !t.reserved && !t.damage && !t.lraValid && t.nwaValid && t.nwa==next && t.free==freeBlocks && t.nwa>=t.start,
            "Physical Track has no verified normal append boundary");
}
void DiscInfo::requireInitialTarget() const {
    require(blank && appendable && sessions == 0 && tracks == 0 && last == 0 && next == 0 && readableEnd == 0 &&
            sessionStatus == 0 && physicalFirst == 1 && physicalLast == 1 && physical.size() == 1,
            "Initial recording requires a completely blank CD-R/DVD-R at Track 1 LBA 0");
    const auto &t = physical.front();
    require(t.number == 1 && t.blank && !t.reserved && !t.damage && !t.lraValid && t.nwaValid && t.start == 0 && t.nwa == 0 &&
            t.free >= freeBlocks && freeBlocks > 0 && (media != "CD-R" || t.incremental == 0),
            "Blank disc has no verified initial writable boundary");
}
RecoveryPlan DiscInfo::recoveryPlan() const {
    require(media=="DVD-R","Reserved-Track recovery is for DVD-R only");
    require(sessionStatus==1 && physical.size()>=2 && physical.size()<=3 && physicalLast-physicalFirst==physical.size()-1,
            "Recovery supports only one or two entirely empty reserved Tracks in one unfinished Session");
    uint64_t previousEnd=readableEnd;
    for(size_t i=0;i+1<physical.size();++i) {
        const auto &t=physical[i];
        require(t.emptyReserved() && t.start>previousEnd && uint64_t(t.start)+t.size<=UINT32_MAX,
                "Recovery refuses a nonempty/damaged/inconsistent reserved Track");
        previousEnd=uint64_t(t.start)+t.size-1;
    }
    const auto &tail=physical.back();
    require(tail.blank && !tail.reserved && !tail.damage && !tail.lraValid && tail.nwaValid && tail.nwa==next &&
            tail.nwa>=tail.start && tail.nwa>previousEnd && tail.free==freeBlocks,
            "Recovery has no verified unreserved tail Track");
    return {physicalFirst,physical.front().start,physical.front().size,uint32_t(physical.size()-1)};
}
RecoveryPlan DiscInfo::finishPlan() const {
    require(media=="DVD-R","Reserved-Track finalization is for DVD-R only");
    require(sessionStatus==1 && physical.size()>=2 && physical.size()<=3 && physicalLast-physicalFirst==physical.size()-1,
            "Finalization requires the exact recorded recovery Track and one or two closed padding Tracks");
    const auto &first=physical.front();
    require(last==first.start && first.lraValid && uint64_t(first.lastRecorded)==uint64_t(first.start)+first.size-1,
            "Recovery Track is not completely recorded at the latest Session start");
    uint64_t end=first.start;
    for(size_t i=0;i+1<physical.size();++i) {
        const auto &t=physical[i];
        require(!t.reserved && !t.blank && !t.damage && !t.nwaValid && t.free==0 && t.size>0 && t.size<=4096 && t.size%16==0 &&
                t.incremental==1 && t.dataMode==1 && t.trackMode==4 && t.start>=end && (i==0 || (!t.lraValid && t.lastRecorded==0)),
                "Finalization refuses incomplete/damaged/non-padding Track");
        end=uint64_t(t.start)+t.size;
    }
    const auto &tail=physical.back();
    require(tail.blank && !tail.reserved && !tail.damage && !tail.lraValid && tail.nwaValid && tail.nwa==next && tail.nwa>=end && tail.free==freeBlocks,
            "Finalization has no verified writable tail");
    return {physicalFirst,first.start,first.size,uint32_t(physical.size()-1)};
}
DiscInfo DiscScanner::parseStatus(const DriveIdentity &drive, const std::string &status, bool allowBlank) {
    DiscInfo d; d.drive = drive; d.device = drive.device; d.drutil = status;
    require(!drive.bsdDevice.empty() && match(status,"Name:\\s*(/dev/disk[0-9]+)") == drive.bsdDevice,
            "drutil BSD node does not belong to the selected optical service");
    d.media=match(d.drutil, "Type:\\s*([^\\s]+)");
    require(d.media=="DVD-R" || d.media=="CD-R", "This PoC accepts DVD-R or CD-R");
    require(d.media==drive.mediaType, "Native optical media type differs from drutil");
    require(d.media=="CD-R"?drive.canWriteCDR:drive.canWriteDVDR, "Drive does not report write support for this media type");
    require(d.media!="CD-R" || drive.canTAO,"Drive does not report CD-TAO support");
    auto writability = match(d.drutil, "Writability:[ \\t]*([^\\r\\n]+)");
    d.blank = allowBlank && std::regex_search(writability, std::regex("(^|[, ])blank($|[, ])"));
    if (d.blank) {
        // macOS 在空白 DVD-R 上省略 Sessions/Tracks，必须同时核对 blank 和已用块为零。
        auto optionalCount = [&](const std::string &key) { std::smatch m; return std::regex_search(status,m,std::regex(key+":\\s*([0-9]+)")) ? number(m[1]) : 0U; };
        d.sessions = optionalCount("Sessions"); d.tracks = optionalCount("Tracks");
        d.appendable = std::regex_search(writability, std::regex("(^|[, ])appendable($|[, ])"));
        require(d.appendable && !d.sessions && !d.tracks && number(match(status,"Space Used:[^\n]*blocks:\\s*([0-9]+)")) == 0,
                "Initial recording refuses nonblank or inconsistent media");
    } else {
        d.sessions = number(match(d.drutil, "Sessions:\\s*([0-9]+)")); d.tracks = number(match(d.drutil, "Tracks:\\s*([0-9]+)"));
        d.appendable = match(d.drutil, "Writability:\\s*([^\\s]+)") == "appendable";
        require(d.appendable && d.sessions && d.tracks, "Need an appendable CD-R/DVD-R with a complete initial session");
    }
    d.freeBlocks = number(match(d.drutil, "Space Free:[^\n]*blocks:\\s*([0-9]+)"));
    auto reported=match(status,"Write Speeds:\\s*([^\\r\\n]+)");
    std::regex speed("(^|[ ,])([0-9]+)x");std::vector<uint32_t> speeds;
    for(auto i=std::sregex_iterator(reported.begin(),reported.end(),speed);i!=std::sregex_iterator();++i) {
        auto value=number((*i)[2]);if(value)speeds.push_back(value);
    }
    require(!speeds.empty(),"No supported integer write speed reported for the selected media");
    auto preferred=d.media=="CD-R"?10U:4U;
    d.writeSpeed=std::find(speeds.begin(),speeds.end(),preferred)!=speeds.end()?preferred:*std::min_element(speeds.begin(),speeds.end());
    return d;
}
void DiscScanner::checkWriteCapabilities(DriveIdentity &drive,const std::string &info,const std::string &xml) {
    // Apple drutil 可在当前执行环境中取得 DiscRecording 能力；XML registry path 把其结果绑定到同一 IOKit 服务。
    auto escaped=match(xml,"<ioRegistryEntryPath>([^<]+)</ioRegistryEntryPath>");
    auto text=CFStringCreateWithCString(nullptr,escaped.c_str(),kCFStringEncodingUTF8);
    require(text,"Invalid optical registry path encoding");
    auto decoded=CFXMLCreateStringByUnescapingEntities(nullptr,text,nullptr);CFRelease(text);
    require(decoded,"Cannot decode optical registry path");
    std::vector<char> bytes(CFStringGetMaximumSizeForEncoding(CFStringGetLength(decoded),kCFStringEncodingUTF8)+1);
    bool converted=CFStringGetCString(decoded,bytes.data(),bytes.size(),kCFStringEncodingUTF8);CFRelease(decoded);
    require(converted && drive.registryPath==bytes.data(),"drutil capability query refers to another optical service");
    auto collapse=[](const std::string &s){return std::regex_replace(trim(s),std::regex("\\s+")," ");};
    auto row=match(info,"(^|\n)[ \\t]*Vendor[ \\t]+Product[ \\t]+Rev[ \\t]*\\r?\n([^\\r\\n]+)",2);
    require(collapse(row)==collapse(drive.vendor+" "+drive.product+" "+drive.firmware),"drutil capability identity differs from the native optical target");
    auto writes=[&](const std::string &kind){auto values=match(info,kind+"-Write:[ \\t]*([^\\r\\n]+)");return std::regex_search(values,std::regex("(^|,)\\s*-R\\s*(,|$)"));};
    drive.canWriteCDR=writes("CD");drive.canWriteDVDR=writes("DVD");
    drive.canTAO=std::regex_search(match(info,"Strategies:[ \\t]*([^\\r\\n]+)"),std::regex("(^|,)\\s*CD-TAO\\s*(,|$)"));
}
DiscInfo DiscScanner::preflight(const std::string &selected, bool allowBlank) {
    auto drive=selectOpticalDrive(opticalDrives(),selected);
    require(!drive.bsdDevice.empty(),"Selected optical drive has no readable media; insert an appendable CD-R/DVD-R");
    auto list=checkedRun({"/usr/bin/drutil","list"});
    std::regex rows("(^|\n)\\s*([0-9]+)\\s+[^\\r\\n]+");
    for(auto i=std::sregex_iterator(list.begin(),list.end(),rows);i!=std::sregex_iterator();++i) {
        auto index=(*i)[2].str();auto status=checkedRun({"/usr/bin/drutil","-drive",index,"status"});
        std::smatch node;
        if(!std::regex_search(status,node,std::regex("Name:\\s*(/dev/disk[0-9]+)")) || node[1]!=drive.bsdDevice)continue;
        auto info=checkedRun({"/usr/bin/drutil","-drive",index,"info"});
        auto xml=checkedRun({"/usr/bin/drutil","-drive",index,"info","-xml"});
        checkWriteCapabilities(drive,info,xml);
        // 查询间可能发生设备列表重排；重新取得同一 BSD 节点的状态再接受结果。
        auto d=parseStatus(drive,checkedRun({"/usr/bin/drutil","-drive",index,"status"}),allowBlank);
        d.driveIndex=number(index);d.driveInfo=info;d.driveInfoXml=xml;revalidateDrive(d.drive);return d;
    }
    throw std::runtime_error("Cannot associate selected optical service with drutil; no device will be unmounted");
}
void DiscScanner::checkInquiry(const DriveIdentity &drive,const std::string &s) {
    require(trim(match(s, "Vendor_info\\s*:\\s*'([^']*)'")) == drive.vendor &&
            trim(match(s, "Identifikation\\s*:\\s*'([^']*)'")) == drive.product &&
            trim(match(s, "Revision\\s*:\\s*'([^']*)'")) == drive.firmware &&
            s.find("Removable CD-ROM") != s.npos, "cdrecord INQUIRY target identity mismatch:\n" + s);
}
static std::map<std::string,std::string> opticalEnvironment(const DiscInfo &disc) {
    require(disc.drive.registryId,"Missing stable optical device identity");
    return {{"APPENDCD_DEVICE_REGISTRY_ID",std::to_string(disc.drive.registryId)}};
}
static std::string recorder(const DiscInfo &disc,const std::vector<std::string> &args) {
    return checkedRun(args,60,false,opticalEnvironment(disc));
}
void DiscScanner::applyInitialProbe(DiscInfo &d, const std::string &probe) {
    require(d.blank, "Initial probe cannot authorize a recorded disc");
    d.physical.clear(); // 写前复查复用身份与容量，但 Track 列表必须重新读取，不能累加旧快照。
    d.backendProbe = probe;
    if (d.media == "CD-R") require(std::regex_search(d.backendProbe,std::regex("Supported modes:[^\\r\\n]*\\bTAO\\b")), "Backend does not report CD-TAO support");
    parsePhysical(d);
    if (d.media == "CD-R") {
        auto &t = d.physical.front();
        require(t.number == 1 || t.number == 0 || t.number == 0xFF, "Blank CD-R does not report initial invisible Track");
        t.number = 1;
    }
    require(d.physical.front().free >= d.freeBlocks, "Blank disc capacity differs between drutil and physical Track");
    d.requireInitialTarget();
}
static DiscInfo scanInitial(const DiscInfo &pre) {
    DiscInfo d = pre;
    auto dev = "dev=" + d.device;
    d.inquiry = recorder(d,{INITIAL_CDRECORD_PATH,dev,"-inq"}); DiscScanner::checkInquiry(d.drive,d.inquiry);
    // -atip 会让 upstream 设置介质对应的 write parameters，空白 DVD-R 的
    // streaming/incremental 容量可差数百块。先记录 incremental 容量，再核对流式边界。
    d.atip = recorder(d,{INITIAL_CDRECORD_PATH,dev,"-atip"});
    d.minfo = recorder(d,{INITIAL_CDRECORD_PATH,dev,"-minfo"});
    require(match(d.minfo,"Mounted media type:\\s*([^\n]+)") == (d.media == "CD-R" ? "CD-R" : "DVD-R sequential recording") &&
            std::regex_search(d.minfo,std::regex("[Dd]isk status:\\s*empty")), "Backend does not confirm blank optical media");
    // upstream -minfo 与 -atip 使用的 write parameters 不同；取实测的 incremental 容量与 drutil 的较小值。
    if (d.media == "DVD-R") d.freeBlocks = std::min(d.freeBlocks,number(match(d.atip,"free blocks:[ \\t]*([0-9]+)")));
    DiscScanner::applyInitialProbe(d,recorder(d,{INITIAL_CDRECORD_PATH,dev,"-minfo","-appendcd-probe","-appendcd-initial","appendcd-nwa=0"}));
    d.msinfo = "BLANK_DISC: no previous Session; LBA 0 verified by READ DISC/TRACK INFORMATION\n";
    d.toc = "BLANK_DISC: no recorded TOC\n";
    d.fulltoc = d.toc;
    d.requireInitialTarget();
    return d;
}
void DiscScanner::applySessionAddresses(DiscInfo &d, const std::string &msinfo) {
    require(!d.blank, "Recorded Session addresses cannot authorize a blank disc");
    std::regex pairs("(^|\n)([0-9]+,[0-9]+)[ \\t]*(?=\\r?\n|$)");
    auto begin = std::sregex_iterator(msinfo.begin(), msinfo.end(), pairs);
    require(begin != std::sregex_iterator(), "Missing multisession LBA pair");
    auto pair = (*begin)[2].str();
    require(++begin == std::sregex_iterator(), "Ambiguous multisession LBA pairs");
    auto last = number(pair.substr(0, pair.find(',')));
    auto next = number(pair.substr(pair.find(',') + 1));
    auto minfoLast = number(match(d.minfo, "Last session start address:\\s*([0-9]+)"));
    auto minfoNext = number(match(d.minfo, "Next writable address:\\s*([0-9]+)"));
    auto end = number(match(d.minfo, "Last session leadout start address:\\s*([0-9]+)"));
    // Windows 追加后曾出现 msinfo=0,NWA、minfo=真实最新 Session 的矛盾。
    // 只接受两个独立查询一致的地址；不能降级读取首 Session 或用 GUI 猜测地址。
    require(last == minfoLast && next == minfoNext, "Multisession addresses differ between -msinfo and -minfo");
    require(uint64_t(next) > uint64_t(last) + 32 && end > last && end <= next,
            "Invalid multisession/readable Session boundary");
    d.last = last; d.next = next; d.readableEnd = end;
}
DiscInfo DiscScanner::scan(const DiscInfo &pre, const std::string &selected) {
    require(geteuid() == 0, "Run optical commands with sudo; offline commands do not need root");
    DiscInfo d = pre;
    revalidateDrive(d.drive,false);
    require(selected.empty() || selected==d.device,"Requested device differs from the native optical target");
    if (d.blank) return scanInitial(d);
    auto dev = "dev=" + d.device;
    d.inquiry = recorder(d,{CDRECORD_PATH, dev, "-inq"});checkInquiry(d.drive,d.inquiry);
    d.minfo = recorder(d,{CDRECORD_PATH, dev, "-minfo"});
    require(match(d.minfo, "Mounted media type:\\s*([^\n]+)") == (d.media=="CD-R"?"CD-R":"DVD-R sequential recording"), "cdrecord media type differs from drutil:\n" + d.minfo);
    require(std::regex_search(d.minfo, std::regex("[Dd]isk status:\\s*incomplete")), "cdrecord does not confirm appendability:\n" + d.minfo);
    d.backendProbe = recorder(d,{CDRECORD_PATH, dev, "-minfo", "-appendcd-probe"});
    if(d.media=="CD-R")require(std::regex_search(d.backendProbe,std::regex("Supported modes:[^\\r\\n]*\\bTAO\\b")),"Backend does not report the CD-R TAO mode used by this PoC");
    parsePhysical(d);
    std::string addressDiagnostics;
    for (unsigned attempt = 1; ; ++attempt) {
        d.msinfo = recorder(d,{CDRECORD_PATH, dev, "-msinfo"});
        try { applySessionAddresses(d, d.msinfo); break; }
        catch (const std::exception &e) {
            addressDiagnostics += "Read-only Session address probe " + std::to_string(attempt) + ": " + e.what() + "\n" + d.msinfo;
            // 仅重查只读地址，最多三次；不重试 writer，不切换 init/burn。
            if (attempt == 3) throw std::runtime_error(addressDiagnostics + "\nReference -minfo:\n" + d.minfo);
            std::cerr << "Session address probe rejected; checking again (" << attempt << "/3): " << e.what() << '\n' << d.msinfo;
        }
    }
    d.toc = recorder(d,{CDRECORD_PATH, dev, "-toc"});
    if(d.media=="CD-R") {
        auto completed=number(match(d.toc,"first:\\s*[0-9]+\\s+last\\s+([0-9]+)"));
        require(completed==d.tracks && completed<99,"CD-R TOC differs from completed Track count");
        auto &tail=d.physical.front();
        require(tail.number==completed+1 || tail.number==0xFF || tail.number==0,"CD-R invisible Track differs from TOC");
        tail.number=completed+1;d.physicalFirst=d.physicalLast=tail.number;
        d.freeBlocks=std::min(d.freeBlocks,tail.free);
    }
    d.atip = recorder(d,{CDRECORD_PATH, dev, "-atip"});
    // Full TOC is CD-specific and may be rejected by a DVD drive. Retain that rejection as evidence.
    auto full = run({READCD_PATH, dev, "-fulltoc"});
    d.fulltoc = "exit=" + std::to_string(full.status) + "\n" + full.output;
    return d;
}
void DiscScanner::revalidate(const DiscInfo &disc) {
    revalidateDrive(disc.drive,false);
    if (disc.blank) {
        disc.requireInitialTarget();
        auto current = scanInitial(disc);
        require(current.backendProbe == disc.backendProbe && current.freeBlocks == disc.freeBlocks, "Blank disc physical geometry changed since preparation");
        // 独占访问期间 IOMedia 会重建；物理 blank/profile/NWA/capacity 才是写前判据。
        return;
    }
    auto dev = "dev=" + disc.device;
    checkInquiry(disc.drive,recorder(disc,{CDRECORD_PATH, dev, "-inq"}));
    auto r = recorder(disc,{CDRECORD_PATH, dev, "-msinfo"});
    auto pair = match(r, "(^|\n)([0-9]+,[0-9]+)\\s*(\n|$)", 2);
    require(pair == std::to_string(disc.last) + "," + std::to_string(disc.next), "Media/session changed since image preparation");
    auto media = recorder(disc,{CDRECORD_PATH, dev, "-minfo"});
    require(match(media,"Mounted media type:\\s*([^\n]+)")==(disc.media=="CD-R"?"CD-R":"DVD-R sequential recording") &&
            std::regex_search(media,std::regex("[Dd]isk status:\\s*incomplete")) &&
            number(match(media,"Last session start address:\\s*([0-9]+)"))==disc.last &&
            number(match(media,"Last session leadout start address:\\s*([0-9]+)"))==disc.readableEnd,
            "Media geometry/status changed since image preparation");
    if(disc.media=="DVD-R")require(number(match(media,"Next writable address:\\s*([0-9]+)"))==disc.next &&
            number(match(media,"Remaining writable size:\\s*([0-9]+)"))==disc.freeBlocks,"DVD-R writable capacity changed");
    DiscInfo current;
    current.media=disc.media;
    current.backendProbe=recorder(disc,{CDRECORD_PATH,dev,"-minfo","-appendcd-probe"});parsePhysical(current);
    auto lines=[](const std::string &s) { std::string out;std::istringstream in(s);std::string line;
        while(std::getline(in,line))if(line.starts_with("APPENDCD_DVD_") || line.starts_with("APPENDCD_CD_"))out+=trim(line)+"\n";return out; };
    require(lines(current.backendProbe)==lines(disc.backendProbe),"Physical Track flags/geometry changed since preparation");
}
void DiscInfo::print() const {
    std::cout << "Drive: " << drive.vendor << ' ' << drive.product << ' ' << drive.firmware
              << "\nDevice: " << device << "\nMedia: " << media << "\nSessions: " << sessions << "\nTracks: " << tracks
              << "\nOptical BSD node: " << drive.bsdDevice << "\nRegistry ID: " << drive.registryId << "\nWrite speed: " << writeSpeed << "x"
              << "\nLast session LBA: " << last << "\nNext writable LBA: " << next
              << "\nFree space: " << freeBlocks << " blocks / " << uint64_t(freeBlocks) * BlockSize
              << " bytes\nAppendable: " << (appendable ? "yes" : "no") << "\nBlank: " << (blank ? "yes" : "no")
              << "\nPhysical next Track: " << physicalLast << "\nUnfinished Session: " << (closedSession() ? "no" : "yes") << '\n';
    for(const auto &t:physical)std::cout<<"Track "<<t.number<<": start="<<t.start<<" blocks="<<t.size<<" reserved="<<t.reserved<<" blank="<<t.blank<<" NWA-valid="<<t.nwaValid<<'\n';
}
void DiscInfo::save(const fs::path &dir, const std::string &prefix) const {
    writeText(dir/(prefix+"-device-identity.txt"),"Device: "+device+"\nVendor: "+drive.vendor+"\nProduct: "+drive.product+"\nFirmware: "+drive.firmware+
        "\nBSD node: "+drive.bsdDevice+"\nRegistry path: "+drive.registryPath+"\nRegistry ID: "+std::to_string(drive.registryId)+"\nMedia registry ID: "+std::to_string(drive.mediaRegistryId)+
        "\nMedia: "+media+"\nWrite speed: "+std::to_string(writeSpeed)+"\n");
    for (auto [name, value] : std::vector<std::pair<std::string, std::string>>{{"drutil", drutil}, {"drive-info",driveInfo}, {"drive-info-xml",driveInfoXml}, {"inquiry", inquiry}, {"minfo", minfo}, {"toc", toc}, {"msinfo", msinfo}, {"atip", atip}, {"fulltoc", fulltoc}, {"backend-probe", backendProbe}})
        writeText(dir / (prefix + "-" + name + ".txt"), value);
}
OpticalBlocks::OpticalBlocks(const DiscInfo &disc, const fs::path &dir) : disc_(disc), directory_(dir) {
    fs::create_directories(dir);
    fd_ = ::open(snapshot().c_str(), O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    require(fd_ >= 0, "Cannot create exclusive optical cache");
}
OpticalBlocks::~OpticalBlocks() { if (fd_ >= 0) close(fd_); }
Block OpticalBlocks::read(uint32_t lba) {
    checkInterrupted(); ++reads; Block b{};
    if (!cached_.contains(lba)) prefetch(lba, 1);
    require(pread(fd_, b.data(), b.size(), off_t(lba) * BlockSize) == ssize_t(BlockSize), "Cached block read failed");
    return b;
}
void OpticalBlocks::prefetch(uint32_t lba, uint32_t count) {
    require(uint64_t(lba) + count <= UINT32_MAX, "Optical range overflow");
    for (uint32_t offset = 0; offset < count;) {
        if (cached_.contains(lba + offset)) { ++offset; continue; }
        uint32_t n = std::min<uint32_t>(512, count - offset), begin = lba + offset;
        revalidateDrive(disc_.drive,false);
        auto file = directory_ / "sector.bin"; std::error_code ec; fs::remove(file, ec);
        auto output = checkedRun({READCD_PATH, "dev=" + disc_.device, "sectors=" + std::to_string(begin) + "-" + std::to_string(begin + n), "retries=4", "f=" + file.string()}, 120);
        writeText(directory_ / "last-readcd.txt", output);
        require(fs::file_size(file) == uint64_t(n) * BlockSize, "readcd range size mismatch");
        FileBlocks sector(file);
        std::ofstream index(directory_ / "disc-cache.iso.blocks", std::ios::app);
        for (uint32_t i = 0; i < n; ++i) {
            auto b = sector.read(i);
            require(pwrite(fd_, b.data(), b.size(), off_t(begin + i) * BlockSize) == ssize_t(BlockSize), "Cache write failed");
            cached_.insert(begin + i); index << begin + i << '\n';
        }
        require(bool(index), "Cannot write optical cache validity map");
        offset += n;
    }
}
void OpticalBlocks::verifyCachedMetadata(Blocks &fresh) {
    // 镜像生成结束后用新的 readcd cache 比较已导入的 metadata，避免沿用换盘前的缓存。
    for (auto at = cached_.begin(); at != cached_.end();) {
        uint32_t begin = *at, count = 1;
        auto end = std::next(at);
        while (end != cached_.end() && *end == uint64_t(begin) + count && count < 512) { ++count; ++end; }
        fresh.prefetch(begin, count);
        for (uint32_t i = 0; i < count; ++i) {
            Block expected{};
            require(pread(fd_, expected.data(), expected.size(), off_t(begin + i) * BlockSize) == ssize_t(BlockSize), "Cannot read prepared metadata cache");
            require(fresh.read(begin + i) == expected, "Optical metadata changed since preparation at LBA " + std::to_string(begin + i));
        }
        at = end;
    }
}
std::vector<std::string> recordingOptions(const DiscInfo &disc) {
    require(disc.media=="CD-R" || disc.media=="DVD-R","Unsupported recording media type");
    require(disc.writeSpeed>0,"No verified write speed");
    std::vector<std::string> args{"-multi","fs=8m"};
    if(disc.media=="CD-R")args.insert(args.end(),{"-tao","-xa","-nopad"});
    else args.push_back("-data");
    args.push_back("speed="+std::to_string(disc.writeSpeed));return args;
}
void recordSession(const DiscInfo &disc, const fs::path &image, bool recovery) {
    DiscScanner::revalidate(disc);
    RecoveryPlan plan;
    if(recovery)plan=disc.recoveryPlan();else disc.requireNormalTarget();
    auto size = fs::file_size(image);
    require(size && size % BlockSize == 0 && size / BlockSize + 64 <= disc.freeBlocks, "Image exceeds available writable capacity");
    require(!recovery || size/BlockSize==plan.blocks,"Recovery image must exactly fill reserved Track");
    auto start=recovery?plan.start:disc.next;
    std::cout << "Writing " << disc.media << " multisession at LBA " << start << '\n';
    // DVD-R 使用 incremental recording 和普通 2048 字节 data，不使用 CD-ROM XA。
    std::vector<std::string> args{CDRECORD_PATH, "dev=" + disc.device, "-v"};
    auto mode=recordingOptions(disc);args.insert(args.end(),mode.begin(),mode.end());
    args.push_back("appendcd-nwa="+std::to_string(start));
    if(recovery){args.push_back("-appendcd-reuse");args.push_back("appendcd-track="+std::to_string(plan.track));args.push_back("appendcd-reserved-count="+std::to_string(plan.reservedCount));}
    if (disc.media=="DVD-R" && disc.minfo.find("Drive returns zero media size. Using media size from ADIP.") != disc.minfo.npos) {
        auto wrong = number(match(disc.minfo,"Phys disk size ([0-9]+) differs from rzone size 0"));
        require(wrong < disc.next && disc.freeBlocks > size/BlockSize+64,
                "Unexpected capacity warning; no -ignsize workaround permitted");
        // 仅覆盖本机已证实的 cdrtools ADIP 错误；实际容量仍由 drutil + READ TRACK INFORMATION 双重限制。
        args.push_back("-ignsize");
        std::cerr << "Capacity workaround: cdrecord ADIP=" << wrong << ", verified NWA=" << disc.next
                  << ", remaining=" << disc.freeBlocks << "; using -ignsize for this diagnosed backend error\n";
    }
    args.push_back(image.string());
    auto result = run(args,1800,true,opticalEnvironment(disc));
    writeText(image.parent_path()/"cdrecord-burn.txt","exit="+std::to_string(result.status)+"\n"+result.output);
    require(result.status==0,"cdrecord physical write failed ("+std::to_string(result.status)+"):\n"+result.output);
}
void recordInitialSession(const DiscInfo &disc, const fs::path &image) {
    disc.requireInitialTarget();
    auto size = fs::file_size(image);
    require(size && size % BlockSize == 0 && size / BlockSize + 64 <= disc.freeBlocks, "Initial image exceeds blank-disc capacity");
    DiscScanner::revalidate(disc);
    // 首刻后端独立部署，已验收的追加刻录继续使用 media6 的原二进制。
    std::vector<std::string> args{INITIAL_CDRECORD_PATH,"dev=" + disc.device,"-v","-appendcd-initial","appendcd-nwa=0"};
    auto mode = recordingOptions(disc); args.insert(args.end(),mode.begin(),mode.end());
    args.push_back(image.string());
    auto result = run(args,1800,true,opticalEnvironment(disc));
    writeText(image.parent_path()/"cdrecord-init.txt","exit=" + std::to_string(result.status) + "\n" + result.output);
    require(result.status == 0,"cdrecord initial write failed; inspect the disc before any further operation:\n" + result.output);
}
void finalizeSession(const DiscInfo &disc,const fs::path &evidence) {
    DiscScanner::revalidate(disc);auto plan=disc.finishPlan();
    // 没有 image 参数；只关闭已经读回验证过的 unfinished Session，不能再次写 payload。
    auto result=run({CDRECORD_PATH,"dev="+disc.device,"-v","-fix","-multi","-appendcd-reuse","-appendcd-finish",
                     "appendcd-nwa="+std::to_string(plan.start),"appendcd-track="+std::to_string(plan.track),
                     "appendcd-reserved-count="+std::to_string(plan.reservedCount)},1800,true,opticalEnvironment(disc));
    writeText(evidence/"cdrecord-finalize.txt","exit="+std::to_string(result.status)+"\n"+result.output);
    require(result.status==0,"cdrecord recovery Session closure failed; do not retry burn:\n"+result.output);
}
}
