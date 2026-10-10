#include "DiscScanner.hpp"
#include "IsoVerifier.hpp"
#include "UdfSession.hpp"
#include <iostream>
#include <unistd.h>

using namespace ac;
template<class F> static void rejected(F f) {
    bool failed=false;try{f();}catch(const std::exception &){failed=true;}
    require(failed,"Recovery rejection was expected");
}
int main(int argc,char **argv) {
    try {
        require(argc==2,"Need the offline UDF fixture directory");
        IsoRuntime runtime;
        fs::path fixture=argv[1];
        char pattern[]="/private/tmp/appendcd-recovery-test-XXXXXX";
        auto name=mkdtemp(pattern);require(name,"Cannot create recovery test directory");
        fs::path out=name;
        struct Cleanup { fs::path path;~Cleanup(){std::error_code ec;fs::remove_all(path,ec);} } cleanup{out};
        auto source=std::make_shared<FileBlocks>(fixture/"source.iso");
        auto before=readUdfTree(*source,0,uint32_t(source->count));
        auto m=buildUdfSession(source,0,12128,uint32_t(source->count),fixture/"new",out/"recovery.iso",(fixture/"source.iso").string(),0,784);
        require(fs::file_size(out/"recovery.iso")==784*BlockSize,"Recovery Track size mismatch");
        verifySession(out/"recovery.iso",m,source,true);
        for(const auto &[p,e]:before.entries)if(!e.directory) {
            const auto &after=m.entries.at(p);
            require(after.old && after.sections.size()==e.sections.size(),"Old extent count changed");
            for(size_t i=0;i<e.sections.size();++i)require(after.sections[i].block==e.sections[i].block && after.sections[i].size==e.sections[i].size,"Old extent moved");
        }
        rejected([&]{buildUdfSession(source,0,12128,uint32_t(source->count),fixture/"new",out/"too-small.iso","",0,576);});
        rejected([&]{buildUdfSession(source,0,12128,uint32_t(source->count),fixture/"new",out/"unaligned.iso","",0,785);});
        DiscInfo d;d.readableEnd=101280;d.next=109040;d.freeBlocks=2188848;d.sessionStatus=1;d.physicalFirst=4;d.physicalLast=6;
        for(uint32_t n=4;n<=6;++n) {
            PhysicalTrack t;t.number=n;t.session=4;t.start=107440+(n-4)*800;t.nwa=t.start;t.nwaValid=true;
            t.size=t.free=n==6?2188848:784;t.reserved=n!=6;t.blank=true;t.incremental=1;t.dataMode=1;t.trackMode=4;d.physical.push_back(t);
        }
        auto plan=d.recoveryPlan();require(plan.track==4 && plan.start==107440 && plan.blocks==784 && plan.reservedCount==2,"Wrong recovery geometry");
        rejected([&]{d.requireNormalTarget();});
        d.physical[1].lraValid=true;rejected([&]{d.recoveryPlan();});d.physical[1].lraValid=false;
        d.physical[0].free=783;rejected([&]{d.recoveryPlan();});d.physical[0].free=784;
        d.physical[2].nwaValid=false;rejected([&]{d.recoveryPlan();});
        d.physical[2].nwaValid=true;d.last=107440;
        for(size_t i=0;i<2;++i){auto &t=d.physical[i];t.blank=false;t.reserved=false;t.nwaValid=false;t.free=0;t.lraValid=i==0;t.lastRecorded=i==0?108223:0;}
        auto finish=d.finishPlan();require(finish.start==107440 && finish.reservedCount==2,"Wrong finalization geometry");
        d.physical[1].lraValid=true;rejected([&]{d.finishPlan();});d.physical[1].lraValid=false;
        d.physical[0].lastRecorded=108222;rejected([&]{d.finishPlan();});
        DiscInfo cd;cd.media="CD-R";cd.sessionStatus=2;cd.next=12300;cd.freeBlocks=900;
        PhysicalTrack tail;tail.number=2;tail.start=tail.nwa=12300;tail.blank=tail.nwaValid=true;tail.free=1000;cd.physical.push_back(tail);
        cd.requireNormalTarget();require(cd.closedSession(),"Complete CD session is not recognized");
        rejected([&]{cd.recoveryPlan();});rejected([&]{cd.finishPlan();});
        cd.physical[0].nwaValid=false;rejected([&]{cd.requireNormalTarget();});
        std::cout<<"Recovery image/old extents/physical geometry checks passed; no optical device accessed.\n";
    } catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
