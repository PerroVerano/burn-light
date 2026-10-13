#include "IsoVerifier.hpp"
#include "UdfSession.hpp"
#include <cstring>
#include <iostream>
#include <set>
#include <sstream>

namespace ac {
void probeFilesystem(Blocks &source, uint32_t start, const fs::path &report) {
    std::ostringstream out; bool udf = false, iso = false;
    source.prefetch(start + 16, 32);
    for (uint32_t i = 16; i < 48; ++i) {
        auto b = source.read(start + i);
        std::string id(reinterpret_cast<const char *>(b.data() + 1), 5);
        for (auto &c : id) if (c < 32 || c > 126) c = '.';
        out << "LBA " << start + i << " type=" << int(b[0]) << " id=" << id << " version=" << int(b[6]) << '\n';
        if (id == "BEA01" || id == "NSR02" || id == "NSR03" || id == "TEA01") udf = true;
        if (id == "CD001" && b[0] == 1) iso = true;
    }
    out << "UDF recognition sequence: " << (udf ? "present" : "not observed")
        << "\nISO9660 PVD in scanned range: " << (iso ? "present" : "not observed") << '\n';
    writeText(report, out.str());
    std::cout << "Filesystem recognition: UDF=" << (udf ? "present" : "absent") << ", ISO9660=" << (iso ? "present" : "absent")
              << "; details: " << report << '\n';
}
static uint32_t le32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
static uint32_t be32(const uint8_t *p) { return uint32_t(p[3]) | uint32_t(p[2]) << 8 | uint32_t(p[1]) << 16 | uint32_t(p[0]) << 24; }
static uint32_t both32(const uint8_t *p) { auto n = le32(p); require(n == be32(p + 4), "ISO both-endian 32-bit mismatch"); return n; }
static uint16_t both16(const uint8_t *p) {
    uint16_t n = uint16_t(p[0]) | uint16_t(p[1]) << 8;
    require(n == (uint16_t(p[3]) | uint16_t(p[2]) << 8), "ISO both-endian 16-bit mismatch"); return n;
}
static std::vector<uint8_t> readRange(Blocks &source, uint32_t start, uint32_t bytes) {
    require(bytes && bytes <= 64 * 1024 * 1024, "Invalid/oversized metadata range");
    std::vector<uint8_t> out(bytes);
    for (uint64_t offset = 0; offset < bytes; offset += BlockSize) {
        require(uint64_t(start) + offset / BlockSize <= UINT32_MAX, "LBA overflow");
        auto b = source.read(start + uint32_t(offset / BlockSize));
        memcpy(out.data() + offset, b.data(), std::min<uint64_t>(BlockSize, bytes - offset));
    }
    return out;
}
struct Directory { std::string path; uint32_t block, size, parent; };
class Parser {
    Blocks &source_;
    uint32_t start_, end_;
    std::set<uint32_t> visited_;
    std::vector<Directory> dirs_;
    Volume &volume_;
    void metadataBounds(uint32_t block, uint32_t bytes) {
        require(block && uint64_t(block) + (uint64_t(bytes) + BlockSize - 1) / BlockSize <= end_, "Metadata outside readable session");
        if (end_ != UINT32_MAX) require(block >= start_ + 16, "New metadata refers to an old session");
    }
    std::string name(const uint8_t *p, size_t size) {
        std::string n = volume_.joliet ? utf16Name(p, size) : std::string(reinterpret_cast<const char *>(p), size);
        if (n.ends_with(";1")) n.resize(n.size() - 2);
        if (!volume_.joliet && n.ends_with('.')) n.pop_back();
        require(!n.empty() && n.find('/') == n.npos && n.find('\0') == n.npos, "Invalid directory identifier");
        return n;
    }
    void directory(uint32_t extent, uint32_t size, uint32_t parent, const std::string &prefix, int depth) {
        require(depth < 128 && dirs_.size() < 100000, "Directory resource limit");
        require(visited_.insert(extent).second, "Directory loop or aliased directory extent");
        metadataBounds(extent, size); dirs_.push_back({prefix, extent, size, parent});
        auto data = readRange(source_, extent, size);
        std::map<std::string, std::string> seen;
        std::string continuation;
        bool dot = false, dotdot = false;
        for (size_t pos = 0; pos < data.size();) {
            auto length = data[pos];
            if (!length) { pos = (pos / BlockSize + 1) * BlockSize; continue; }
            require(length >= 34 && pos + length <= data.size() && pos % BlockSize + length <= BlockSize, "Directory record crosses sector/end");
            const uint8_t *r = data.data() + pos;
            require(r[1] == 0 && r[26] == 0 && r[27] == 0, "Extended attributes/interleaved data unsupported");
            require(both16(r + 28) == 1, "Unsupported volume sequence");
            uint32_t block = both32(r + 2), bytes = both32(r + 10);
            bool isDir = (r[25] & 2) != 0, more = (r[25] & 128) != 0;
            size_t nameSize = r[32]; require(nameSize && 33 + nameSize <= length, "Invalid identifier length");
            if (nameSize == 1 && r[33] < 2) {
                require(isDir && !more, "Invalid dot record");
                if (!r[33]) { require(!dot && block == extent && bytes == size, "Wrong self directory record"); dot = true; }
                else { require(!dotdot && block == parent, "Wrong parent directory record"); dotdot = true; }
            } else {
                std::string leaf = name(r + 33, nameSize), path = prefix + leaf;
                if (volume_.joliet) validateName(leaf);
                if (!continuation.empty()) require(path == continuation, "Broken multi-extent sequence");
                auto key = volume_.joliet ? windowsKey(leaf) : leaf;
                if (continuation.empty()) require(seen.emplace(key, path).second, "Duplicate/colliding directory entry: " + path);
                if (isDir) {
                    require(!more && bytes, "Invalid directory extent");
                    Entry e; e.directory = true; e.size = bytes; e.sections.push_back({block, bytes});
                    require(volume_.tree.emplace(path, e).second, "Duplicate directory");
                    directory(block, bytes, extent, path + "/", depth + 1);
                } else {
                    if (bytes) require(uint64_t(block) + (uint64_t(bytes) + BlockSize - 1) / BlockSize <= end_, "File extent outside readable image");
                    if (more) require(bytes && bytes % BlockSize == 0, "Non-final multi-extent must be block aligned");
                    auto &e = volume_.tree[path];
                    require(!e.directory && (e.sections.empty() || path == continuation), "Duplicate file extent");
                    e.size += bytes; e.sections.push_back({block, bytes});
                    continuation = more ? path : "";
                }
                require(volume_.tree.size() < 100000, "File resource limit");
            }
            pos += length;
        }
        require(dot && dotdot && continuation.empty(), "Incomplete directory/multi-extent");
    }
    struct PathItem { uint32_t extent; uint16_t parent; std::string name; bool operator==(const PathItem &) const = default; };
    std::vector<PathItem> table(uint32_t extent, bool big) {
        metadataBounds(extent, volume_.pathTableSize);
        auto bytes = readRange(source_, extent, volume_.pathTableSize);
        std::vector<PathItem> list;
        for (size_t pos = 0; pos < bytes.size();) {
            require(pos + 8 <= bytes.size(), "Truncated path table"); const auto *r = bytes.data() + pos;
            auto length = r[0]; require(length && r[1] == 0 && pos + 8 + length + (length & 1) <= bytes.size(), "Bad path table record");
            PathItem p; p.extent = big ? be32(r + 2) : le32(r + 2);
            p.parent = big ? uint16_t(r[6]) << 8 | r[7] : uint16_t(r[7]) << 8 | r[6];
            require(p.parent >= 1 && p.parent <= list.size() + 1, "Invalid path table parent");
            if (list.empty()) require(length == 1 && r[8] == 0 && p.parent == 1 && p.extent == volume_.root, "Invalid path table root");
            else {
                require(p.parent <= list.size(), "Non-root path table self-parent");
                p.name = name(r + 8, length);
            }
            list.push_back(p); pos += 8 + length + (length & 1);
        }
        return list;
    }
public:
    Parser(Blocks &s, uint32_t start, uint32_t end, Volume &v) : source_(s), start_(start), end_(end), volume_(v) {}
    void parse(bool validateTables) {
        directory(volume_.root, volume_.rootSize, volume_.root, "", 0);
        if (!validateTables) return;
        auto little = table(volume_.littleTable, false), big = table(volume_.bigTable, true);
        require(little == big && little.size() == dirs_.size(), "L/M path tables disagree with directories");
        std::map<std::string, uint32_t> paths{{"", volume_.root}};
        for (const auto &[p, e] : volume_.tree) if (e.directory) paths[p] = e.sections[0].block;
        std::vector<std::string> resolved{""};
        for (size_t i = 1; i < little.size(); ++i) {
            const auto &p = little[i]; auto parent = resolved[p.parent - 1];
            auto full = parent.empty() ? p.name : parent + "/" + p.name;
            require(paths.contains(full) && paths.at(full) == p.extent, "Path table extent/name mismatch: " + full);
            resolved.push_back(full);
        }
    }
};
std::vector<Volume> readVolumes(Blocks &source, uint32_t start, uint32_t end, bool validateTables) {
    require(start <= UINT32_MAX - 80, "Start LBA overflow");
    std::vector<Volume> volumes; bool terminated = false;
    for (uint32_t i = 16; i < 80; ++i) {
        auto b = source.read(start + i);
        if (memcmp(b.data() + 1, "BEA01", 5) == 0 || memcmp(b.data() + 1, "NSR0", 4) == 0)
            throw std::runtime_error("UDF-only/session recognition area at LBA " + std::to_string(start + i) + "; libisofs cannot import a UDF directory tree. Need a Windows ISO9660+Joliet initial session");
        require(memcmp(b.data() + 1, "CD001", 5) == 0 && b[6] == 1, "Invalid volume descriptor at LBA " + std::to_string(start + i));
        if (b[0] == 255) { terminated = true; break; }
        if (b[0] != 1 && b[0] != 2) continue;
        bool joliet = b[0] == 2 && b[88] == '%' && b[89] == '/' && (b[90] == '@' || b[90] == 'C' || b[90] == 'E');
        if (b[0] == 2 && !joliet) continue;
        require(both16(b.data() + 128) == BlockSize && both16(b.data() + 120) == 1 && both16(b.data() + 124) == 1, "Unsupported logical block size/volume set");
        require(b[156] >= 34 && b[157] == 0 && (b[181] & 2) && b[188] == 1 && b[189] == 0, "Invalid VD root record");
        Volume v{joliet, start + i, both32(b.data() + 80), both32(b.data() + 158), both32(b.data() + 166), both32(b.data() + 132), le32(b.data() + 140), be32(b.data() + 148), {}};
        // 导入的 Windows 小 Session 实测仅 31 blocks；32 是本 writer 的约束，
        // 不能当作所有已有 ISO/Joliet 卷的最小值。描述符仍必须位于卷范围内。
        require(v.spaceSize > i, "Invalid Volume Space Size");
        if (end != UINT32_MAX) {
            require(v.spaceSize >= 32, "Invalid generated Volume Space Size");
            require(v.spaceSize == end - start, "Volume Space Size differs from libisofs session block count");
        }
        Parser(source, start, end, v).parse(validateTables); volumes.push_back(std::move(v));
    }
    require(terminated, "Missing descriptor set terminator");
    size_t pvd = 0, svd = 0;
    for (const auto &v : volumes) { if (v.joliet) ++svd; else ++pvd; }
    require(pvd == 1 && svd == 1, "Expected exactly one PVD and one Joliet SVD");
    return volumes;
}
void printVolumes(const std::vector<Volume> &volumes) {
    for (const auto &v : volumes)
        std::cout << (v.joliet ? "Joliet SVD" : "ISO9660 PVD") << " LBA=" << v.descriptor
                  << " VolumeSpaceSize=" << v.spaceSize << " Root=" << v.root << " RootBytes=" << v.rootSize
                  << " PathTableBytes=" << v.pathTableSize << " L=" << v.littleTable << " M=" << v.bigTable << '\n';
}
static void compare(const std::vector<Volume> &volumes, Blocks &source, const Manifest &m, bool hashOld) {
    const Volume *joliet = nullptr, *primary = nullptr;
    for (const auto &v : volumes) (v.joliet ? joliet : primary) = &v;
    require(joliet && primary && joliet->tree.size() == m.entries.size(), "Expected tree entry count mismatch");
    std::multiset<std::string> primaryFiles, jolietFiles;
    auto signature = [](const Entry &e) {
        std::ostringstream s; for (auto x : e.sections) s << x.block << ':' << x.size << ','; return s.str();
    };
    for (const auto &[p, e] : primary->tree) if (!e.directory) primaryFiles.insert(signature(e));
    size_t oldCount = 0, newCount = 0, oldHashes = 0;
    for (const auto &[p, expected] : m.entries) {
        auto found = joliet->tree.find(p); require(found != joliet->tree.end(), "Joliet name missing/changed: " + p);
        const auto &actual = found->second;
        require(actual.directory == expected.directory, "Entry type mismatch: " + p);
        if (actual.directory) continue;
        require(actual.size == expected.size, "File size mismatch: " + p);
        jolietFiles.insert(signature(actual));
        if (expected.old) {
            // 空文件不占用数据块，生成器可以调整其占位地址。
            if (actual.size) require(actual.sections == expected.sections, "Old extent was relocated: " + p);
            for (auto s : actual.sections) if (s.size) require(s.block < m.start && uint64_t(s.block) + (uint64_t(s.size) + BlockSize - 1) / BlockSize <= m.start, "Old extent overlaps new session");
            ++oldCount;
        } else {
            for (auto s : actual.sections) require(s.block >= m.start + 16, "New file extent refers to old session: " + p);
            require(!expected.sha.empty(), "Missing new file hash: " + p); ++newCount;
        }
        if (!expected.old || (hashOld && !expected.sha.empty())) {
            require(hashEntry(source, actual) == expected.sha, "File SHA256 mismatch: " + p);
            if (expected.old) ++oldHashes;
        }
        std::cout << (expected.old ? "OLD " : "NEW ") << p << " bytes=" << actual.size;
        for (auto s : actual.sections) std::cout << " [LBA " << s.block << ", " << s.size << " bytes]";
        std::cout << '\n';
    }
    require(primaryFiles == jolietFiles, "ISO9660 and Joliet file extent sets differ");
    std::cout << "Verified: PVD/SVD, L/M path tables, tree, absolute LBAs, " << oldCount << " old files, "
              << newCount << " new files; old content hashes checked=" << oldHashes << '\n';
}
void verifySession(const fs::path &image, const Manifest &m, std::shared_ptr<Blocks> oldSource, bool hashOld) {
    require(shaFile(image) == m.imageSha, "Continuation ISO hash differs from manifest");
    auto next = std::make_shared<FileBlocks>(image, m.start);
    require(next->count <= UINT32_MAX - m.start, "Image end LBA overflow");
    OverlayBlocks combined(std::move(oldSource), next, m.start);
    if (m.filesystem == "udf1.02" || m.filesystem == "udf2.01+vat") { verifyUdfSession(combined, m, uint32_t(next->count), hashOld); return; }
    auto volumes = readVolumes(combined, m.start, m.start + uint32_t(next->count)); printVolumes(volumes);
    compare(volumes, combined, m, hashOld);
    // 独立解析器之外，再经 libisofs 重新导入实际输出，检查其 Joliet 选择。
    auto reader = std::make_shared<OverlayBlocks>(nullptr, next, m.start);
    IsoImporter imported(reader, m.start);
    auto tree = imported.tree();
    require(tree.size() == m.entries.size(), "libisofs reimport entry count mismatch");
    for (const auto &[p, e] : m.entries) require(tree.contains(p) && tree.at(p).directory == e.directory && (e.directory || tree.at(p).size == e.size), "libisofs reimport mismatch: " + p);
}
void verifyReadback(Blocks &disc, const Manifest &m, uint32_t imageBlocks) {
    if (m.filesystem == "udf1.02" || m.filesystem == "udf2.01+vat") {
        for (const auto &[p,e] : m.entries) if (!e.directory) require(!e.sha.empty(), "Readback requires pre-burn content hash: " + p);
        verifyUdfSession(disc, m, imageBlocks, true);
        std::cout << "OPTICAL UDF READBACK VERIFIED (Windows acceptance still required)\n"; return;
    }
    auto volumes = readVolumes(disc, m.start, m.start + imageBlocks); printVolumes(volumes);
    for (const auto &[p, e] : m.entries) if (!e.directory) require(!e.sha.empty(), "Readback requires pre-burn content hash: " + p);
    compare(volumes, disc, m, true);
    std::cout << "OPTICAL READBACK VERIFIED (Windows acceptance still required)\n";
}
}
