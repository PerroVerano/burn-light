#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>
#include <sys/types.h>
#include <string>

// Exercise the actual backend policy with deterministic read-only MMC replies.
struct SCSI {};
struct disk_info { int first_track_ls, first_track_ls_msb, last_track_ls, last_track_ls_msb, disk_status, sess_status; };
struct rzone_info {
    uint8_t data_len[2], rzone_start[4], next_recordable_addr[4], free_blocks[4], rzone_size[4], last_recorded_addr[4];
    int rzone_num_lsb, rzone_num_msb, border_num_lsb, border_num_msb;
    int rt, blank, damage, lra_v, nwa_v, incremental, datamode, trackmode;
};
struct track_info {
    uint8_t data_len[2],track_start[4],next_writable_addr[4],free_blocks[4],track_size[4];
    int track_number,session_number,nwa_valid,blank,rt,damage,packet,fp,data_mode,track_mode;
};
static disk_info media;
static track_info cdReply;
static int profile=0x11;
static std::map<int,rzone_info> replies;
static int closeCalls;
static long a_to_4_byte(const uint8_t *b) { return (long(b[0])<<24)|(long(b[1])<<16)|(long(b[2])<<8)|b[3]; }
static int a_to_2_byte(const uint8_t *b) { return (b[0]<<8)|b[1]; }
static void put4(uint8_t *b,uint32_t n) { for(int i=3;i>=0;--i){b[i]=uint8_t(n);n>>=8;} }
static void fillbytes(caddr_t p,size_t n,int c) { std::memset(p,c,n); }
static int get_curprofile(SCSI *) { return profile; }
static int get_diskinfo(SCSI *,disk_info *d,int) { *d=media;return 0; }
static int get_trackinfo(SCSI *,caddr_t b,int,int t,int) {
    if(profile==0x09){if(t!=0xFF)return -1;*reinterpret_cast<track_info *>(b)=cdReply;return 0;}
    if(!replies.contains(t))return -1;*reinterpret_cast<rzone_info *>(b)=replies.at(t);return 0;
}
[[noreturn]] static void comerrno(int,const char *m,...) { throw std::runtime_error(m); }
static int scsi_close_tr_session(SCSI *,int,int t,int) {
    ++closeCalls;auto &r=replies.at(t);r.blank=0;r.rt=0;r.lra_v=0;r.nwa_v=0;put4(r.free_blocks,0);return 0;
}
#define EX_BAD 1
#define TI_TYPE_TRACK 1
#define CL_TYPE_TRACK 1
#define FALSE 0
#define APPENDCD_BACKEND_IMPLEMENTATION
#include "../backend/appendcd_backend.h"
#include "../backend/appendcd_identity.h"

static rzone_info track(int n,uint32_t start,uint32_t size,bool reserved) {
    rzone_info r{};r.data_len[1]=50;r.rzone_num_lsb=n;r.border_num_lsb=4;
    r.blank=1;r.rt=reserved;r.nwa_v=!reserved;r.incremental=1;r.datamode=1;r.trackmode=4;
    put4(r.rzone_start,start);put4(r.next_recordable_addr,reserved?0:start);put4(r.free_blocks,size);put4(r.rzone_size,size);return r;
}
static void normal() {
    profile=0x11;
    media={4,0,4,0,1,0};replies={{4,track(4,107440,100000,false)}};closeCalls=0;
    appendcd_reuse=0;appendcd_physical_track=0;appendcd_reserved_count=0;appendcd_expected_nwa=107440;
    appendcd_initial=0;
}
static void cd() {
    normal();profile=0x09;media={1,0,1,0,1,2};cdReply={};
    cdReply.data_len[1]=30;cdReply.track_number=2;cdReply.session_number=2;cdReply.nwa_valid=1;cdReply.blank=1;
    put4(cdReply.track_start,12300);put4(cdReply.next_writable_addr,12300);put4(cdReply.free_blocks,300000);
    appendcd_expected_nwa=12300;
}
static void recovery() {
    normal();media={4,0,6,0,1,1};
    replies={{4,track(4,107440,784,true)},{5,track(5,108240,784,true)},{6,track(6,109040,2188848,false)}};
    appendcd_reuse=1;appendcd_physical_track=4;appendcd_reserved_count=2;
}
static void check(bool c) { if(!c)throw std::runtime_error("backend policy assertion failed"); }
template<class F> static void rejected(F f) { bool failed=false;try{f();}catch(const std::runtime_error &){failed=true;}check(failed); }
int main() {
    check(appendcd_registry_matches("4294967297",4294967297ULL));
    check(!appendcd_registry_matches("4294967298",4294967297ULL));
    check(!appendcd_registry_matches("-1",UINT64_MAX));
    check(!appendcd_registry_matches("18446744073709551616",1));
    check(appendcd_registry_matches("18446744073709551615",UINT64_MAX));
    check(!appendcd_registry_matches("",1) && !appendcd_registry_matches("1,0,0",1));
    SCSI s;long address=0;
    normal();check(appendcd_select_track(&s,&address)==4&&address==107440);
    appendcd_expected_nwa=108240;rejected([&]{appendcd_select_track(&s,&address);});check(closeCalls==0);
    normal();replies.at(4).nwa_v=0;rejected([&]{appendcd_select_track(&s,&address);});
    recovery();appendcd_reuse=0;appendcd_physical_track=0;appendcd_expected_nwa=109040;rejected([&]{appendcd_select_track(&s,&address);});
    recovery();check(appendcd_select_track(&s,&address)==4&&address==107440);appendcd_check_reserved_size(&s,4,784);
    rejected([&]{appendcd_check_reserved_size(&s,4,768);});check(closeCalls==0);
    recovery();replies.at(5).lra_v=1;rejected([&]{appendcd_select_track(&s,&address);});
    recovery();replies.at(6).nwa_v=0;rejected([&]{appendcd_select_track(&s,&address);});
    recovery();rejected([&]{appendcd_close_empty_tracks(&s);});check(closeCalls==0);
    recovery();auto &first=replies.at(4);first.blank=0;first.rt=0;first.lra_v=1;first.nwa_v=0;put4(first.free_blocks,0);put4(first.last_recorded_addr,108223);
    appendcd_close_empty_tracks(&s);check(closeCalls==1&&!replies.at(5).blank&&a_to_4_byte(replies.at(5).free_blocks)==0);
    appendcd_close_empty_tracks(&s);check(closeCalls==1);appendcd_validate_finish(&s);check(appendcd_checked_track==4);
    replies.at(5).lra_v=1;rejected([&]{appendcd_validate_finish(&s);});
    recovery();auto &f=replies.at(4);f.blank=0;f.lra_v=1;put4(f.free_blocks,0);replies.at(5).blank=0;
    rejected([&]{appendcd_close_empty_tracks(&s);});check(closeCalls==0);
    cd();appendcd_validate_cd(&s);check(closeCalls==0);
    cdReply.nwa_valid=0;rejected([&]{appendcd_validate_cd(&s);});
    cd();appendcd_expected_nwa=12301;rejected([&]{appendcd_validate_cd(&s);});
    cd();media.sess_status=1;rejected([&]{appendcd_validate_cd(&s);});
    cd();cdReply.packet=1;rejected([&]{appendcd_validate_cd(&s);});
    cd();cdReply.data_len[1]=26;rejected([&]{appendcd_validate_cd(&s);});
    cd();appendcd_reuse=1;rejected([&]{appendcd_validate_cd(&s);});
    // 首刻只能通过独立标志进入；原追加路径仍拒绝 blank/NWA 0。
    normal();media={1,0,1,0,0,0};replies={{1,track(1,0,2297888,false)}};appendcd_expected_nwa=0;
    rejected([&]{appendcd_select_track(&s,&address);});
    appendcd_initial=1;check(appendcd_select_track(&s,&address)==1 && address==0 && closeCalls==0);
    media.disk_status=1;rejected([&]{appendcd_select_track(&s,&address);});
    normal();appendcd_initial=1;rejected([&]{appendcd_select_track(&s,&address);});
    recovery();appendcd_initial=1;rejected([&]{appendcd_select_track(&s,&address);});
    cd();media={1,0,1,0,0,0};put4(cdReply.track_start,0);put4(cdReply.next_writable_addr,0);appendcd_expected_nwa=0;
    rejected([&]{appendcd_validate_cd(&s);});
    appendcd_initial=1;appendcd_validate_cd(&s);check(closeCalls==0);
    put4(cdReply.next_writable_addr,1);rejected([&]{appendcd_validate_cd(&s);});
    cd();appendcd_initial=1;rejected([&]{appendcd_validate_cd(&s);});
    std::puts("Optical identity, DVD recovery and CD-R invisible Track/NWA policy checks passed; no physical device accessed.");
}
