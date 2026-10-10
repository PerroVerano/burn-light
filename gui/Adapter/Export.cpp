#include "DiscScanner.hpp"
#include "DiskArbitrationGuard.hpp"
#include "UdfReader.hpp"
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

using namespace ac;
namespace {
struct FD {
    int value;
    explicit FD(int v) : value(v) { require(v >= 0, "Cannot open export object"); }
    ~FD() { close(value); }
    FD(const FD &) = delete;
};
struct Owner {
    fs::path path;
    uid_t uid = 0;
    gid_t gid = 0;
    ~Owner() {
        if (geteuid() != 0 || !uid || path.empty()) return;
        try {
            for (const auto &e : fs::recursive_directory_iterator(path))
                require(lchown(e.path().c_str(), uid, gid) == 0, "Cannot return evidence ownership");
            lchown(path.c_str(), uid, gid);
        } catch (...) { /* 未移交成功时 GUI 读取失败，不宣称成功。 */ }
    }
};
std::vector<std::string> parts(const std::string &path) {
    require(!path.empty() && path.size() <= 4096 && path.front() != '/' && path.back() != '/', "Unsafe export path");
    std::istringstream in(path); std::string part; std::vector<std::string> out;
    while (std::getline(in, part, '/')) { validateName(part); out.push_back(part); }
    require(out.size() <= 128, "Export directory depth limit");
    return out;
}
std::string json(const std::string &s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        require(c >= 32 && c != 127, "Control character in export metadata");
        if (c == '"' || c == '\\') out += '\\';
        out += char(c);
    }
    return out + '"';
}
Tree selection(const fs::path &file, const Tree &tree) {
    FD fd(open(file.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
    struct stat st{}; require(fstat(fd.value, &st) == 0 && S_ISREG(st.st_mode) && st.st_size <= 8 * 1024 * 1024, "Invalid selection file");
    std::string text(size_t(st.st_size), '\0'); size_t at = 0;
    while (at < text.size()) { auto n = read(fd.value, text.data() + at, text.size() - at); if (n < 0 && errno == EINTR) continue; require(n > 0, "Cannot read selection"); at += size_t(n); }
    std::istringstream in(text); std::string line;
    require(bool(std::getline(in, line)) && line == "BURNLIGHT_EXPORT_SELECTION 1", "Invalid selection version");
    require(bool(std::getline(in, line)) && (line == "ALL" || line == "SELECTED"), "Invalid selection mode");
    bool all = line == "ALL"; Tree selected;
    while (std::getline(in, line)) {
        std::istringstream row(line); std::string path, type; uint64_t bytes = 0;
        require(bool(row >> std::quoted(path) >> type >> bytes) && (row >> std::ws).eof(), "Invalid selection record");
        parts(path); auto e = tree.find(path);
        require(e != tree.end() && type == (e->second.directory ? "D" : "F") && bytes == e->second.size, "Disc directory changed; inspect again: " + path);
        require(selected.emplace(path, e->second).second, "Repeated selection path");
    }
    require(!selected.empty() && selected.size() <= 100000, "Empty/oversized selection");
    if (all) require(selected.size() == tree.size(), "Disc directory changed; inspect again");
    for (const auto &[path, e] : selected) {
        auto p = fs::path(path).parent_path();
        while (!p.empty()) { require(selected.contains(p.string()) && selected.at(p.string()).directory, "Missing parent directory"); p = p.parent_path(); }
        (void)e;
    }
    return selected;
}
Tree imported(std::shared_ptr<Blocks> source, uint32_t last, uint32_t end, std::string &filesystem) {
    if (isUdfSession(*source, last, end)) {
        filesystem = "udf1.02"; auto udf = readUdfTree(*source, last, end);
        require(udf.inlineFiles.empty(), "Inline/unmapped UDF files are not supported by the validated reader");
        return udf.entries;
    }
    filesystem = "iso9660+joliet"; IsoImporter importer(source, last); return importer.tree();
}
void extract(Blocks &source, Tree &tree, uint32_t end, const fs::path &root) {
    require(mkdir(root.c_str(), 0700) == 0, "Export content already exists");
    FD base(open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    for (auto &[path, e] : tree) {
        checkInterrupted(); auto components = parts(path);
        FD parent(dup(base.value));
        for (size_t i = 0; i + 1 < components.size(); ++i) {
            int next = openat(parent.value, components[i].c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            require(next >= 0, "Missing export directory"); close(parent.value); parent.value = next;
        }
        const auto &name = components.back();
        if (e.directory) { require(mkdirat(parent.value, name.c_str(), 0700) == 0, "Directory collision"); continue; }
        FD output(openat(parent.value, name.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
        e.sha = hashBytes([&](auto sink) {
            uint64_t total = 0;
            for (auto section : e.sections) {
                // 空文件的 ISO extent 地址可能在镜像末尾之外；零长度不读取任何块。
                if (!section.size) continue;
                uint64_t blocks = (uint64_t(section.size) + BlockSize - 1) / BlockSize;
                require(uint64_t(section.block) + blocks <= end, "File extent exceeds recorded media");
                source.prefetch(section.block, uint32_t(blocks));
                for (uint64_t offset = 0; offset < section.size; offset += BlockSize) {
                    auto b = source.read(section.block + uint32_t(offset / BlockSize));
                    auto n = size_t(std::min<uint64_t>(BlockSize, section.size - offset)); sink(b.data(), n);
                    size_t written = 0;
                    while (written < n) { auto v = write(output.value, b.data() + written, n - written); if (v < 0 && errno == EINTR) continue; require(v > 0, "Export write failed"); written += size_t(v); }
                }
                total += section.size;
            }
            require(total == e.size, "File extent size mismatch");
        });
        require(fsync(output.value) == 0 && shaFile(root / path) == e.sha, "Export cache hash differs from optical content");
        std::cerr << "Export verified: " << std::quoted(path) << " bytes=" << e.size << '\n';
    }
}
void receipt(const fs::path &evidence, const Tree &tree, const std::string &filesystem, const std::string &sourceKind, const DiscInfo &disc) {
    std::ostringstream out;
    out << "{\"schema\":1,\"sourceKind\":" << json(sourceKind) << ",\"filesystem\":" << json(filesystem)
        << ",\"device\":" << json(disc.device) << ",\"registryID\":" << json(std::to_string(disc.drive.registryId))
        << ",\"media\":" << json(disc.media) << ",\"entries\":[";
    bool first = true;
    for (const auto &[path, e] : tree) {
        if (!first) out << ','; first = false;
        out << "{\"path\":" << json(path) << ",\"directory\":" << (e.directory ? "true" : "false") << ",\"bytes\":" << e.size
            << ",\"sha256\":" << (e.directory ? "null" : json(e.sha)) << '}';
    }
    out << "]}\n"; writeText(evidence / "export.json", out.str());
    writeText(evidence / "result.txt", sourceKind == "optical" ? "OPTICAL_EXPORT_VERIFIED\n" : "OFFLINE_EXPORT_VERIFIED\n");
}
}
int main(int argc, char **argv) {
    Owner owner;
    try {
        installSignals(); IsoRuntime runtime;
        require(argc >= 2 && (std::string(argv[1]) == "export" || std::string(argv[1]) == "offline-export"), "Use export or offline-export");
        bool offline = std::string(argv[1]) == "offline-export";
        std::map<std::string, std::string> options;
        for (int i = 2; i < argc; i += 2) {
            std::string key = argv[i];
            const std::set<std::string> allowed = offline ? std::set<std::string>{"--image", "--last", "--selection", "--work-dir"}
                : std::set<std::string>{"--device", "--registry-id", "--bsd-node", "--media", "--selection", "--work-dir"};
            require(allowed.contains(key) && i + 1 < argc && options.emplace(key, argv[i + 1]).second, "Unknown/missing/repeated export option");
        }
        require(options.contains("--selection") && options.contains("--work-dir"), "Missing export paths");
        auto parent = fs::canonical(options.at("--work-dir"));
        struct stat st{}; require(lstat(parent.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && (st.st_mode & 077) == 0, "Export evidence parent must be private");
        if (!offline) {
            require(geteuid() == 0 && st.st_uid != 0, "Optical export needs system authorization and a user job");
            require(std::regex_match(parent.string(), std::regex("/private/tmp/burnlight-job-[A-Fa-f0-9-]{36}/optical")), "Export must use a GUI private job");
            require(fs::canonical(options.at("--selection")) == parent.parent_path() / "export-selection.txt", "Selection must belong to this GUI job");
        } else require(geteuid() != 0, "Offline export runs as the user");
        owner.uid = st.st_uid; owner.gid = st.st_gid;
        std::string pattern = (parent / "run-XXXXXX").string(); std::vector<char> b(pattern.begin(), pattern.end()); b.push_back(0);
        require(mkdtemp(b.data()) != nullptr, "Cannot create private export evidence"); owner.path = b.data();
        std::cout << "Evidence directory: " << owner.path.string() << '\n';
        DiscInfo disc; std::string filesystem;
        if (offline) {
            require(options.contains("--image") && options.contains("--last"), "Missing offline source/session");
            auto source = std::make_shared<FileBlocks>(options.at("--image"));
            require(source->count <= UINT32_MAX, "Offline image too large");
            auto tree = selection(options.at("--selection"), imported(source, number(options.at("--last")), uint32_t(source->count), filesystem));
            extract(*source, tree, uint32_t(source->count), owner.path / "files");
            receipt(owner.path, tree, filesystem, "offline", disc); std::cout << "OFFLINE EXPORT VERIFIED\n"; return 0;
        }
        for (auto key : {"--device", "--registry-id", "--bsd-node", "--media"}) require(options.contains(key), "Missing expected drive identity");
        auto pre = DiscScanner::preflight(options.at("--device"));
        require(std::to_string(pre.drive.registryId) == options.at("--registry-id") && pre.drive.bsdDevice == options.at("--bsd-node") && pre.media == options.at("--media"), "Selected optical identity/media changed");
        revalidateDrive(pre.drive, true);
        checkedRun({"/usr/sbin/diskutil", "unmountDisk", pre.drive.bsdDevice});
        DiskArbitrationGuard guard;
        disc = DiscScanner::scan(pre, pre.device);
        require(!disc.blank && disc.closedSession(), "Export requires a complete recorded Session");
        disc.save(owner.path, "before"); disc.print();
        auto source = std::make_shared<OpticalBlocks>(disc, owner.path / "source-read");
        auto tree = selection(options.at("--selection"), imported(source, disc.last, disc.readableEnd, filesystem));
        DiscScanner::revalidate(disc);
        extract(*source, tree, disc.readableEnd, owner.path / "files");
        // 新的实际 readcd 缓存复读 metadata 和所选 payload；换盘或内容变化均失败。
        DiscScanner::revalidate(disc);
        OpticalBlocks fresh(disc, owner.path / "verification-read"); source->verifyCachedMetadata(fresh);
        for (const auto &[path, e] : tree) if (!e.directory) require(hashEntry(fresh, e) == e.sha, "Fresh optical hash mismatch: " + path);
        DiscScanner::revalidate(disc); guard.restore();
        receipt(owner.path, tree, filesystem, "optical", disc);
        std::cout << "OPTICAL EXPORT VERIFIED\n"; return 0;
    } catch (const std::exception &error) { std::cerr << "burnlight-export: " << error.what() << '\n'; return 1; }
}
