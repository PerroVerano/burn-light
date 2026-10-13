#include "UdfVat.hpp"
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <set>
#include <sstream>

namespace ac {
namespace {
uint16_t get16(const uint8_t *p) { return uint16_t(p[0]) | uint16_t(p[1])<<8; }
uint32_t get32(const uint8_t *p) { return get16(p) | uint32_t(get16(p+2))<<16; }
uint64_t get64(const uint8_t *p) { return get32(p) | uint64_t(get32(p+4))<<32; }
uint16_t crc(const uint8_t *p,size_t n) {
    uint16_t c=0;
    for(size_t i=0;i<n;++i) { c^=uint16_t(p[i])<<8; for(int j=0;j<8;++j)c=c&0x8000?uint16_t((c<<1)^0x1021):uint16_t(c<<1); }
    return c;
}
std::string cs0(const uint8_t *p,size_t n) {
    require(n>0 && (p[0]==8 || p[0]==16),"Unsupported UDF CS0 encoding");
    if(p[0]==16) { require((n-1)%2==0,"Truncated UDF Unicode");return utf16Name(p+1,n-1); }
    std::vector<uint8_t> wide;
    for(size_t i=1;i<n;++i){wide.push_back(0);wide.push_back(p[i]);}
    return utf16Name(wide.data(),wide.size());
}
std::string dstring(const uint8_t *p,size_t n) {
    auto len=p[n-1]; require(len && len<n,"Invalid UDF dstring length");return cs0(p,len);
}
UdfAddress address(const uint8_t *p) {
    require(get32(p)==BlockSize,"Unsupported UDF ICB extent length/type");return {get32(p+4),get16(p+8)};
}
class Reader {
    Blocks &source;
    uint32_t end;
    std::vector<Blocks::Range> ranges;
    uint64_t historyBytes=0;
    struct History {uint32_t icb;std::vector<uint32_t> map;bool windows;uint64_t nextUniqueId;};
    std::vector<History> history;
    uint64_t compatibilityBytes=0;
    size_t compatibilityProbes=0;
    size_t fidRecords=0;
    bool windowsTransaction=false;
public:
    VatVolume v;
    Reader(Blocks &s,uint32_t last,uint32_t limit):source(s),end(limit),ranges(s.recordedRanges()) {
        v.session=last;
        require(!ranges.empty(),"UDF needs recorded Track ranges");
        if(ranges.back().session) {
            auto first=std::find_if(ranges.begin(),ranges.end(),[&](auto r){return r.session==ranges.back().session;});
            require(first!=ranges.end() && first->begin==last,"UDF entry is not the latest completed Session");
        }
    }
    void span(uint32_t lba,uint32_t count=1) const {
        require(count && uint64_t(lba)+count<=end,"UDF reference overflow/outside recorded media");
        auto it=std::find_if(ranges.begin(),ranges.end(),[&](auto r){return lba>=r.begin && lba<r.end;});
        require(it!=ranges.end() && uint64_t(lba)+count<=it->end,"UDF reference points to a Track gap/unrecorded region");
    }
    Block raw(uint32_t lba) {span(lba);return source.read(lba);}
    void trace(const std::string &kind,uint32_t lba) {
        auto r=std::find_if(ranges.begin(),ranges.end(),[&](auto r){return lba>=r.begin && lba<r.end;});
        std::ostringstream out;out<<kind<<" absolute="<<lba;
        if(r!=ranges.end())out<<" track="<<r->track<<" session="<<r->session;
        v.diagnostics+=out.str()+"\n";
    }
    void vds(uint32_t begin,uint32_t bytes) {
        require(bytes>=16*BlockSize && bytes<=64*BlockSize && bytes%BlockSize==0,"Unsupported UDF VDS extent");span(begin,bytes/BlockSize);
        std::set<uint16_t> found; bool terminated=false;
        for(uint32_t i=0;i<bytes/BlockSize;++i) {
            auto b=raw(begin+i);auto id=get16(b.data());validateUdfTag(b.data(),b.size(),id,get16(b.data()+2),begin+i);
            require(get16(b.data()+10)>=(id==6?424U:(id==7?8U:496U)),"Truncated UDF VDS CRC coverage");
            require(id==1 || id==4 || id==5 || id==6 || id==7 || id==8,"Unsupported UDF VDS descriptor/continuation");
            require(found.insert(id).second,"Ambiguous duplicate UDF VDS descriptor");
            if(id==1)v.pvd=b;else if(id==4)v.iuvd=b;else if(id==5)v.pd=b;else if(id==6)v.lvd=b;
            else if(id==8){terminated=true;break;}
        }
        require(terminated && found.contains(1) && found.contains(4) && found.contains(5) && found.contains(6),"Incomplete UDF VDS");
        auto l=v.lvd.data();auto p=v.pd.data();v.revision=get16(l+240);
        require(get32(l+264)<=BlockSize-440 && get16(l+10)>=424+get32(l+264),"Truncated UDF partition map CRC coverage");
        require(v.revision==0x0102 || v.revision==0x0201,"Unsupported UDF revision");
        uint16_t version=v.revision==0x0201?3:2;
        for(auto b:{v.pvd,v.iuvd,v.pd,v.lvd})require(get16(b.data()+2)==version,"UDF descriptor Tag Version differs from revision");
        require(!memcmp(l+217,"*OSTA UDF Compliant",18) && get32(l+212)==BlockSize,"Unsupported UDF domain/block size");
        v.partitionNumber=get16(p+22);v.partition=get32(p+188);v.capacity=get32(p+192);
        require(v.capacity && uint64_t(v.partition)+v.capacity<=UINT32_MAX,"UDF partition capacity overflow");
        require(!memcmp(p+25,"+NSR",4),"Unsupported UDF partition contents");
        if(v.revision==0x0102) {
            require(get32(l+264)==6 && get32(l+268)==1 && l[440]==1 && l[441]==6 && get16(l+444)==v.partitionNumber,
                    "Unsupported UDF 1.02 partition map");return;
        }
        require(get32(p+184)==2,"UDF VAT needs a write-once physical partition");
        require(!memcmp(p+25,"+NSR03",6) && l[216]==0 && l[242]==0,"Unsupported UDF 2.01 partition contents/domain flags");
        require(get32(l+268)==2 && get32(l+264)==70,"UDF 2.01 supports only type-1 plus Virtual Partition Map");
        uint32_t offset=440;bool physical=false,virt=false;
        for(uint16_t index=0;index<2;++index) {
            auto map=l+offset;
            if(map[0]==1 && map[1]==6) {
                require(!physical && get16(map+2)==1 && get16(map+4)==v.partitionNumber,"Invalid UDF physical partition map");
                v.physicalReference=index;physical=true;
            } else if(map[0]==2 && map[1]==64) {
                require(!virt && map[4]==0 && !memcmp(map+5,"*UDF Virtual Partition",22) && get16(map+28)==0x0201 &&
                        get16(map+36)==1 && get16(map+38)==v.partitionNumber,"Unsupported/invalid UDF type-2 partition map");
                v.virtualReference=index;virt=true;
            } else throw std::runtime_error("Unsupported UDF partition map");
            offset+=map[1];
        }
        require(physical && virt && offset==510 && get16(l+256)==v.virtualReference,"Invalid UDF FSD partition reference");
        v.tree.filesystem="udf2.01+vat";v.tree.revision=v.revision;v.tree.volumeName=dstring(l+84,128);
        auto ilen=get32(l+432),ilba=get32(l+436);
        require(ilen>=BlockSize && ilen<=16*BlockSize,"Unsupported UDF integrity sequence");
        v.integrity=raw(ilba);validateUdfTag(v.integrity.data(),BlockSize,9,3,ilba);
        require(get32(v.integrity.data()+76)>=46 && uint64_t(80)+16+get32(v.integrity.data()+76)<=BlockSize &&
                get16(v.integrity.data()+10)>=80+get32(v.integrity.data()+76),"Truncated UDF integrity implementation use/CRC coverage");
        require(get32(v.integrity.data()+28)==0 && get32(v.integrity.data()+72)==2,"UDF VAT integrity must be open with two partitions");
        trace("VDS",begin);trace("Partition start",v.partition);
    }
    void volume() {
        require(uint64_t(v.session)+256<end,"UDF latest Anchor outside recorded range");
        v.anchor=v.session+256;auto anchor=raw(v.anchor);
        validateUdfTag(anchor.data(),BlockSize,2,get16(anchor.data()+2),v.anchor);
        require(get16(anchor.data()+10)==496,"Truncated UDF Anchor CRC coverage");
        v.mainVds=get32(anchor.data()+20);v.reserveVds=get32(anchor.data()+28);
        trace("Latest Anchor",v.anchor);
        try {vds(v.mainVds,get32(anchor.data()+16));}
        catch(const std::exception &mainError) {
            auto reason=std::string(mainError.what());
            try { vds(v.reserveVds,get32(anchor.data()+24));v.reserveUsed=true;v.diagnostics+="Reserve VDS recovery: "+reason+"\n"; }
            catch(const std::exception &reserveError){throw std::runtime_error("Main/reserve UDF VDS invalid: "+reason+"; "+reserveError.what());}
        }
        require(get16(anchor.data()+2)==(v.revision==0x0201?3:2),"UDF Anchor Tag Version mismatch");
        trace("Main VDS pointer",v.mainVds);trace("Reserve VDS pointer",v.reserveVds);
    }
    uint32_t physical(UdfAddress a,const std::vector<uint32_t> *mapping=nullptr) {
        require(a.reference==v.physicalReference || a.reference==v.virtualReference,"Invalid UDF Partition Reference (map index)");
        uint32_t block=a.block;
        if(a.reference==v.virtualReference){const auto &map=mapping?*mapping:v.vat;require(block<map.size() && map[block]!=UINT32_MAX,"Invalid/unmapped VAT entry");block=map[block];}
        require(block<v.capacity && uint64_t(v.partition)+block<UINT32_MAX,"UDF partition reference overflow");
        auto lba=v.partition+block;span(lba);return lba;
    }
    struct Node {Block fe;Entry entry;UdfAddress icb;uint8_t type;std::vector<UdfAddress> dataAddresses;};
    Node node(UdfAddress a,const std::vector<uint32_t> *mapping=nullptr) {
        auto lba=physical(a,mapping);Node n{raw(lba),{},a,0,{}};auto p=n.fe.data();auto id=get16(p);
        require(id==261 || id==266,"Unsupported UDF ICB (FE/EFE required)");validateUdfTag(p,BlockSize,id,3,a.block);
        require(get16(p+20)==4 && get16(p+24)==1,"Unsupported UDF ICB strategy/entries");
        n.type=p[27];require(n.type==4 || n.type==5 || n.type==248,"Unsupported UDF file type/stream");
        n.entry.old=true;n.entry.directory=n.type==4;n.entry.size=get64(p+56);
        auto header=id==261?176U:216U;auto ea=get32(p+header-8),ads=get32(p+header-4);auto flags=get16(p+34)&7;
        require(uint64_t(header)+ea+ads<=BlockSize && get16(p+10)==header-16+ea+ads,"Truncated UDF FE allocation data/CRC coverage");
        if(id==266)require(get64(p+64)==n.entry.size && get32(p+152)==0 && get32(p+136)==0,"Unsupported UDF EFE streams/object size");
        else require(get32(p+112)==0,"Unsupported UDF external extended attribute ICB");
        require((get16(p+34)&0xE000)==0,"Unsupported UDF transformed/multiversion file");
        if(flags==3) {
            require(n.entry.size<=ads,"Truncated UDF inline data");
            require(get64(p+(id==261?64:72))==0,"Inline UDF recorded-block count must be zero");
            if(n.entry.size)n.entry.sections.push_back({lba,uint32_t(n.entry.size),header+ea});
            return n;
        }
        require(flags==0 || flags==1,"Unsupported UDF allocation descriptor type");
        const auto step=flags==0?8U:16U;std::set<std::pair<uint16_t,uint32_t>> chain;
        uint64_t total=0;size_t allocations=0;
        std::function<void(const uint8_t *,uint32_t,int)> parse=[&](const uint8_t *ad,uint32_t length,int depth) {
            require(depth<128 && length%step==0,"UDF allocation chain/declaration limit");
            for(uint32_t pos=0;pos<length;pos+=step) {
                require(++allocations<=100000,"UDF allocation resource limit");
                auto bytes=get32(ad+pos),kind=bytes>>30;bytes&=0x3fffffff;
                UdfAddress target{get32(ad+pos+4),flags==0?a.reference:get16(ad+pos+8)};
                require(bytes>0,"Zero UDF allocation descriptor");
                if(kind==3) {
                    require(bytes==BlockSize && chain.emplace(target.reference,target.block).second,"UDF allocation chain cycle/size");
                    auto b=raw(physical(target));validateUdfTag(b.data(),BlockSize,258,3,target.block);
                    auto len=get32(b.data()+20);require(len<=BlockSize-24 && get16(b.data()+10)>=8+len,"Truncated allocation extent");parse(b.data()+24,len,depth+1);continue;
                }
                require(kind==0,"Unsupported sparse/unrecorded UDF extent");
                require(target.reference==v.physicalReference,"File/directory allocations must identify physical space");
                require(total<=n.entry.size && bytes<=n.entry.size-total,"UDF allocation exceeds information length");
                for(uint64_t at=0;at<bytes;at+=BlockSize) {
                    require(n.dataAddresses.size()<1000000,"UDF file block resource limit");
                    require(uint64_t(target.block)+at/BlockSize<=UINT32_MAX,"UDF allocation address overflow");
                    UdfAddress block{target.block+uint32_t(at/BlockSize),target.reference};auto abs=physical(block);
                    auto size=uint32_t(std::min<uint64_t>(BlockSize,bytes-at));
                    n.dataAddresses.push_back(block);
                    if(!n.entry.sections.empty() && n.entry.sections.back().offset==0 && n.entry.sections.back().size%BlockSize==0 &&
                       uint64_t(n.entry.sections.back().block)+n.entry.sections.back().size/BlockSize==abs && uint64_t(n.entry.sections.back().size)+size<=UINT32_MAX)
                        n.entry.sections.back().size+=size;
                    else n.entry.sections.push_back({abs,size});
                }
                total+=bytes;
            }
        };
        parse(p+header+ea,ads,0);require(total==n.entry.size,"UDF allocation length differs from file size");
        require(get64(p+(id==261?64:72))==n.dataAddresses.size(),"UDF recorded-block count differs from allocations");return n;
    }
    std::vector<uint8_t> data(const Entry &e,uint64_t limit) {
        require(e.size<=limit,"UDF metadata resource limit");std::vector<uint8_t> out;
        for(auto s:e.sections)for(uint64_t at=0;at<s.size;) {
            auto pos=at+s.offset;auto b=raw(s.block+uint32_t(pos/BlockSize));auto len=std::min<uint64_t>(BlockSize-pos%BlockSize,s.size-at);
            out.insert(out.end(),b.begin()+pos%BlockSize,b.begin()+pos%BlockSize+len);at+=len;
        }
        return out;
    }
    static bool microsoft(const Block &fe) {
        auto offset=get16(fe.data())==261?129U:169U;
        return !memcmp(fe.data()+offset,"*Microsoft Windows",18) &&
               std::all_of(fe.begin()+offset+18,fe.begin()+offset+23,[](auto c){return c==0 || c==' ';});
    }
    static uint64_t uniqueId(const Block &fe) {return get64(fe.data()+(get16(fe.data())==261?160:200));}
    bool historicalFid(const Node &directory,const uint8_t *fid,uint32_t size,size_t position,uint32_t recorded,size_t first=0) {
        for(size_t i=first;i<history.size();++i) {
            const auto &h=history[i];
            require(++compatibilityProbes<=100000,"Windows FID history proof resource limit");
            auto a=directory.icb;
            if(a.reference!=v.virtualReference || a.block>=h.map.size() || h.map[a.block]==UINT32_MAX)continue;
            auto abs=physical(a,&h.map);if(abs>h.icb)continue;
            auto old=node(a,&h.map);require(old.type==4,"Historical directory ICB type differs");
            if(uniqueId(old.fe)!=uniqueId(directory.fe) || position+size>old.entry.size)continue;
            auto location=old.dataAddresses.empty()?a.block:old.dataAddresses.at(position/BlockSize).block;
            if(location!=recorded)continue;
            compatibilityBytes+=old.entry.size;require(compatibilityBytes<=64*1024*1024,"Windows FID history proof byte limit");
            auto bytes=data(old.entry,64*1024*1024);auto p=bytes.data()+position;
            validateUdfTag(p,size,257,3,recorded);
            if(!std::equal(p,p+size,fid))continue;
            auto child=node(address(p+20),&h.map);
            require(get32(p+32)==uint32_t(uniqueId(child.fe)),"Historical FID UniqueID differs from ICB");
            trace("Windows FID proven historical directory ICB",abs);trace("Windows FID proof VAT ICB",h.icb);
            return true;
        }
        return false;
    }
    bool newZeroChild(UdfAddress a,uint32_t vatIcb,uint64_t nextUniqueId,size_t older,const Node &child) {
        auto abs=physical(a);
        if(abs>vatIcb || (older<history.size() && abs<=history[older].icb) || uniqueId(child.fe)>=nextUniqueId)return false;
        auto r=std::find_if(ranges.begin(),ranges.end(),[&](auto r){return vatIcb>=r.begin && vatIcb<r.end;});
        if(r==ranges.end() || abs<r->begin || abs>=r->end)return false;
        for(size_t i=older;i<history.size();++i)
            if(a.block<history[i].map.size() && history[i].map[a.block]!=UINT32_MAX)return false;
        return true;
    }
    bool historicalZeroFid(const Node &directory,const uint8_t *fid,uint32_t size,size_t position,const Node &child) {
        auto a=directory.icb,childAddress=child.icb;
        for(size_t i=0;i<history.size();++i) {
            require(++compatibilityProbes<=100000,"Windows FID history proof resource limit");
            const auto &h=history[i];
            if(!h.windows || a.block>=h.map.size() || h.map[a.block]==UINT32_MAX ||
               childAddress.block>=h.map.size() || h.map[childAddress.block]==UINT32_MAX ||
               physical(childAddress,&h.map)!=physical(childAddress) || !newZeroChild(childAddress,h.icb,h.nextUniqueId,i+1,child))continue;
            auto old=node(a,&h.map);
            if(old.type!=4 || !microsoft(old.fe) || uniqueId(old.fe)!=uniqueId(directory.fe) ||
               physical(a,&h.map)>h.icb || position+size>old.entry.size)continue;
            compatibilityBytes+=old.entry.size;require(compatibilityBytes<=64*1024*1024,"Windows FID history proof byte limit");
            auto bytes=data(old.entry,64*1024*1024);
            if(!std::equal(fid,fid+size,bytes.data()+position))continue;
            // 旧零位置 FID 只能追溯到首次分配该 ICB 的 VAT；原事务仍需严格历史搬移证明。
            bool proven=false;
            for(size_t pos=0;pos<position;) {
                require(position-pos>=38,"Truncated Windows origin FID prefix");auto p=bytes.data()+pos;
                auto length=(38U+get16(p+36)+p[19]+3U)&~3U;
                require(length<=BlockSize && length<=position-pos,"Invalid Windows origin FID boundary");
                auto recorded=get32(p+12);validateUdfTag(p,length,257,3,recorded);
                require(get16(p+10)==length-16 && get16(p+16)==1,"Invalid Windows origin FID CRC coverage/version");
                auto location=old.dataAddresses.empty()?a.block:old.dataAddresses.at(pos/BlockSize).block;
                if(recorded && recorded!=location && historicalFid(old,p,length,pos,recorded,i+1))proven=true;
                pos+=length;
            }
            if(!proven)continue;
            trace("Windows zero FID origin VAT ICB",h.icb);trace("Windows zero FID origin directory ICB",physical(a,&h.map));
            trace("Windows zero FID retained child ICB",physical(childAddress));return true;
        }
        return false;
    }
    void fidTag(const Node &directory,const uint8_t *p,uint32_t size,size_t position,uint32_t location,bool &relocationProven) {
        auto recorded=get32(p+12);
        if(recorded==location){validateUdfTag(p,size,257,3,location);return;}
        // 用户明确授权的 Windows 偏差分支；先完整验证 tag/CRC，绝不放宽任意位置字段。
        validateUdfTag(p,size,257,3,recorded);
        if(!windowsTransaction || !microsoft(directory.fe))validateUdfTag(p,size,257,3,location);
        if(recorded) {
            require(recorded<v.capacity && historicalFid(directory,p,size,position,recorded),"Windows relocated FID lacks exact historical directory proof");
            relocationProven=true;
        } else {
            require(relocationProven && get16(p+36)==0,
                    "Windows zero-location FID lacks proven directory relocation profile");
            if(!(p[18]&4)) {
                auto childAddress=address(p+20);
                require(p[18]==0 && childAddress.reference==v.virtualReference,"Windows zero-location FID flags/reference differ");
                auto child=node(childAddress);
                require(get16(child.fe.data())==266 && child.type==5 && microsoft(child.fe) && uniqueId(child.fe)>=16 &&
                        uniqueId(child.fe)<v.nextUniqueId && get32(p+32)==uint32_t(uniqueId(child.fe)),"Windows zero-location FID child identity/type differs");
                require(newZeroChild(childAddress,v.vatIcb,v.nextUniqueId,0,child) || historicalZeroFid(directory,p,size,position,child),
                        "Windows zero-location FID lacks exact first-allocation VAT origin proof");
            }
        }
        v.tree.compatibility="windows-fid-location-v1";
        v.diagnostics+="UDF compatibility: windows-fid-location-v1 FID directory="+std::to_string(directory.icb.block)+
                       " byte="+std::to_string(position)+" expected="+std::to_string(location)+" recorded="+std::to_string(recorded)+
                       " checksum/CRC=PASS strict-conformance=NO\n";
    }
    uint32_t loadVat(uint32_t lba,bool latest) {
        require(lba>=v.partition,"VAT ICB precedes partition");auto n=node({lba-v.partition,v.physicalReference});
        require(n.type==248,"VAT ICB file type differs");auto bytes=data(n.entry,64*1024*1024);
        historyBytes+=bytes.size();require(historyBytes<=64*1024*1024,"VAT history resource byte limit");
        require(bytes.size()>=152,"Truncated UDF 2.01 VAT header");auto p=bytes.data();auto header=get16(p),impl=get16(p+2);
        require(header==152+uint32_t(impl) && header<=bytes.size() && (impl==0 || (impl>=32 && impl%4==0)) && (bytes.size()-header)%4==0,"Invalid VAT header/entry length");
        require(dstring(p+4,128)==v.tree.volumeName && get16(p+144)>=0x0200 && get16(p+144)<=0x0201 &&
                get16(p+146)==0x0201 && get16(p+148)==0x0201,"Unsupported VAT revision/volume identity");
        if(latest) {
            v.vatIcb=lba;v.previousVat=get32(p+132);v.files=get32(p+136);v.directories=get32(p+140);v.vatHeader.assign(bytes.begin(),bytes.begin()+header);v.vat.clear();
            v.nextUniqueId=get64(n.fe.data()+(get16(n.fe.data())==261?160:200));
            for(size_t i=header;i<bytes.size();i+=4){auto block=get32(p+i);require(block==UINT32_MAX || block<v.capacity,"VAT entry outside declared partition");v.vat.push_back(block);}
            require(!v.vat.empty(),"Empty VAT");trace("Latest VAT ICB",lba);
            windowsTransaction=microsoft(n.fe);
        } else {
            History h{lba,{},microsoft(n.fe),uniqueId(n.fe)};
            for(size_t i=header;i<bytes.size();i+=4){auto block=get32(p+i);require(block==UINT32_MAX || block<v.capacity,"Historical VAT entry outside declared partition");h.map.push_back(block);}
            history.push_back(std::move(h));
        }
        return get32(p+132);
    }
    void vat() {
        // 只搜索最新 Session 最后已记录 Track；丢失/损坏最新事务绝不回退历史目录。
        auto r=std::find_if(ranges.rbegin(),ranges.rend(),[&](auto range){return range.end<=end && range.begin>=v.session;});
        require(r!=ranges.rend(),"Latest UDF Session has no recorded Track");
        auto lower=std::max(r->begin,r->end-std::min(4096U,r->end-r->begin));
        source.prefetch(lower,r->end-lower);bool found=false;
        for(uint32_t at=r->end;at>lower;) {
            --at;auto b=raw(at);auto id=get16(b.data());
            if((id==261 || id==266) && b[27]==248) {loadVat(at,true);found=true;break;}
            require(b==Block{},"Latest VAT cannot be confirmed: non-padding data after/beside VAT candidate at LBA "+std::to_string(at));
        }
        require(found,"Latest VAT not found within bounded recorded tail");
        std::set<uint32_t> seen{v.vatIcb};auto previous=v.previousVat;
        for(size_t count=0;previous!=UINT32_MAX;++count) {
            require(count<1024 && previous<v.capacity && uint64_t(v.partition)+previous<v.vatIcb &&
                    seen.insert(v.partition+previous).second,"VAT history cycle/forward reference/resource limit");
            trace("Previous VAT ICB",v.partition+previous);previous=loadVat(v.partition+previous,false);
        }
    }
    void walk(UdfAddress a,UdfAddress parent,const std::string &path,std::set<std::pair<uint16_t,uint32_t>> &visited,int depth) {
        require(depth<128 && visited.size()<100000 && visited.emplace(a.reference,a.block).second,"UDF directory/ICB cycle or resource limit");
        auto n=node(a);require(n.type==4 || n.type==5,"VAT ICB in directory");v.icbs.emplace(path,a);trace("ICB "+path,physical(a));
        if(!path.empty()){auto entry=n.entry;if(entry.directory){entry.size=0;entry.sections.clear();}require(v.tree.entries.emplace(path,entry).second,"Duplicate UDF path");}
        if(n.type==5)return;
        auto bytes=data(n.entry,64*1024*1024);std::map<std::string,std::string> names;bool haveParent=false,relocationProven=false;
        for(size_t pos=0;pos<bytes.size();) {
            require(++fidRecords<=100000,"UDF FID record resource limit");
            require(bytes.size()-pos>=38,"Truncated UDF FID");auto p=bytes.data()+pos;auto impl=get16(p+36);auto len=p[19];
            auto size=(38U+impl+len+3U)&~3U;require(size<=BlockSize && size<=bytes.size()-pos,"Truncated/oversized UDF FID identifier");
            auto location=n.dataAddresses.empty()?a.block:n.dataAddresses.at(pos/BlockSize).block;
            if(p[18]&4) {
                // UDF 2.01 2.3.4.2：已删除且释放的 FID 保留名称，ICB 清零，压缩 ID 改为 254/255。
                require((p[18]&~7)==0 && impl==0 && len>1 && std::all_of(p+20,p+36,[](auto b){return b==0;}),
                        "Unsupported deleted UDF FID flags/ICB/implementation use");
                require(p[38]==254 || p[38]==255,"Invalid deleted UDF FID compression ID");
                std::vector<uint8_t> name(p+38,p+38+len);name[0]=name[0]==254?8:16;auto deleted=cs0(name.data(),name.size());validateName(deleted);
                fidTag(n,p,size,pos,location,relocationProven);
                require(get16(p+10)==size-16 && get16(p+16)==1,"Invalid deleted UDF FID CRC coverage/version");
                v.diagnostics+="Deleted FID excluded from current namespace: "+deleted+"\n";pos+=size;continue;
            }
            fidTag(n,p,size,pos,location,relocationProven);require(get16(p+10)==size-16 && get16(p+16)==1,"Invalid UDF FID CRC coverage/version");
            auto flags=p[18];require((flags&~0x0b)==0,"Unsupported UDF FID flags/deleted entry");auto child=address(p+20);
            if(flags&8){require(pos==0 && !haveParent && len==0 && flags==10 && child==parent,"Invalid UDF parent FID");haveParent=true;}
            else {
                require(len>0,"Empty UDF child name");auto name=cs0(p+38+impl,len);validateName(name);
                require(names.emplace(windowsKey(name),name).second,"UDF Windows name collision");auto full=path.empty()?name:path+"/"+name;
                auto childNode=node(child);require(get32(p+32)==uint32_t(uniqueId(childNode.fe)),"UDF FID UniqueID differs from ICB");
                walk(child,a,full,visited,depth+1);require(v.tree.entries.at(full).directory==bool(flags&2),"UDF FID/FE type mismatch");
            }
            pos+=size;
        }
        require(haveParent,"UDF directory missing parent FID");
    }
    void tree() {
        auto a=address(v.lvd.data()+248);auto abs=physical(a);v.fsd=raw(abs);validateUdfTag(v.fsd.data(),BlockSize,256,3,a.block);
        require(get16(v.fsd.data()+10)==496,"Truncated UDF FSD CRC coverage");
        require(get32(v.fsd.data()+448)==0 && get32(v.fsd.data()+464)==0,"Unsupported UDF FSD chain/system streams");
        v.root=address(v.fsd.data()+400);trace("FSD",abs);std::set<std::pair<uint16_t,uint32_t>> visited;walk(v.root,v.root,"",visited,0);
        uint32_t files=0,dirs=1;for(auto &[path,e]:v.tree.entries){(void)path;e.directory?++dirs:++files;}
        require(files==v.files && dirs==v.directories,"VAT file/directory counts differ from current tree");
        v.tree.viewIdentity=hashBytes([&](auto sink){
            sink(v.lvd.data(),BlockSize);sink(v.pd.data(),BlockSize);sink(v.fsd.data(),BlockSize);
            sink(v.vatHeader.data(),v.vatHeader.size());sink(v.vat.data(),v.vat.size()*sizeof(uint32_t));
            sink(&v.vatIcb,sizeof(v.vatIcb));
            sink(v.tree.compatibility.data(),v.tree.compatibility.size());
            for(auto &[path,a]:v.icbs){sink(path.data(),path.size());auto n=node(a);sink(n.fe.data(),BlockSize);if(n.type==4){auto directory=data(n.entry,64*1024*1024);sink(directory.data(),directory.size());}}
        });
        v.tree.diagnostics=v.diagnostics;
    }
};
}
void validateUdfTag(const uint8_t *p,size_t available,uint16_t id,uint16_t version,uint32_t location) {
    require(available>=16,"Truncated UDF descriptor tag");
    require(get16(p)==id && (version==2 || version==3) && get16(p+2)==version && get32(p+12)==location,
            "UDF tag identity/version/location mismatch: expected id="+std::to_string(id)+" version="+std::to_string(version)+
            " location="+std::to_string(location)+"; recorded id="+std::to_string(get16(p))+" version="+std::to_string(get16(p+2))+
            " location="+std::to_string(get32(p+12)));
    uint8_t sum=0;for(int i=0;i<16;++i)if(i!=4)sum+=p[i];auto n=get16(p+10);
    require(sum==p[4] && n>0 && n<=available-16 && crc(p+16,n)==get16(p+8),"UDF tag checksum/CRC mismatch");
}
VatVolume readUdfVolume(Blocks &source,uint32_t session,uint32_t end) {Reader r(source,session,end);r.volume();return r.v;}
VatVolume readVatVolume(Blocks &source,uint32_t session,uint32_t end) {
    Reader r(source,session,end);r.volume();require(r.v.revision==0x0201,"UDF VAT reader requires revision 2.01");r.vat();r.tree();return r.v;
}
}
