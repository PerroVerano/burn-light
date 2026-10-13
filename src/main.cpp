#include "DiscScanner.hpp"
#include "DiskArbitrationGuard.hpp"
#include "IsoVerifier.hpp"
#include "UdfReader.hpp"
#include "UdfSession.hpp"
#include "UdfVat.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <sys/stat.h>

using namespace ac;
namespace {
struct EvidenceOwner {
    fs::path path;
    ~EvidenceOwner() {
        if (path.empty() || geteuid() != 0) return;
        try {
            uid_t uid = 0; gid_t gid = 0;
            for (auto p = path.parent_path(); !p.empty(); p = p.parent_path()) {
                struct stat st{};
                if (stat(p.c_str(), &st) == 0 && st.st_uid != 0) { uid = st.st_uid; gid = st.st_gid; break; }
                if (p == p.root_path()) break;
            }
            if (!uid) return;
            if (fs::is_directory(path)) for (const auto &e : fs::recursive_directory_iterator(path)) lchown(e.path().c_str(), uid, gid);
            lchown(path.c_str(), uid, gid);
        } catch (...) {}
    }
};
struct Arguments {
    std::string command;
    std::vector<std::string> positional;
    std::map<std::string, std::string> options;
    explicit Arguments(int argc, char **argv) {
        require(argc >= 2, "Use appendcd help for usage"); command = argv[1];
        if (command != "init" && command != "init-build" && command != "drives" && command != "inspect" && command != "list" && command != "build" && command != "verify" && command != "readback" && command != "burn" && command != "recover" && command != "recover-finish" && command != "help" && command != "version") {
            positional.push_back(command); command = "burn";
        }
        for (int i = 2; i < argc; ++i) {
            std::string a = argv[i];
            if (a.starts_with("--")) {
                if(a=="--prepare-only") {require(command=="recover" && !options.contains(a),"--prepare-only is for recover only");options[a]="1";continue;}
                if(a=="--hash-files") {require((command=="inspect" || command=="list") && !options.contains(a),"--hash-files is for inspect/list only");options[a]="1";continue;}
                require(a == "--source" || a == "--source-base" || a == "--last" || a == "--next" || a == "--work-dir" || a == "--device" || a == "--on-conflict" || a=="--expected-view", "Unknown option: " + a);
                require(i + 1 < argc && !options.contains(a), "Missing/repeated option: " + a); options[a] = argv[++i];
            } else positional.push_back(a);
        }
        if (options.contains("--on-conflict")) {
            require(command == "build" || command == "burn", "--on-conflict is for build/burn only");
            require(get("--on-conflict") == "skip" || get("--on-conflict") == "replace", "--on-conflict must be skip or replace");
        }
        if(options.contains("--expected-view"))require((command=="build" || command=="burn") && get("--expected-view").size()==64 && get("--expected-view").find_first_not_of("0123456789abcdef")==std::string::npos,"--expected-view needs a build/burn VAT view hash");
    }
    std::string get(const std::string &key, const std::string &fallback = "") const { auto i = options.find(key); return i == options.end() ? fallback : i->second; }
    void count(size_t n) const { require(positional.size() == n, "Wrong argument count; use appendcd help"); }
    ConflictPolicy conflictPolicy() const {
        auto value = get("--on-conflict");
        return value == "skip" ? ConflictPolicy::Skip : value == "replace" ? ConflictPolicy::Replace : ConflictPolicy::Reject;
    }
};
void help() {
    std::cout << "appendcd 0.1.0 — native macOS libisofs multisession PoC\n"
              << "Physical target: automatically identified optical writer + appendable CD-R/DVD-R\n"
              << "With multiple drives specify --device bus,target,lun, as listed by drives.\n\n"
              << "appendcd drives\n"
              << "appendcd init-build FILES OUTPUT.iso (offline first-session image, LBA 0)\n"
              << "sudo appendcd init FILES [--work-dir DIR] (blank CD-R/DVD-R only)\n"
              << "sudo appendcd inspect [--work-dir DIR]\n"
              << "sudo appendcd list [--work-dir DIR]\n"
              << "  inspect/list --hash-files reads every regular file and records SHA256.\n"
              << "  build/burn --expected-view SHA256 binds a UDF VAT preview transaction.\n"
              << "sudo appendcd build FILES OUTPUT.iso [--work-dir DIR]\n"
              << "appendcd verify OUTPUT.iso [--source OLD.iso]\n"
              << "sudo appendcd readback OUTPUT.iso [--work-dir DIR] (read only)\n"
              << "sudo appendcd burn FILES [--work-dir DIR]\n"
              << "sudo appendcd recover FILES [--prepare-only] [--work-dir DIR]\n"
              << "sudo appendcd recover-finish OUTPUT.iso [--work-dir DIR] (verify recorded recovery, then close Session only)\n"
              << "sudo appendcd FILES [--work-dir DIR]\n\n"
              << "All physical commands accept --device BUS,TARGET,LUN (never a /dev path).\n"
              << "Offline list/inspect/build: --source DISC.iso --last LBA\n"
              << "Offline build also requires --next LBA (never inferred from image length).\n"
              << "For readcd session-only input add --source-base ABSOLUTE_LBA.\n"
              << "Input detection: ISO9660/Joliet, supported UDF 1.02, or CD-R/DVD-R UDF 2.01/VAT.\n"
              << "Output continues the detected filesystem; verify loads OUTPUT.iso.manifest.\n";
    std::cout << "build/burn: --on-conflict skip|replace explicitly handles same-path files.\n"
              << "Without this option existing files are rejected; directories merge, type/name collisions remain errors.\n"
              << "Replacement changes the latest Session directory; older recorded data is not erased.\n";
}
fs::path workDirectory(const Arguments &args) {
    fs::path parent = fs::absolute(args.get("--work-dir", "artifacts")); fs::create_directories(parent);
    std::string pattern = (parent / "run-XXXXXX").string(); std::vector<char> b(pattern.begin(), pattern.end()); b.push_back(0);
    auto path = mkdtemp(b.data()); require(path, "Cannot create evidence directory");
    std::cout << "Evidence directory: " << path << '\n'; return path;
}
void listTree(const Tree &tree) {
    for (const auto &[p, e] : tree) {
        std::cout << (e.directory ? "DIR  " : "FILE ") << std::quoted(p);
        if (!e.directory) { std::cout << " bytes=" << e.size; for (auto s : e.sections) std::cout << " LBA=" << s.block << '+' << s.size; }
        std::cout << '\n';
    }
}
void baselineHashes(Manifest &m, Blocks &source) {
    size_t done = 0;
    for (auto &[p, e] : m.entries) if (e.old && !e.directory) {
        e.sha = hashEntry(source, e); ++done;
        std::cerr << "Pre-burn optical baseline " << done << ": " << std::quoted(p) << '\n';
    }
}
void reportUdf(UdfTree &tree,Blocks &source,uint32_t last,const fs::path &evidence,bool hashes,bool listing=true) {
    std::cout<<"UDF volume: "<<tree.volumeName<<"\nFilesystem: "<<tree.filesystem<<"\n";
    if(!tree.viewIdentity.empty())std::cout<<"Filesystem view: "<<tree.viewIdentity<<'\n';
    if(!tree.compatibility.empty())std::cout<<"UDF compatibility: "<<tree.compatibility<<'\n';
    if(listing)listTree(tree.entries);
    if(!evidence.empty())writeText(evidence/"udf-view.txt",tree.diagnostics);
    if(hashes) {
        Manifest baseline;baseline.filesystem=tree.filesystem;baseline.importedStart=last;baseline.start=last;
        for(auto &[p,e]:tree.entries)if(!e.directory){e.sha=hashEntry(source,e);std::cout<<"SHA256 "<<std::quoted(p)<<' '<<e.sha<<'\n';}
        baseline.entries=tree.entries;
        if(!evidence.empty())baseline.save(evidence/"read-baseline.manifest");
    }
}
void opticalReadback(const DiscInfo &disc, const Manifest &m, const fs::path &image, const fs::path &evidence,bool allowIncomplete=false) {
    require(allowIncomplete || disc.closedSession(),"The latest Session is unfinished; readback alone cannot declare physical completion");
    require(disc.last == m.start && disc.next > disc.last, "Optical last session differs from prepared start LBA");
    require(shaFile(image) == m.imageSha, "Prepared image hash differs from manifest");
    OpticalBlocks readback(disc, evidence / (allowIncomplete?"before-close-read":"after-read"));
    auto blocks = uint32_t(fs::file_size(image) / BlockSize); readback.prefetch(m.start, blocks);
    auto hash = hashBytes([&](auto sink) { for (uint32_t i = 0; i < blocks; ++i) { auto b = readback.read(m.start + i); sink(b.data(), b.size()); } });
    require(hash == m.imageSha, "Optical continuation bytes differ from prepared image");
    verifyReadback(readback,m,blocks);
    if(m.filesystem=="udf2.01+vat") {
        auto tree=readUdfTree(readback,disc.last,disc.readableEnd);
        std::cout<<"Filesystem: "<<tree.filesystem<<"\nFilesystem view: "<<tree.viewIdentity<<'\n';
    }
    m.saveWindowsExpected(evidence/"windows-expected.json",disc.media);
    writeText(evidence/"result.txt",std::string(allowIncomplete?"INCOMPLETE_SESSION_READBACK_PASS":"PHYSICAL_READBACK_PASS")+"\nWINDOWS_ACCEPTANCE_PENDING\nMedia: "+disc.media+"\nSession start: "+std::to_string(m.start)+"\nImage SHA256: "+hash+"\n");
}
void unmountOptical(const DiscInfo &disc) {
    // BSD 编号可能重用，卸载前再核对它仍属于选定的光学服务。
    revalidateDrive(disc.drive,true);
    checkedRun({"/usr/sbin/diskutil","unmountDisk",disc.drive.bsdDevice});
}
void completePhysical(const DiscInfo &disc,const Manifest &m,const fs::path &image,const fs::path &evidence,bool recovery) {
    DiscInfo postPre;bool ready=false;
    for(int attempt=0;attempt<15;++attempt) {
        checkInterrupted();
        try {postPre=DiscScanner::preflight(disc.device);requireSameDrive(disc.drive,postPre.drive,false);if(postPre.media==disc.media && postPre.sessions==disc.sessions+1 && postPre.tracks==disc.tracks+(recovery?0:1)){ready=true;break;}}
        catch(const std::exception &error){writeText(evidence/("after-ready-attempt-"+std::to_string(attempt)+".txt"),error.what());}
        for(int i=0;i<20;++i){usleep(100000);checkInterrupted();}
    }
    require(ready,"Data was written, but a completed new Session was not confirmed within 30 seconds; no further write or close was attempted. Inspect the actual physical state before readback");
    writeText(evidence/"after-drutil.txt",postPre.drutil);
    unmountOptical(postPre);
    DiskArbitrationGuard postGuard;
    auto post=DiscScanner::scan(postPre,disc.device);post.save(evidence,"after");post.print();post.requireNormalTarget();
    require(post.last==m.start && post.next>post.last,"Post-burn last session start differs from prepared ms_block");
    opticalReadback(post,m,image,evidence);
    postGuard.restore();
    std::cout<<"Physical readback passed, "<<disc.media<<" remains appendable. Windows acceptance PENDING.\nEvidence: "<<evidence<<'\n';
}
}
int main(int argc, char **argv) {
    installSignals();
    fs::path evidence;
    EvidenceOwner ownership;
    try {
        Arguments a(argc, argv);
        if (a.command == "help") { help(); return 0; }
        if (a.command == "drives") { a.count(0);printDrives(opticalDrives());return 0; }
        if (a.command == "version") {
            int x, y, z; iso_lib_version(&x, &y, &z);
            std::cout << "appendcd 0.1.0, libisofs " << x << '.' << y << '.' << z << "\ncdrecord: " << CDRECORD_PATH << "\nreadcd: " << READCD_PATH << '\n'; return 0;
        }
        IsoRuntime runtime;
        if (a.command == "init-build") {
            a.count(2);
            require(a.options.empty(), "init-build is offline and accepts no device/source/LBA options");
            auto m = buildInitialSession(a.positional[0], a.positional[1]);
            verifySession(a.positional[1], m, nullptr);
            std::cout << "INITIAL IMAGE VERIFIED; Windows acceptance PENDING\n"; return 0;
        }
        if (a.command == "verify") {
            a.count(1); auto m = Manifest::load(a.positional[0] + ".manifest");
            auto source = a.get("--source", m.sourcePath);
            std::shared_ptr<Blocks> old;
            if (!source.empty()) old = std::make_shared<FileBlocks>(source, number(a.get("--source-base", std::to_string(m.sourceBase))));
            verifySession(a.positional[0], m, old);
            std::cout << "IMAGE VERIFIED; this does not prove Windows mounting\n"; return 0;
        }
        if (!a.get("--source").empty()) {
            require(a.command != "init" && a.command != "burn" && a.command != "recover" && a.command != "recover-finish" && a.command != "readback", "Offline images cannot select a physical burn/readback target");
            require(a.options.contains("--last"), "Offline source needs --last");
            uint32_t last = number(a.get("--last")), base = number(a.get("--source-base", "0"));
            auto path = fs::canonical(a.get("--source"));
            auto source = std::make_shared<FileBlocks>(path, base);
            require(source->count <= UINT32_MAX - base, "Offline source end LBA overflow");
            bool udf = isUdfSession(*source, last, uint32_t(source->count + base));
            if(a.options.contains("--expected-view"))require(udf && readUdfTree(*source,last,uint32_t(source->count+base)).viewIdentity==a.get("--expected-view"),"VAT view changed since preview");
            if (a.command == "build" && udf) {
                a.count(2); require(a.options.contains("--next"), "Offline UDF build needs --next");
                auto m = buildUdfSession(source,last,number(a.get("--next")),uint32_t(source->count+base),a.positional[0],a.positional[1],path.string(),base,0,a.conflictPolicy());
                verifySession(a.positional[1],m,source,false); std::cout << "UDF BUILD VERIFIED\n"; return 0;
            }
            if (a.command == "inspect" && udf) {
                a.count(0); auto tree=readUdfTree(*source,last,uint32_t(source->count+base));reportUdf(tree,*source,last,{},a.options.contains("--hash-files"));return 0;
            }
            if (a.command == "list" && udf) {
                a.count(0); auto tree = readUdfTree(*source, last, uint32_t(source->count + base));
                reportUdf(tree,*source,last,{},a.options.contains("--hash-files"));
                for (const auto &p : tree.inlineFiles) std::cout << "INLINE/UNMAPPED: " << std::quoted(p) << '\n';
                return 0;
            }
            if (a.command == "inspect") { a.count(0); printVolumes(readVolumes(*source, last)); return 0; }
            if (a.command == "list") { a.count(0); IsoImporter imported(source, last); listTree(imported.tree()); return 0; }
            a.count(2); require(a.options.contains("--next"), "Offline build needs --next from actual msinfo or an explicit synthetic test");
            auto m = buildSession(source, last, number(a.get("--next")), a.positional[0], a.positional[1], path.string(), base, a.conflictPolicy());
            verifySession(a.positional[1], m, source, false);
            std::cout << "BUILD VERIFIED (old extents checked; old contents not copied)\n"; return 0;
        }
        require(!a.options.contains("--last") && !a.options.contains("--next") && !a.options.contains("--source-base"), "Physical LBAs must come from the drive, not options");
        if (a.command == "build") a.count(2);
        else if (a.command == "init" || a.command == "burn" || a.command == "recover" || a.command == "recover-finish" || a.command == "readback") a.count(1);
        else a.count(0);
        auto pre = DiscScanner::preflight(a.get("--device"), a.command == "init" || a.command == "inspect"); // 在停止系统服务之前确认目标光驱与介质。
        require(a.command != "init" || pre.blank, "init requires a blank disc; use burn to append to an existing disc");
        require(geteuid() == 0, "Physical commands need sudo; an optical target was identified, but root access is unavailable");
        evidence = workDirectory(a);
        ownership.path = evidence;
        unmountOptical(pre);
        DiskArbitrationGuard guard;
        auto disc = DiscScanner::scan(pre, a.get("--device")); disc.save(evidence, "before"); disc.print();
        if (disc.blank) {
            disc.requireInitialTarget();
            if (a.command == "inspect") { guard.restore(); return 0; }
            require(a.command == "init", "Blank discs require the explicit init command");
            auto image = evidence / "session-first.iso";
            auto m = buildInitialSession(a.positional[0], image);
            verifySession(image, m, nullptr);
            m.saveWindowsExpected(evidence / "windows-expected.json", disc.media);
            recordInitialSession(disc, image);
            guard.restore();
            completePhysical(disc, m, image, evidence, false);
            return 0;
        }
        bool recovery=a.command=="recover";
        RecoveryPlan recoveryPlan;
        if(recovery)recoveryPlan=disc.recoveryPlan();
        else if(a.command=="burn" || a.command=="build")disc.requireNormalTarget();
        if(a.command=="readback") {
            auto image=fs::absolute(a.positional[0]);auto m=Manifest::load(image.string()+".manifest");
            opticalReadback(disc,m,image,evidence);guard.restore();std::cout<<"PHYSICAL READBACK PASS; Windows acceptance PENDING\n";return 0;
        }
        if(a.command=="recover-finish") {
            auto image=fs::absolute(a.positional[0]);auto m=Manifest::load(image.string()+".manifest");auto plan=disc.finishPlan();
            require(m.filesystem=="udf1.02" && m.start==plan.start && fs::file_size(image)==uint64_t(plan.blocks)*BlockSize,
                    "Finalization image/manifest does not match recorded recovery Track");
            opticalReadback(disc,m,image,evidence,true);
            OpticalBlocks padding(disc,evidence/"padding-read");
            for(size_t i=1;i+1<disc.physical.size();++i) {
                const auto &t=disc.physical[i];padding.prefetch(t.start,t.size);
                for(uint32_t j=0;j<t.size;++j)require(padding.read(t.start+j)==Block{},"Finalization refuses nonzero padding Track content");
            }
            writeText(evidence/"padding-result.txt","CLOSED_PADDING_TRACKS_ALL_ZERO_PASS\n");
            finalizeSession(disc,evidence);guard.restore();completePhysical(disc,m,image,evidence,true);return 0;
        }
        auto old = std::make_shared<OpticalBlocks>(disc, evidence / "before-read");
        probeFilesystem(*old, disc.last, evidence / "filesystem-probe.txt");
        bool udf = isUdfSession(*old, disc.last, disc.readableEnd);
        if(a.options.contains("--expected-view"))require(udf && readUdfTree(*old,disc.last,disc.readableEnd).viewIdentity==a.get("--expected-view"),"VAT view changed since preview");
        require(!recovery || udf,"Reserved-Track recovery currently requires the validated UDF 1.02 writer");
        if (udf && (a.command == "list" || a.command=="inspect")) {
            auto tree = readUdfTree(*old, disc.last, disc.readableEnd);
            reportUdf(tree,*old,disc.last,evidence,a.options.contains("--hash-files"),a.command=="list");
            for (const auto &p : tree.inlineFiles) std::cout << "INLINE/UNMAPPED: " << std::quoted(p) << '\n';
            guard.restore(); return 0;
        }
        if (a.command == "inspect") { guard.restore(); return 0; }
        // 独立读取 descriptors/目录，再让 libisofs 选择 Joliet。无有效 Joliet 时停止。
        if (!udf) { auto oldVolumes = readVolumes(*old, disc.last, UINT32_MAX, false); printVolumes(oldVolumes); }
        if (a.command == "list") { IsoImporter imported(old, disc.last); listTree(imported.tree()); guard.restore(); return 0; }
        fs::path image = a.command == "burn" || recovery ? evidence / "session-next.iso" : fs::absolute(a.positional[1]);
        auto start=recovery?recoveryPlan.start:disc.next;
        auto m = udf ? buildUdfSession(old,disc.last,start,disc.readableEnd,a.positional[0],image,old->snapshot().string(),0,recovery?recoveryPlan.blocks:0,a.conflictPolicy())
                     : buildSession(old, disc.last, disc.next, a.positional[0], image, old->snapshot().string(),0,a.conflictPolicy());
        EvidenceOwner imageOwner{image}, manifestOwner{image.string()+".manifest"};
        verifySession(image, m, old, false);
        if (a.command == "build") { guard.restore(); std::cout << "BUILD VERIFIED\n"; return 0; }
        auto fresh = std::make_shared<OpticalBlocks>(disc, evidence / "before-write-read");
        old->verifyCachedMetadata(*fresh);
        baselineHashes(m, *fresh); m.sourcePath = fresh->snapshot().string(); m.save(image.string() + ".manifest");
        m.saveWindowsExpected(evidence / "windows-expected.json",disc.media);
        verifySession(image, m, fresh, true);
        if(recovery && !a.get("--prepare-only").empty()) {
            writeText(evidence/"result.txt","RECOVERY_PREPARED_NOT_WRITTEN\nTrack: "+std::to_string(recoveryPlan.track)+"\nStart: "+std::to_string(m.start)+"\nBlocks: "+std::to_string(recoveryPlan.blocks)+"\n");
            guard.restore();std::cout<<"Recovery image and optical baselines verified; no write performed.\n";return 0;
        }
        // 同一光盘的旧 descriptors/content 基线已保存。紧接写入前重新核对 INQUIRY/msinfo/media。
        recordSession(disc, image, recovery);
        guard.restore();
        completePhysical(disc,m,image,evidence,recovery);
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "appendcd: " << e.what() << '\n';
        if (!evidence.empty()) { try { writeText(evidence / "failure.txt", e.what()); } catch (...) {} }
        return interrupted ? 128 + interrupted : 1;
    }
}
