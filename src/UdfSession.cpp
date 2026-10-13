#include "UdfSession.hpp"
#include "UdfVat.hpp"
#include <CoreFoundation/CoreFoundation.h>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <fcntl.h>
#include <unistd.h>
#include <ctime>

namespace ac {
namespace {
uint16_t u16(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
uint32_t u32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint64_t u64(const uint8_t *p) { return u32(p) | uint64_t(u32(p + 4)) << 32; }
void put16(uint8_t *p, uint16_t n) { p[0] = n; p[1] = n >> 8; }
void put32(uint8_t *p, uint32_t n) { for (int i = 0; i < 4; ++i) p[i] = n >> (8 * i); }
void put64(uint8_t *p, uint64_t n) { put32(p, uint32_t(n)); put32(p + 4, uint32_t(n >> 32)); }
uint16_t crc16(const uint8_t *p, size_t n) {
    uint16_t crc = 0;
    for (size_t i = 0; i < n; ++i) { crc ^= uint16_t(p[i]) << 8; for (int j = 0; j < 8; ++j) crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ 0x1021) : uint16_t(crc << 1); }
    return crc;
}
void tag(Block &b, uint16_t id, uint32_t location, uint16_t length, uint16_t version = 2) {
    require(length <= BlockSize - 16, "UDF CRC length overflow");
    put16(b.data(), id); put16(b.data() + 2, version); b[4] = 0; b[5] = 0; put16(b.data() + 6, 1);
    put16(b.data() + 8, crc16(b.data() + 16, length)); put16(b.data() + 10, length); put32(b.data() + 12, location);
    uint8_t sum = 0; for (int i = 0; i < 16; ++i) if (i != 4) sum += b[i]; b[4] = sum;
}
void validTag(const Block &b, uint16_t id, uint32_t location) {
    require(u16(b.data()) == id && u16(b.data() + 2) == 2 && u32(b.data() + 12) == location, "UDF tag identity/version/location mismatch");
    uint8_t sum = 0; for (int i = 0; i < 16; ++i) if (i != 4) sum += b[i];
    auto length = u16(b.data() + 10);
    require(sum == b[4] && length <= BlockSize - 16 && crc16(b.data() + 16, length) == u16(b.data() + 8), "UDF tag checksum/CRC mismatch");
}
void timestamp(uint8_t *p) {
    std::time_t now = std::time(nullptr); std::tm value{}; gmtime_r(&now, &value);
    memset(p, 0, 12); put16(p, 0x1000); put16(p + 2, value.tm_year + 1900);
    p[4] = value.tm_mon + 1; p[5] = value.tm_mday; p[6] = value.tm_hour; p[7] = value.tm_min; p[8] = value.tm_sec;
}
void entity(uint8_t *p) { memset(p, 0, 32); memcpy(p + 1, "*appendcd", 9); }
void longAd(uint8_t *p, uint32_t location) { memset(p, 0, 16); put32(p, BlockSize); put32(p + 4, location); }
std::vector<uint8_t> encodedName(const std::string &name) {
    auto s = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8 *>(name.data()), name.size(), kCFStringEncodingUTF8, false);
    require(s, "Invalid UTF-8 for UDF name");
    std::vector<uint8_t> out(1 + 2 * CFStringGetLength(s)); out[0] = 16; CFIndex used = 0;
    auto converted = CFStringGetBytes(s, CFRangeMake(0, CFStringGetLength(s)), kCFStringEncodingUTF16BE, 0, false, out.data() + 1, out.size() - 1, &used);
    auto length = CFStringGetLength(s); CFRelease(s);
    require(converted == length && size_t(used) == out.size() - 1 && out.size() <= 255, "UDF name cannot be encoded"); return out;
}
struct Layout {
    uint32_t partition = 0;
    Block pvd{}, iuvd{}, lvd{}, pd{}, usd{}, term{}, fsd{}, root{}, integrity{};
};
Layout readLayout(Blocks &source, uint32_t last) {
    auto anchor = source.read(last + 256); validTag(anchor, 2, last + 256);
    uint32_t begin = u32(anchor.data() + 20), bytes = u32(anchor.data() + 16);
    require(bytes && bytes <= 32 * BlockSize && bytes % BlockSize == 0, "Unsupported UDF VDS range");
    Layout layout; std::set<uint16_t> found;
    for (uint32_t i = 0; i < bytes / BlockSize; ++i) {
        auto b = source.read(begin + i); auto id = u16(b.data()); validTag(b, id, begin + i); found.insert(id);
        if (id == 1) layout.pvd = b; else if (id == 4) layout.iuvd = b; else if (id == 5) layout.pd = b;
        else if (id == 6) layout.lvd = b; else if (id == 7) layout.usd = b; else if (id == 8) { layout.term = b; break; }
        else throw std::runtime_error("Unsupported UDF VDS descriptor");
    }
    require(found == std::set<uint16_t>({1,4,5,6,7,8}), "Need complete simple Windows UDF 1.02 VDS");
    require(u16(layout.lvd.data() + 240) == 0x0102 && u32(layout.lvd.data() + 212) == BlockSize &&
            u32(layout.lvd.data() + 264) == 6 && u32(layout.lvd.data() + 268) == 1 && layout.lvd[440] == 1 && layout.lvd[441] == 6 &&
            u16(layout.lvd.data() + 444) == u16(layout.pd.data() + 22) && u32(layout.pd.data() + 184) == 1,
            "UDF PoC supports only 1.02 / one read-only physical partition / type-1 map");
    layout.partition = u32(layout.pd.data() + 188);
    require(u16(layout.lvd.data() + 256) == 0, "UDF FSD map is not partition zero");
    auto fsdRel = u32(layout.lvd.data() + 252); layout.fsd = source.read(layout.partition + fsdRel); validTag(layout.fsd, 256, fsdRel);
    require(u16(layout.fsd.data() + 408) == 0, "UDF root uses another partition");
    auto rootRel = u32(layout.fsd.data() + 404); layout.root = source.read(layout.partition + rootRel); validTag(layout.root, 261, rootRel);
    auto integrityLba = u32(layout.lvd.data() + 436); layout.integrity = source.read(integrityLba); validTag(layout.integrity, 9, integrityLba);
    require(u32(layout.integrity.data() + 72) == 1 && u32(layout.integrity.data() + 76) >= 46, "Unsupported UDF integrity layout");
    return layout;
}
void merge(const fs::path &folder, const std::string &prefix, Manifest &m, std::map<std::string, fs::path> &sources, ConflictPolicy policy, int depth = 0) {
    require(depth < 128 && fs::is_directory(fs::symlink_status(folder)), "Invalid UDF input directory/depth");
    std::map<std::string, std::string> names;
    for (const auto &[path, entry] : m.entries) {
        (void)entry; auto slash = path.rfind('/'); auto parent = slash == path.npos ? "" : path.substr(0, slash + 1);
        if (parent == prefix) { auto leaf = path.substr(prefix.size()); names.emplace(windowsKey(leaf), leaf); }
    }
    for (const auto &item : fs::directory_iterator(folder)) {
        checkInterrupted(); auto leaf = item.path().filename().string(); auto status = item.symlink_status();
        // 所有层级都在冲突检查前跳过 Finder 缓存，不改动已经导入的旧目录树。
        if (leaf == ".DS_Store" && fs::is_regular_file(status)) continue;
        validateName(leaf); auto key = windowsKey(leaf);
        require(!names.contains(key) || names[key] == leaf, "UDF case/Unicode collision: " + prefix + leaf);
        auto path = prefix + leaf;
        require(fs::is_directory(status) || fs::is_regular_file(status), "UDF merge rejects symlinks/special files");
        auto existing = m.entries.find(path);
        if (fs::is_directory(status)) {
            require(existing == m.entries.end() || existing->second.directory, "UDF directory/file conflict");
            if (existing == m.entries.end()) { Entry e; e.directory = true; m.entries.emplace(path, e); }
            merge(item.path(), path + "/", m, sources, policy, depth + 1);
        } else {
            if (existing != m.entries.end()) {
                require(!existing->second.directory, "UDF directory/file conflict: " + path);
                require(policy != ConflictPolicy::Reject, "Existing file conflict (no replacement): " + path);
                if (policy == ConflictPolicy::Skip) continue;
            }
            Entry e; e.size = item.file_size(); require(e.size < 0x40000000, "UDF PoC new file limit is below 1 GiB"); e.sha = shaFile(item.path());
            m.entries.insert_or_assign(path, e); sources.emplace(path, item.path());
        }
        names[key] = leaf; require(m.entries.size() < 100000, "UDF entry limit");
    }
}
Block fid(const std::string &name, bool directory, uint32_t icb, uint32_t location, bool parent = false) {
    Block b{}; auto bytes = parent ? std::vector<uint8_t>{} : encodedName(name);
    put16(b.data() + 16, 1); b[18] = uint8_t((directory ? 2 : 0) | (parent ? 8 : 0)); b[19] = uint8_t(bytes.size());
    longAd(b.data() + 20, icb);
    // 每条 FID 占一块。Implementation Use 的首 32 字节是 regid，消除跨块 tag/record 边界。
    uint16_t padding = uint16_t((4 - (38 + bytes.size()) % 4) % 4);
    uint16_t impl = uint16_t(BlockSize - 38 - bytes.size() - padding); put16(b.data() + 36, impl); entity(b.data() + 38);
    memcpy(b.data() + 38 + impl, bytes.data(), bytes.size()); tag(b, 257, location, BlockSize - 16); return b;
}
void validateTree(Blocks &source, const Layout &layout, uint32_t start, uint32_t end, uint32_t icb,
                  uint32_t parent, const std::string &path, std::set<uint32_t> &visited, Tree &tree, int depth=0) {
    require(depth < 128 && visited.insert(icb).second && visited.size() < 100000, "UDF ICB cycle/resource limit");
    require(icb >= start && icb < end, "New UDF ICB refers outside new session");
    auto fe=source.read(icb);validTag(fe,261,icb-layout.partition);
    require(u16(fe.data()+20)==4 && u16(fe.data()+34)==0 && u32(fe.data()+168)==0, "Unsupported generated UDF FE/AD type");
    require(u32(fe.data()+172)%8==0 && u32(fe.data()+172)<=BlockSize-176 && u16(fe.data()+10)==160+u32(fe.data()+172), "UDF FE CRC/AD length differs");
    Entry entry;entry.directory=fe[27]==4;require(entry.directory || fe[27]==5, "Invalid UDF FE file type");entry.size=u64(fe.data()+56);
    uint64_t total=0,recorded=0;
    for(uint32_t offset=176;offset<176+u32(fe.data()+172);offset+=8) {
        uint32_t bytes=u32(fe.data()+offset),relative=u32(fe.data()+offset+4);
        require(bytes && bytes<0x40000000 && uint64_t(layout.partition)+relative+(uint64_t(bytes)+BlockSize-1)/BlockSize<=end,"UDF allocation descriptor outside partition");
        entry.sections.push_back({layout.partition+relative,bytes});total+=bytes;recorded+=(uint64_t(bytes)+BlockSize-1)/BlockSize;
    }
    require(total==entry.size && recorded==u64(fe.data()+64),"UDF FE size/block accounting differs");
    if(!path.empty())require(tree.emplace(path,entry).second,"Duplicate UDF path");
    if(!entry.directory)return;
    require(entry.sections.size()==1 && entry.sections[0].size%BlockSize==0 && entry.sections[0].block>=start,"Generated UDF directory range invalid");
    std::map<std::string,std::string> names;uint32_t subdirs=0;
    for(uint32_t i=0;i<entry.sections[0].size/BlockSize;++i) {
        auto block=entry.sections[0].block+i;auto b=source.read(block);validTag(b,257,block-layout.partition);
        require(u16(b.data()+16)==1 && u16(b.data()+28)==0 && u32(b.data()+20)==BlockSize,"UDF FID version/map/ICB size mismatch");
        auto len=b[19];auto impl=u16(b.data()+36);require(38+uint32_t(impl)+len<=BlockSize,"UDF FID identifier overflow");
        uint32_t child=u32(b.data()+24)+layout.partition;
        if(i==0) {require(b[18]==10 && !len && child==parent,"UDF parent FID mismatch");continue;}
        require((b[18]==0 || b[18]==2) && len>=3 && b[38+impl]==16,"UDF child FID flags/name encoding mismatch");
        auto name=utf16Name(b.data()+39+impl,len-1);validateName(name);
        require(names.emplace(windowsKey(name),name).second,"Generated UDF case collision");
        auto full=path.empty()?name:path+"/"+name;auto before=tree.size();
        validateTree(source,layout,start,end,child,icb,full,visited,tree,depth+1);
        require(tree.size()>before && tree.at(full).directory==(b[18]==2),"UDF FID/FE type mismatch");
        if(b[18]==2)++subdirs;
    }
    require(u16(fe.data()+48)==1+subdirs,"UDF directory link count mismatch");
}
}
static Manifest buildVatSession(std::shared_ptr<Blocks> source, uint32_t last, uint32_t next, uint32_t readableEnd,
                                const fs::path &files, const fs::path &image, const std::string &sourcePath,
                                uint32_t sourceBase, ConflictPolicy policy) {
    auto v=readVatVolume(*source,last,readableEnd);
    Manifest m;m.filesystem="udf2.01+vat";m.importedStart=last;m.start=next;m.sourcePath=sourcePath;m.sourceBase=sourceBase;m.entries=v.tree.entries;
    std::map<std::string,fs::path> sources;merge(files,"",m,sources,policy);require(!sources.empty(),"Nothing new to append");
    auto vat=v.vat;auto fsdVirtual=u32(v.lvd.data()+252);
    require(vat.size()<1000000 && fsdVirtual<vat.size(),"VAT generation resource limit");
    std::map<std::string,UdfAddress> icbs;std::map<std::string,uint32_t> fePhysical,dirPhysical;
    std::map<std::string,std::vector<std::string>> children;children[""]={};
    for(const auto &[path,e]:m.entries) {
        if(e.directory)children[path]={};auto parent=fs::path(path).parent_path().string();children[parent].push_back(path);
    }
    uint64_t cursor=uint64_t(next)+288;
    auto allocate=[&](uint32_t count){require(cursor+count<UINT32_MAX && cursor+count<=uint64_t(v.partition)+v.capacity,"VAT continuation exceeds physical partition capacity");auto result=uint32_t(cursor);cursor+=count;return result;};
    auto fsdPhysical=allocate(1);vat[fsdVirtual]=fsdPhysical-v.partition;
    for(const auto &[path,list]:children){dirPhysical[path]=allocate(uint32_t(list.size()+1));}
    for(auto &[path,e]:m.entries)if(!e.directory && !e.old && e.size){e.sections={{allocate(uint32_t((e.size+BlockSize-1)/BlockSize)),uint32_t(e.size)}};}
    uint64_t unique=std::max({uint64_t(16),u64(v.integrity.data()+40),v.nextUniqueId});
    auto normalizeId=[&](){require(unique<UINT64_MAX-16,"UDF UniqueID overflow");if(uint32_t(unique)==UINT32_MAX)++unique;if(uint32_t(unique)<16)unique+=16-uint32_t(unique);};
    auto nextId=[&](){normalizeId();return unique++;};
    std::map<std::string,uint64_t> ids;
    for(const auto &[path,a]:v.icbs) {
        auto physical=a.reference==v.virtualReference?v.vat.at(a.block):a.block;auto fe=source->read(v.partition+physical);
        auto id=u64(fe.data()+(u16(fe.data())==261?160:200));require(id<UINT64_MAX-100001,"UDF UniqueID overflow");unique=std::max(unique,id+1);
        ids[path]=id;
    }
    for(const auto &[path,list]:children){(void)list;auto abs=allocate(1);fePhysical[path]=abs;icbs[path]={uint32_t(vat.size()),v.virtualReference};vat.push_back(abs-v.partition);ids[path]=path.empty()?0:nextId();}
    for(const auto &[path,e]:m.entries)if(!e.directory) {
        if(e.old){icbs[path]=v.icbs.at(path);continue;} // 包括内嵌文件：原 ICB/VAT entry 完整复用。
        auto abs=allocate(1);fePhysical[path]=abs;icbs[path]={uint32_t(vat.size()),v.virtualReference};vat.push_back(abs-v.partition);ids[path]=nextId();
    }
    require(unique<UINT64_MAX-1 && vat.size()<1000000,"UDF VAT/UniqueID limit");
    normalizeId();
    auto header=v.vatHeader;uint32_t fileCount=0,dirCount=1;for(auto &[p,e]:m.entries){(void)p;e.directory?++dirCount:++fileCount;}
    put32(header.data()+132,v.vatIcb-v.partition);put32(header.data()+136,fileCount);put32(header.data()+140,dirCount);
    for(auto off:{144,146,148})put16(header.data()+off,0x0201);
    std::vector<uint8_t> table=header;for(auto block:vat){auto at=table.size();table.resize(at+4);put32(table.data()+at,block);}
    bool inlineVat=table.size()<=BlockSize-176;uint32_t tablePhysical=0;
    if(!inlineVat)tablePhysical=allocate(uint32_t((table.size()+BlockSize-1)/BlockSize));
    // 6.10.1.3：N-256 Anchor，255 块结束区，最后有效数据块为 VAT ICB。
    uint64_t end64=uint64_t(next)+((cursor-next+257+31)/32)*32;
    require(end64<=uint64_t(v.partition)+v.capacity && end64<UINT32_MAX,"UDF VAT end-of-session capacity overflow");
    auto end=uint32_t(end64),vatPhysical=end-1;std::map<uint32_t,Block> metadata;
    for(auto [offset,id]:std::vector<std::pair<uint32_t,std::string>>{{16,"BEA01"},{17,"NSR03"},{18,"TEA01"}}){Block b{};memcpy(b.data()+1,id.data(),5);b[6]=1;metadata[next+offset]=b;}
    auto lvd=v.lvd;put32(lvd.data()+432,2*BlockSize);put32(lvd.data()+436,next+64);entity(lvd.data()+272);
    Block usd{},term{};put32(usd.data()+16,5);
    std::vector<Block> descriptors{v.pvd,v.iuvd,lvd,v.pd,usd,term};
    const std::vector<uint16_t> types{1,4,6,5,7,8};
    for(auto base:{next+32,next+96})for(size_t i=0;i<descriptors.size();++i){auto b=descriptors[i];auto len=i<4?u16(b.data()+10):(i==4?8:496);tag(b,types[i],base+uint32_t(i),len,3);metadata[base+uint32_t(i)]=b;}
    auto integrity=v.integrity;timestamp(integrity.data()+16);put32(integrity.data()+28,0);memset(integrity.data()+32,0,8);put64(integrity.data()+40,unique);
    require(u32(integrity.data()+76)>=46 && 80+16+u32(integrity.data()+76)<=BlockSize,"Unsupported VAT LVID implementation use");
    for(auto off:{80,84,88,92})put32(integrity.data()+off,UINT32_MAX);
    put32(integrity.data()+80+4*v.physicalReference,v.capacity-(end-v.partition));
    put32(integrity.data()+88+4*v.physicalReference,v.capacity);
    entity(integrity.data()+96);put32(integrity.data()+128,fileCount);put32(integrity.data()+132,dirCount);
    for(auto off:{136,138,140})put16(integrity.data()+off,0x0201);
    tag(integrity,9,next+64,u16(integrity.data()+10),3);metadata[next+64]=integrity;tag(term,8,next+65,496,3);metadata[next+65]=term;
    auto longReference=[&](uint8_t *p,UdfAddress a){longAd(p,a.block);put16(p+8,a.reference);};
    auto fsd=v.fsd;timestamp(fsd.data()+16);longReference(fsd.data()+400,icbs.at(""));memset(fsd.data()+448,0,32);tag(fsd,256,fsdVirtual,496,3);metadata[fsdPhysical]=fsd;
    for(const auto &[path,abs]:fePhysical) {
        bool directory=children.contains(path);Entry e;
        if(directory){e.directory=true;e.size=(children.at(path).size()+1)*BlockSize;e.sections={{dirPhysical.at(path),uint32_t(e.size)}};}
        else e=m.entries.at(path);
        require(e.sections.size()<=(BlockSize-176)/16,"VAT FE allocation limit");Block fe{};put16(fe.data()+20,4);put16(fe.data()+24,1);fe[27]=directory?4:5;put16(fe.data()+34,1);
        put32(fe.data()+36,UINT32_MAX);put32(fe.data()+40,UINT32_MAX);put32(fe.data()+44,0x14a5);
        uint32_t links=1;if(directory)for(auto &child:children.at(path))if(m.entries.at(child).directory)++links;
        require(links<=UINT16_MAX,"VAT directory link limit");put16(fe.data()+48,uint16_t(links));put64(fe.data()+56,e.size);
        uint64_t recorded=0;for(auto s:e.sections)recorded+=(uint64_t(s.size)+BlockSize-1)/BlockSize;put64(fe.data()+64,recorded);
        for(auto off:{72,84,96})timestamp(fe.data()+off);put32(fe.data()+108,1);entity(fe.data()+128);put64(fe.data()+160,ids.at(path));put32(fe.data()+172,uint32_t(e.sections.size()*16));
        for(size_t i=0;i<e.sections.size();++i){auto s=e.sections[i];require(!s.offset && s.block>=v.partition,"New VAT file allocation invalid");longReference(fe.data()+176+16*i,{s.block-v.partition,v.physicalReference});put32(fe.data()+176+16*i,s.size);}
        tag(fe,261,icbs.at(path).block,uint16_t(160+e.sections.size()*16),3);metadata[abs]=fe;
        if(!directory)continue;
        auto parent=fs::path(path).parent_path().string();auto block=dirPhysical.at(path);
        auto makeFid=[&](const std::string &name,const std::string &target,bool isDirectory,bool isParent){
            auto b=fid(name,isDirectory,icbs.at(target).block,block-v.partition,isParent);put16(b.data()+28,icbs.at(target).reference);
            put32(b.data()+32,uint32_t(ids.at(target)));tag(b,257,block-v.partition,BlockSize-16,3);metadata[block++]=b;
        };
        makeFid("",parent,true,true);
        for(auto &child:children.at(path))makeFid(fs::path(child).filename().string(),child,m.entries.at(child).directory,false);
    }
    Block vatFe{};put16(vatFe.data()+20,4);put16(vatFe.data()+24,1);vatFe[27]=248;put16(vatFe.data()+34,inlineVat?3:0);put16(vatFe.data()+48,1);put64(vatFe.data()+56,table.size());
    for(auto off:{72,84,96})timestamp(vatFe.data()+off);put32(vatFe.data()+108,1);entity(vatFe.data()+128);put64(vatFe.data()+160,unique);
    if(inlineVat){put32(vatFe.data()+172,uint32_t(table.size()));memcpy(vatFe.data()+176,table.data(),table.size());}
    else {put64(vatFe.data()+64,(table.size()+BlockSize-1)/BlockSize);put32(vatFe.data()+172,8);put32(vatFe.data()+176,uint32_t(table.size()));put32(vatFe.data()+180,tablePhysical-v.partition);
        for(size_t pos=0;pos<table.size();pos+=BlockSize){Block b{};memcpy(b.data(),table.data()+pos,std::min(BlockSize,table.size()-pos));metadata[tablePhysical+uint32_t(pos/BlockSize)]=b;}}
    tag(vatFe,261,vatPhysical-v.partition,uint16_t(160+(inlineVat?table.size():8)),3);metadata[vatPhysical]=vatFe;
    for(auto abs:{next+256,end-257}){Block a{};put32(a.data()+16,16*BlockSize);put32(a.data()+20,next+32);put32(a.data()+24,16*BlockSize);put32(a.data()+28,next+96);tag(a,2,abs,496,3);metadata[abs]=a;}
    auto readsBefore=source->reads;int fd=open(image.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);require(fd>=0,"Cannot create VAT continuation");
    try {
        require(ftruncate(fd,off_t(end-next)*BlockSize)==0,"Cannot size VAT continuation");
        for(auto &[lba,b]:metadata)require(pwrite(fd,b.data(),BlockSize,off_t(lba-next)*BlockSize)==ssize_t(BlockSize),"VAT metadata write failed");
        for(auto &[path,file]:sources){auto &e=m.entries.at(path);std::ifstream in(file,std::ios::binary);require(bool(in),"Cannot read VAT input");uint64_t offset=0;std::array<char,65536> buffer{};
            while(offset<e.size){checkInterrupted();auto n=std::min<uint64_t>(buffer.size(),e.size-offset);in.read(buffer.data(),n);require(uint64_t(in.gcount())==n,"VAT input short/changed");require(pwrite(fd,buffer.data(),n,off_t(e.sections.at(0).block-next)*BlockSize+offset)==ssize_t(n),"VAT file write failed");offset+=n;}}
        require(source->reads==readsBefore,"VAT generator reread old payload");require(fsync(fd)==0,"VAT fsync failed");close(fd);fd=-1;
        m.imageSha=shaFile(image);m.save(image.string()+".manifest");
        std::ostringstream report;report<<v.diagnostics<<"New VDS absolute="<<next+32<<"\nNew reserve VDS absolute="<<next+96<<"\nNew VAT absolute="<<vatPhysical<<" previous="<<v.vatIcb<<"\nOld-source reads during generation: 0\n";
        writeText(image.string()+".udf.txt",report.str());std::cout<<"UDF 2.01/VAT continuation LBA: "<<next<<"\nImage blocks: "<<end-next<<"\nOld-source reads during generation: 0\n";
        if(!v.tree.compatibility.empty())std::cout<<"UDF source compatibility: "<<v.tree.compatibility<<'\n';
        return m;
    } catch(...){if(fd>=0)close(fd);std::error_code ec;fs::remove(image,ec);fs::remove(image.string()+".manifest",ec);throw;}
}
Manifest buildUdfSession(std::shared_ptr<Blocks> source, uint32_t last, uint32_t next, uint32_t readableEnd,
                         const fs::path &files, const fs::path &image, const std::string &sourcePath, uint32_t sourceBase, uint32_t fixedBlocks, ConflictPolicy policy) {
    require(next > readableEnd && !fs::exists(image) && !fs::exists(image.string() + ".manifest"), "Invalid UDF append address or output exists");
    if(readUdfVolume(*source,last,readableEnd).revision==0x0201){require(!fixedBlocks,"UDF VAT is excluded from DVD-R recovery");return buildVatSession(source,last,next,readableEnd,files,image,sourcePath,sourceBase,policy);}
    auto imported = readUdfTree(*source, last, readableEnd); auto layout = readLayout(*source, last);
    require(imported.inlineFiles.empty(), "UDF inline/sparse files need ICB reuse; current data-extent writer refuses them");
    Manifest m; m.filesystem = "udf1.02"; m.importedStart = last; m.start = next; m.sourceBase = sourceBase; m.sourcePath = sourcePath; m.entries = imported.entries;
    std::map<std::string, fs::path> sources; merge(files, "", m, sources, policy); require(!sources.empty(), "Nothing new to append");
    std::map<std::string, uint32_t> icbs, dirBlocks; std::map<std::string, std::vector<std::string>> children;
    icbs[""] = next + 290; children[""] = {};
    uint32_t cursor = next + 291;
    for (const auto &[path, entry] : m.entries) { icbs[path] = cursor++; if (entry.directory) children[path] = {}; }
    for (const auto &[path, entry] : m.entries) {
        (void)entry; auto slash = path.rfind('/'); auto parent = slash == path.npos ? "" : path.substr(0, slash); children[parent].push_back(path);
    }
    for (const auto &[path, entries] : children) { dirBlocks[path] = cursor; cursor += uint32_t(entries.size() + 1); }
    uint32_t oldEnd = layout.partition + u32(layout.pd.data() + 192);
    for (auto &[path, entry] : m.entries) if (!entry.directory) {
        if (entry.old) {
            for (auto section : entry.sections) require(section.block >= layout.partition && uint64_t(section.block) + (uint64_t(section.size) + BlockSize - 1) / BlockSize <= oldEnd && section.size < 0x40000000, "Old UDF extent outside imported partition");
        } else if (entry.size) { entry.sections = {{cursor, uint32_t(entry.size)}}; cursor += uint32_t((entry.size + BlockSize - 1) / BlockSize); }
    }
    require(cursor > next && uint64_t(cursor) + 288 < UINT32_MAX, "UDF address overflow");
    uint32_t initialBody = ((cursor - next + 31) / 32) * 32;
    // DVD-R 的实测 Track 按 16 块补齐；提前对齐，让尾部 N/N-256 Anchor 与物理 Track 末尾一致。
    uint32_t end = next + ((initialBody + 257 + 15) / 16) * 16, bodyEnd = end - 257;
    if (fixedBlocks) {
        // 恢复只允许精确填满已有的空白预留 Track；尾部 Anchor 必须跟随这个真实边界。
        require(fixedBlocks % 16 == 0 && fixedBlocks >= end - next && uint64_t(next) + fixedBlocks < UINT32_MAX,
                "Recovery image does not fit the exact reserved Track; use fewer/smaller files");
        end = next + fixedBlocks;
        bodyEnd = end - 257;
    }
    uint32_t partitionLength = bodyEnd - layout.partition;
    std::map<uint32_t, Block> metadata;
    for (auto [offset, id] : std::vector<std::pair<uint32_t, std::string>>{{16,"BEA01"},{17,"NSR02"},{18,"TEA01"}}) { Block b{}; memcpy(b.data() + 1, id.data(), 5); b[6] = 1; metadata[next + offset] = b; }
    auto pvd = layout.pvd, iuvd = layout.iuvd, lvd = layout.lvd, pd = layout.pd, usd = layout.usd, term = layout.term;
    put32(pd.data() + 192, partitionLength); memset(pd.data() + 56, 0, 128);
    longAd(lvd.data() + 248, next + 288 - layout.partition); put32(lvd.data() + 248, 2 * BlockSize);
    put32(lvd.data() + 432, 2 * BlockSize); put32(lvd.data() + 436, next + 64);
    put32(usd.data() + 20, 0); memset(usd.data() + 24, 0, BlockSize - 24);
    std::vector<Block> vds{pvd,iuvd,lvd,pd,usd,term};
    for (uint32_t base : {next + 32, next + 96}) for (size_t i = 0; i < vds.size(); ++i) {
        auto b = vds[i]; auto length = u16(b.data() + 10); if (u16(b.data()) == 7) length = 8;
        tag(b, u16(b.data()), base + uint32_t(i), length); metadata[base + uint32_t(i)] = b;
    }
    auto integrity = layout.integrity; timestamp(integrity.data() + 16); put32(integrity.data() + 28, 1); memset(integrity.data() + 32, 0, 8);
    put64(integrity.data() + 40, std::max<uint64_t>(u64(integrity.data() + 40), 16) + m.entries.size() + 1);
    put32(integrity.data() + 80, 0xffffffff); put32(integrity.data() + 84, partitionLength);
    uint32_t fileCount = 0, dirCount = 1; for (const auto &[path,e] : m.entries) { (void)path; e.directory ? ++dirCount : ++fileCount; }
    entity(integrity.data() + 88); put32(integrity.data() + 120, fileCount); put32(integrity.data() + 124, dirCount);
    put16(integrity.data() + 128, 0x0102); put16(integrity.data() + 130, 0x0102); put16(integrity.data() + 132, 0x0102);
    tag(integrity, 9, next + 64, u16(integrity.data() + 10)); metadata[next + 64] = integrity;
    Block termination{}; tag(termination, 8, next + 65, 496); metadata[next + 65] = termination;
    auto fsd = layout.fsd; timestamp(fsd.data() + 16); longAd(fsd.data() + 400, icbs[""] - layout.partition); memset(fsd.data() + 448, 0, 16);
    tag(fsd, 256, next + 288 - layout.partition, 496); metadata[next + 288] = fsd;
    tag(termination, 8, next + 289 - layout.partition, 496); metadata[next + 289] = termination;
    uint64_t unique = std::max<uint64_t>(u64(layout.integrity.data() + 40), 16);
    for (const auto &[path, location] : icbs) {
        bool directory = path.empty() || m.entries.at(path).directory; Entry e;
        if (directory) { e.directory = true; e.size = (children[path].size() + 1) * BlockSize; e.sections = {{dirBlocks[path], uint32_t(e.size)}}; }
        else e = m.entries.at(path);
        require(e.sections.size() <= (BlockSize - 176) / 8, "UDF FE allocation descriptor limit");
        Block fe = layout.root; memset(fe.data() + 176, 0, BlockSize - 176); fe[27] = directory ? 4 : 5; put16(fe.data() + 34, 0);
        uint32_t links = 1;
        if (directory) for (const auto &child : children[path]) if (m.entries.at(child).directory) ++links;
        require(links <= UINT16_MAX, "UDF directory link limit");
        memset(fe.data() + 28, 0, 6); put16(fe.data() + 48, uint16_t(links));
        put64(fe.data() + 56, e.size); uint64_t recorded = 0; for (auto s : e.sections) recorded += (uint64_t(s.size) + BlockSize - 1) / BlockSize; put64(fe.data() + 64, recorded);
        for (int position : {72,84,96}) timestamp(fe.data() + position);
        memset(fe.data() + 112, 0, 16); entity(fe.data() + 128); put64(fe.data() + 160, path.empty() ? 0 : unique++);
        put32(fe.data() + 168, 0); put32(fe.data() + 172, uint32_t(e.sections.size() * 8));
        for (size_t i = 0; i < e.sections.size(); ++i) { put32(fe.data() + 176 + i * 8, e.sections[i].size); put32(fe.data() + 180 + i * 8, e.sections[i].block - layout.partition); }
        tag(fe, 261, location - layout.partition, uint16_t(160 + e.sections.size() * 8)); metadata[location] = fe;
        if (directory) {
            auto slash = path.rfind('/'); auto parent = path.empty() || slash == path.npos ? "" : path.substr(0, slash);
            auto block = dirBlocks[path]; metadata[block] = fid("", true, icbs[parent] - layout.partition, block - layout.partition, true);
            for (const auto &child : children[path]) {
                auto at = child.rfind('/'); auto leaf = at == child.npos ? child : child.substr(at + 1); ++block;
                metadata[block] = fid(leaf, m.entries.at(child).directory, icbs[child] - layout.partition, block - layout.partition);
            }
        }
    }
    for (uint32_t location : {next + 256, bodyEnd, end - 1}) {
        Block b{}; put32(b.data() + 16, 16 * BlockSize); put32(b.data() + 20, next + 32);
        put32(b.data() + 24, 16 * BlockSize); put32(b.data() + 28, next + 96); tag(b, 2, location, 496); metadata[location] = b;
    }
    auto readsBefore = source->reads;
    int fd = open(image.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600); require(fd >= 0, "Cannot create UDF continuation");
    try {
        require(ftruncate(fd, off_t(end - next) * BlockSize) == 0, "Cannot size UDF continuation");
        for (const auto &[lba,b] : metadata) require(pwrite(fd,b.data(),b.size(),off_t(lba-next)*BlockSize) == ssize_t(BlockSize), "UDF metadata write failed");
        for (const auto &[path,file] : sources) {
            const auto &e = m.entries.at(path); std::ifstream in(file,std::ios::binary); require(bool(in), "Cannot read new UDF file");
            uint64_t offset = 0; std::array<char,65536> buffer{};
            while (offset < e.size) { checkInterrupted(); auto n = std::min<uint64_t>(buffer.size(),e.size-offset); in.read(buffer.data(),n); require(uint64_t(in.gcount()) == n, "New UDF file changed/short read");
                require(pwrite(fd,buffer.data(),n,off_t(e.sections[0].block-next)*BlockSize+off_t(offset)) == ssize_t(n), "UDF new file write failed"); offset += n; }
        }
        require(source->reads == readsBefore, "UDF generator reread old blocks"); require(fsync(fd) == 0, "UDF fsync failed"); close(fd); fd = -1;
        m.imageSha = shaFile(image); m.save(image.string()+".manifest");
        std::cout << "UDF 1.02 continuation LBA: " << next << "\nImage blocks: " << end-next << "\nOld-source reads during generation: 0\n";
        return m;
    } catch (...) { if(fd>=0)close(fd); std::error_code ec; fs::remove(image,ec); fs::remove(image.string()+".manifest",ec); throw; }
}
void verifyUdfSession(Blocks &source, const Manifest &m, uint32_t blocks, bool hashOld) {
    if(m.filesystem=="udf2.01+vat") {
        auto v=readVatVolume(source,m.start,m.start+blocks);
        require(v.vatIcb==m.start+blocks-1,"VAT ICB is not the last valid transaction sector");
        auto tail=source.read(m.start+blocks-257);validateUdfTag(tail.data(),BlockSize,2,3,m.start+blocks-257);
        require(u32(tail.data()+20)==v.mainVds && u32(tail.data()+28)==v.reserveVds,"VAT end Anchor differs from current entry");
        for(uint32_t i=0;i<6;++i){auto main=source.read(v.mainVds+i),reserve=source.read(v.reserveVds+i);validateUdfTag(reserve.data(),BlockSize,u16(main.data()),3,v.reserveVds+i);require(std::equal(main.begin()+16,main.end(),reserve.begin()+16),"VAT reserve VDS differs");}
        require(v.tree.entries.size()==m.entries.size(),"VAT verified tree count differs");
        size_t oldFiles=0,newFiles=0,oldHashes=0;
        for(auto &[path,e]:m.entries){auto at=v.tree.entries.find(path);require(at!=v.tree.entries.end() && at->second.directory==e.directory,"VAT verified name/type differs: "+path);if(e.directory)continue;
            require(at->second.size==e.size && at->second.sections==e.sections,"VAT verified size/actual data address differs: "+path);
            for(auto s:e.sections)require(e.old?uint64_t(s.block)+(uint64_t(s.offset)+s.size+BlockSize-1)/BlockSize<=m.start:s.block>=m.start && uint64_t(s.block)+(uint64_t(s.size)+BlockSize-1)/BlockSize<v.vatIcb,"VAT old/new data range differs");
            if(!e.old || (hashOld && !e.sha.empty())){require(hashEntry(source,at->second)==e.sha,"VAT file hash mismatch: "+path);if(e.old)++oldHashes;}
            e.old?++oldFiles:++newFiles;
        }
        std::cout<<"Verified UDF 2.01/VAT: current tree/extents; new files="<<newFiles<<", old files="<<oldFiles<<", old hashes verified="<<oldHashes<<" (unverified="<<oldFiles-oldHashes<<")\n";return;
    }
    auto layout = readLayout(source, m.start);
    require(layout.partition < m.start && layout.partition + u32(layout.pd.data()+192) == m.start + blocks - 257, "UDF partition span differs from continuation");
    for(uint32_t lba : {m.start+256,m.start+blocks-257,m.start+blocks-1}) { auto b=source.read(lba);validTag(b,2,lba);require(u32(b.data()+20)==m.start+32 && u32(b.data()+28)==m.start+96,"UDF Anchor VDS mismatch"); }
    for(uint32_t i=0;i<6;++i) {auto b=source.read(m.start+96+i);validTag(b,u16(b.data()),m.start+96+i);auto main=source.read(m.start+32+i);require(std::equal(b.begin()+16,b.end(),main.begin()+16),"UDF reserve VDS mismatch");}
    require(u32(layout.lvd.data()+252)+layout.partition==m.start+288 && u32(layout.lvd.data()+436)==m.start+64 &&
            u32(layout.integrity.data()+28)==1 && u32(layout.integrity.data()+84)==u32(layout.pd.data()+192),"UDF FSD/integrity/partition pointers differ");
    std::set<uint32_t> visited;Tree strict;auto root=u32(layout.fsd.data()+404)+layout.partition;
    validateTree(source,layout,m.start,m.start+blocks-257,root,root,"",visited,strict);
    require(strict.size()==m.entries.size(),"Strict UDF tree count differs");
    auto tree = readUdfTree(source,m.start,m.start+blocks);require(tree.inlineFiles.empty() && tree.entries.size()==m.entries.size(),"UDF tree/count differs");
    size_t oldFiles=0,newFiles=0,oldHashes=0;
    for(const auto &[path,e]:m.entries) {
        auto at=tree.entries.find(path);require(at!=tree.entries.end() && at->second.directory==e.directory,"UDF name/type mismatch: "+path);
        require(strict.contains(path) && strict.at(path).directory==e.directory && (e.directory || (strict.at(path).size==e.size && strict.at(path).sections==e.sections)),"Strict UDF tree/extent mismatch: "+path);
        if(e.directory)continue;
        require(at->second.size==e.size && at->second.sections==e.sections,"UDF size/extent mismatch: "+path);
        for(auto s:e.sections)require(e.old ? uint64_t(s.block)+(uint64_t(s.size)+BlockSize-1)/BlockSize<=m.start : s.block>=m.start && uint64_t(s.block)+(uint64_t(s.size)+BlockSize-1)/BlockSize<=m.start+blocks-257,"UDF old/new extent range mismatch");
        if(!e.old || (hashOld && !e.sha.empty())) {require(hashEntry(source,at->second)==e.sha,"UDF file hash mismatch: "+path);if(e.old)++oldHashes;}
        e.old ? ++oldFiles : ++newFiles;
    }
    std::cout<<"Verified UDF: old files="<<oldFiles<<" new files="<<newFiles<<" old content hashes="<<oldHashes<<"\n";
}
}
