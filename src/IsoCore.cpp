#include "IsoCore.hpp"
#include "GeneratedMetadata.hpp"
#include <cerrno>
#include <algorithm>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace ac {
static void isoCheck(int result, const std::string &operation) {
    require(result > 0, operation + ": " + iso_error_to_msg(result));
}
FileBlocks::FileBlocks(const fs::path &path, uint32_t base) : fd_(-1), base_(base) {
    fd_ = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    require(fd_ >= 0, "Cannot open ISO: " + path.string());
    try {
    struct stat s{};
    if (fstat(fd_, &s) != 0 || !S_ISREG(s.st_mode) || s.st_size % BlockSize) {
        throw std::runtime_error("ISO must be a regular file of 2048-byte blocks");
    }
    count = uint64_t(s.st_size) / BlockSize;
    require(count<=UINT32_MAX-base_,"File block address overflow");
    if (fs::exists(path.string()+".tracks")) {
        std::istringstream tracks(readText(path.string()+".tracks"));std::string line;
        while(std::getline(tracks,line)) {
            std::istringstream row(line);Range r;
            require(bool(row>>r.begin>>r.end>>r.track>>r.session) && (row>>std::ws).eof(),"Malformed recorded Track map");
            require(r.begin<r.end && r.begin>=base_ && uint64_t(r.end)<=base_+count &&
                    (ranges_.empty() || r.begin>=ranges_.back().end),"Invalid recorded Track map");
            ranges_.push_back(r);
        }
        require(tracks.eof() && !ranges_.empty(),"Malformed recorded Track map");
    } else ranges_.push_back({base_,uint32_t(base_+count)});
    if (fs::exists(path.string() + ".blocks")) {
        sparse_ = true; std::ifstream index(path.string() + ".blocks");std::string line;
        require(bool(index),"Cannot open sparse cache validity map");
        while(std::getline(index,line)) {
            std::istringstream row(line);uint64_t lba;
            require(bool(row>>lba) && (row>>std::ws).eof() && lba>=base_ && lba<uint64_t(base_)+count,"Malformed sparse cache validity map");
            valid_.insert(uint32_t(lba));
        }
        require(index.eof(),"Cannot read sparse cache validity map");
    }
    } catch(...) {close(fd_);fd_=-1;throw;}
}
FileBlocks::~FileBlocks() { if (fd_ >= 0) close(fd_); }
Block FileBlocks::read(uint32_t lba) {
    checkInterrupted(); require(lba >= base_ && uint64_t(lba - base_) < count, "LBA outside ISO: " + std::to_string(lba));
    require(!sparse_ || valid_.contains(lba), "Requested LBA is a hole in optical cache: " + std::to_string(lba));
    require(std::any_of(ranges_.begin(),ranges_.end(),[&](auto r){return lba>=r.begin && lba<r.end;}),"LBA in an unrecorded Track/gap: "+std::to_string(lba));
    Block b{};
    require(pread(fd_, b.data(), b.size(), off_t(lba - base_) * BlockSize) == ssize_t(BlockSize), "ISO read error at LBA " + std::to_string(lba));
    ++reads; return b;
}
OverlayBlocks::OverlayBlocks(std::shared_ptr<Blocks> oldSource, std::shared_ptr<Blocks> nextSource, uint32_t start)
    : old_(std::move(oldSource)), next_(std::move(nextSource)), start_(start) {}
Block OverlayBlocks::read(uint32_t lba) {
    ++reads;
    if (lba >= start_) return next_->read(lba);
    require(bool(old_), "Old extent needs --source or a live optical read: LBA " + std::to_string(lba));
    return old_->read(lba);
}
std::vector<Blocks::Range> OverlayBlocks::recordedRanges() const {
    auto out=old_?old_->recordedRanges():std::vector<Range>{};
    out.erase(std::remove_if(out.begin(),out.end(),[&](auto r){return r.begin>=start_;}),out.end());
    for(auto &r:out)r.end=std::min(r.end,start_);
    auto next=next_->recordedRanges(); out.insert(out.end(),next.begin(),next.end()); return out;
}
IsoRuntime::IsoRuntime() {
    isoCheck(iso_init(), "iso_init");
    isoCheck(iso_set_local_charset(const_cast<char *>("UTF-8"), 0), "UTF-8 charset");
    iso_set_msgs_severities(const_cast<char *>("NEVER"), const_cast<char *>("WARNING"), const_cast<char *>("libisofs: "));
}
IsoRuntime::~IsoRuntime() { iso_finish(); }
static IsoDataSource *dataSource(std::shared_ptr<Blocks> blocks) {
    auto src = static_cast<IsoDataSource *>(calloc(1, sizeof(IsoDataSource)));
    require(src, "Cannot allocate data source");
    src->refcount = 1;
    src->data = new std::shared_ptr<Blocks>(std::move(blocks));
    src->open = [](IsoDataSource *) { return 1; };
    src->close = [](IsoDataSource *) { return 1; };
    src->read_block = [](IsoDataSource *s, uint32_t lba, uint8_t *buffer) {
        try {
            auto b = (*static_cast<std::shared_ptr<Blocks> *>(s->data))->read(lba);
            memcpy(buffer, b.data(), b.size()); return 1;
        } catch (const std::exception &e) {
            std::cerr << "Block source: " << e.what() << '\n'; return int(ISO_DATA_SOURCE_FAILURE);
        }
    };
    src->free_data = [](IsoDataSource *s) { delete static_cast<std::shared_ptr<Blocks> *>(s->data); };
    return src;
}
IsoImporter::IsoImporter(std::shared_ptr<Blocks> source, uint32_t start) {
    isoCheck(iso_image_new("APPENDCD", &image_), "iso_image_new");
    std::unique_ptr<IsoDataSource, decltype(&iso_data_source_unref)> src(dataSource(std::move(source)), iso_data_source_unref);
    IsoReadOpts *raw = nullptr;
    try {
        isoCheck(iso_read_opts_new(&raw, 0), "read options");
        std::unique_ptr<IsoReadOpts, decltype(&iso_read_opts_free)> opts(raw, iso_read_opts_free);
        isoCheck(iso_read_opts_set_start_block(raw, start), "last session start");
        isoCheck(iso_read_opts_set_preferjoliet(raw, 1), "prefer Joliet");
        isoCheck(iso_read_opts_set_no_iso1999(raw, 1), "disable ISO1999 tree");
        isoCheck(iso_read_opts_set_joliet_map(raw, 1), "strip Joliet version suffix");
        isoCheck(iso_read_opts_keep_import_src(raw, 1), "keep imported source");
        IsoReadImageFeatures *features = nullptr;
        int result = iso_image_import(image_, src.get(), raw, &features);
        std::unique_ptr<IsoReadImageFeatures, decltype(&iso_read_image_features_destroy)> f(features, iso_read_image_features_destroy);
        isoCheck(result, "import last session");
        joliet = iso_read_image_features_has_joliet(features);
        rockridge = iso_read_image_features_has_rockridge(features);
        require(joliet, "Last session has no Joliet tree; refusing an ambiguous import");
        iso_tree_set_replace_mode(image_, ISO_REPLACE_NEVER);
        iso_tree_set_follow_symlinks(image_, 0);
    } catch (...) { iso_image_unref(image_); image_ = nullptr; throw; }
}
IsoImporter::~IsoImporter() { if (image_) iso_image_unref(image_); }
static void walk(IsoDir *dir, const std::string &prefix, Tree &tree, int depth = 0) {
    require(depth < 128, "Directory depth limit exceeded");
    IsoDirIter *raw = nullptr; isoCheck(iso_dir_get_children(dir, &raw), "directory iterator");
    std::unique_ptr<IsoDirIter, decltype(&iso_dir_iter_free)> iter(raw, iso_dir_iter_free);
    IsoNode *node = nullptr;
    int result;
    std::map<std::string, std::string> names;
    while ((result = iso_dir_iter_next(raw, &node)) == 1) {
        std::string name = iso_node_get_name(node); validateName(name);
        require(names.emplace(windowsKey(name), name).second, "Case/Unicode collision in imported directory: " + prefix + name);
        std::string path = prefix + name;
        Entry e; e.old = true;
        if (ISO_NODE_IS_DIR(node)) {
            e.directory = true; tree.emplace(path, e); walk(ISO_DIR(node), path + "/", tree, depth + 1);
        } else {
            require(ISO_NODE_IS_FILE(node), "Unsupported imported node: " + path);
            auto file = ISO_FILE(node); e.size = uint64_t(iso_file_get_size(file));
            int count = 0; struct iso_file_section *sections = nullptr;
            isoCheck(iso_file_get_old_image_sections(file, &count, &sections, 0), "old extents " + path);
            for (int i = 0; i < count; ++i) e.sections.push_back({sections[i].block, sections[i].size});
            free(sections); tree.emplace(path, e);
        }
    }
    require(result == 0, "Directory iteration failed");
}
Tree IsoImporter::tree() const { Tree tree; walk(iso_image_get_root(image_), "", tree); return tree; }
static void mergeDir(IsoImage *image, IsoDir *dir, const fs::path &source, const std::string &prefix, Tree &expected, ConflictPolicy policy, int depth = 0) {
    require(depth < 128, "Directory depth limit exceeded");
    std::map<std::string, std::string> names;
    IsoDirIter *iter = nullptr; isoCheck(iso_dir_get_children(dir, &iter), "merge iterator");
    IsoNode *node = nullptr;
    while (iso_dir_iter_next(iter, &node) == 1) names.emplace(windowsKey(iso_node_get_name(node)), iso_node_get_name(node));
    iso_dir_iter_free(iter);
    std::vector<fs::path> children;
    for (const auto &child : fs::directory_iterator(source)) children.push_back(child.path());
    std::sort(children.begin(), children.end());
    for (const auto &p : children) {
        checkInterrupted(); std::string name = p.filename().string();
        auto status = fs::symlink_status(p);
        // Finder 缓存不属于新增文件；光盘导入树中的旧文件仍保持原 extent。
        if (name == ".DS_Store" && fs::is_regular_file(status)) continue;
        validateName(name);
        require(fs::is_directory(status) || fs::is_regular_file(status), "Only regular files/directories allowed (no symlinks): " + p.string());
        auto key = windowsKey(name);
        auto collision = names.find(key);
        require(collision == names.end() || collision->second == name, "Windows case/Unicode name collision: " + prefix + name);
        IsoNode *existing = nullptr; int found = iso_dir_get_node(dir, name.c_str(), &existing);
        require(found >= 0, "Cannot look up imported node");
        std::string path = prefix + name;
        if (fs::is_directory(status)) {
            IsoDir *sub = nullptr;
            if (found) { require(ISO_NODE_IS_DIR(existing), "Directory/file collision: " + path); sub = ISO_DIR(existing); }
            else {
                isoCheck(iso_tree_add_new_dir(dir, name.c_str(), &sub), "new directory " + path);
                Entry e; e.directory = true; expected.emplace(path, e);
            }
            mergeDir(image, sub, p, path + "/", expected, policy, depth + 1);
        } else {
            if (found) {
                require(ISO_NODE_IS_FILE(existing), "Directory/file collision: " + path);
                require(policy != ConflictPolicy::Reject, "Existing file conflict (no replacement): " + path);
                if (policy == ConflictPolicy::Skip) continue;
                // 只改最新 Session 的目录引用，旧 Session 数据不会被擦除。
                isoCheck(iso_node_remove(existing), "replace file " + path);
            }
            Entry e; e.size = fs::file_size(p); e.sha = shaFile(p);
            isoCheck(iso_tree_add_node(image, dir, p.c_str(), &node), "add file " + path);
            expected.insert_or_assign(path, e);
        }
        names[key] = name;
    }
}
void IsoImporter::merge(const fs::path &files, Tree &expected, ConflictPolicy policy) {
    mergeIsoFiles(image_, files, expected, policy);
}
void mergeIsoFiles(IsoImage *image, const fs::path &files, Tree &expected, ConflictPolicy policy) {
    require(fs::is_directory(fs::symlink_status(files)), "Input must be a directory, not a symlink");
    mergeDir(image, iso_image_get_root(image), fs::canonical(files), "", expected, policy);
}
std::string hashEntry(Blocks &source, const Entry &e) {
    require(!e.directory, "Cannot hash directory");
    return hashBytes([&](auto sink) {
        uint64_t total = 0;
        for (auto section : e.sections) {
            require(section.offset<BlockSize,"Invalid data offset");
            source.prefetch(section.block, uint32_t((uint64_t(section.offset)+section.size + BlockSize - 1) / BlockSize));
            uint64_t offset = 0;
            while (offset < section.size) {
                auto position=offset+section.offset;
                auto b = source.read(section.block + uint32_t(position / BlockSize));
                auto n = std::min<uint64_t>(BlockSize-position%BlockSize, section.size - offset);
                sink(b.data()+position%BlockSize, size_t(n)); offset += n;
            }
            total += section.size;
        }
        require(total == e.size, "Extent sizes differ from file size");
    });
}
void Manifest::save(const fs::path &path) const {
    std::ostringstream o;
    o << "APPENDCD_MANIFEST " << (filesystem=="udf2.01+vat"?3:2) << "\n" << std::quoted(filesystem) << '\n' << importedStart << ' ' << start << ' ' << sourceBase << ' ' << std::quoted(sourcePath) << ' ' << std::quoted(imageSha) << '\n';
    for (const auto &[path, e] : entries) {
        o << (e.directory ? 'D' : 'F') << ' ' << (e.old ? 'O' : 'N') << ' ' << std::quoted(path) << ' ' << e.size << ' ' << std::quoted(e.sha) << ' ' << e.sections.size();
        for (auto s : e.sections) { o << ' ' << s.block << ' ' << s.size; if(filesystem=="udf2.01+vat")o<<' '<<s.offset; }
        o << '\n';
    }
    writeText(path, o.str());
}
void Manifest::saveWindowsExpected(const fs::path &path,const std::string &media) const {
    std::ostringstream o;
    o << "{\"media\":" << std::quoted(media) << ",\"filesystem\":" << std::quoted(filesystem) << ",\"sessionStartLba\":" << start << ",\"imageSha256\":" << std::quoted(imageSha) << ",\"entries\":[";
    bool first = true;
    for (const auto &[name, e] : entries) {
        if (!first) o << ','; first = false;
        o << "{\"relativePath\":" << std::quoted(name) << ",\"directory\":" << (e.directory ? "true" : "false")
          << ",\"origin\":\"" << (e.old ? "old" : "new") << "\",\"size\":" << e.size << ",\"sha256\":" << std::quoted(e.sha) << '}';
    }
    o << "]}\n"; writeText(path, o.str());
}
Manifest Manifest::load(const fs::path &path) {
    std::istringstream in(readText(path)); std::string magic; int version;
    in >> magic >> version; require(magic == "APPENDCD_MANIFEST" && version>=1 && version<=3, "Unsupported manifest");
    Manifest m;
    if (version >= 2) in >> std::quoted(m.filesystem);
    require(m.filesystem == "iso9660+joliet" || m.filesystem == "udf1.02" || (version==3 && m.filesystem=="udf2.01+vat"), "Unknown manifest filesystem");
    in >> m.importedStart >> m.start >> m.sourceBase >> std::quoted(m.sourcePath) >> std::quoted(m.imageSha);
    bool initial = m.start == 0 && m.importedStart == 0 && m.sourceBase == 0 && m.sourcePath.empty() && m.filesystem == "iso9660+joliet";
    require(bool(in) && (m.start > m.importedStart || initial), "Invalid manifest addresses");
    char kind, origin;
    while (in >> kind >> origin) {
        std::string p; Entry e; size_t count;
        in >> std::quoted(p) >> e.size >> std::quoted(e.sha) >> count;
        require(bool(in) && count < 100000 && (kind == 'D' || kind == 'F') && (origin == 'O' || origin == 'N'), "Invalid manifest entry");
        e.directory = kind == 'D'; e.old = origin == 'O';
        require(!initial || !e.old, "Initial-session manifest cannot contain old references");
        for (size_t i = 0; i < count; ++i) { Section s; in >> s.block >> s.size; if(version==3)in>>s.offset; require(s.offset<BlockSize,"Invalid manifest offset"); e.sections.push_back(s); }
        require(bool(in) && m.entries.emplace(p, e).second, "Invalid/duplicate manifest entry");
    }
    require(in.eof(), "Malformed manifest"); return m;
}
Manifest buildSession(std::shared_ptr<Blocks> source, uint32_t importedStart, uint32_t start,
                      const fs::path &files, const fs::path &output, const std::string &sourcePath, uint32_t sourceBase, ConflictPolicy policy) {
    require(start > importedStart + 32 && start < UINT32_MAX - 1000000, "Invalid next writable LBA");
    require(!fs::exists(output) && !fs::exists(output.string() + ".manifest"), "Output/manifest already exists; refusing overwrite");
    IsoImporter imported(source, importedStart);
    Manifest m; m.importedStart = importedStart; m.start = start; m.sourceBase = sourceBase; m.sourcePath = sourcePath;
    m.entries = imported.tree(); imported.merge(files, m.entries, policy);
    bool added = false;
    for (const auto &[p, e] : m.entries) {
        if (!e.old) added = true;
        if (e.old && !e.directory) for (auto s : e.sections)
            if (s.size) require(uint64_t(s.block) + (uint64_t(s.size) + BlockSize - 1) / BlockSize <= start, "Old extent overlaps new session: " + p);
    }
    require(added, "Nothing new to append");
    IsoWriteOpts *raw = nullptr; isoCheck(iso_write_opts_new(&raw, 0), "write options");
    std::unique_ptr<IsoWriteOpts, decltype(&iso_write_opts_free)> opts(raw, iso_write_opts_free);
    isoCheck(iso_write_opts_set_iso_level(raw, 3), "ISO level 3");
    isoCheck(iso_write_opts_set_joliet(raw, 1), "Joliet");
    isoCheck(iso_write_opts_set_rockridge(raw, 0), "no RR dependency");
    isoCheck(iso_write_opts_set_joliet_long_names(raw, 1), "Joliet long names");
    isoCheck(iso_write_opts_set_joliet_longer_paths(raw, 1), "Joliet long paths");
    isoCheck(iso_write_opts_set_joliet_utf16(raw, 1), "Joliet UTF-16");
    isoCheck(iso_write_opts_set_allow_deep_paths(raw, 1), "deep paths");
    isoCheck(iso_write_opts_set_allow_longer_paths(raw, 1), "long ISO paths");
    isoCheck(iso_write_opts_set_appendable(raw, 1), "appendable image");
    isoCheck(iso_write_opts_set_ms_block(raw, start), "ms block");
    isoCheck(iso_write_opts_set_tail_blocks(raw, 32), "read-ahead tail blocks");
    int fd = ::open(output.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    require(fd >= 0, "Cannot create continuation ISO");
    struct burn_source *burn = nullptr;
    uint64_t readsBefore = source->reads;
    try {
        isoCheck(iso_image_create_burn_source(imported.image(), raw, &burn), "create continuation generator");
        uint64_t predicted = uint64_t(burn->get_size(burn)), written = 0;
        std::array<uint8_t, BlockSize> buffer{};
        for (;;) {
            checkInterrupted();
            int n = burn->read ? burn->read(burn, buffer.data(), buffer.size()) : burn->read_xt(burn, buffer.data(), buffer.size());
            require(n >= 0, "Continuation generator failed"); if (!n) break;
            size_t offset = 0;
            while (offset < size_t(n)) {
                ssize_t w = ::write(fd, buffer.data() + offset, size_t(n) - offset);
                if (w < 0 && errno == EINTR) { checkInterrupted(); continue; }
                require(w > 0, "ISO write failed"); offset += size_t(w);
            }
            written += uint64_t(n);
        }
        require(written == predicted && written % BlockSize == 0, "Generator size mismatch: written=" + std::to_string(written) + " predicted=" + std::to_string(predicted));
        require(source->reads == readsBefore, "Generator reread old image; aborting PoC");
        require(fsync(fd) == 0, "ISO fsync failed"); close(fd); fd = -1;
        // libburn 不参与链接：此结构由 libisofs 分配，free_data 后释放唯一引用。
        burn->free_data(burn); free(burn); burn = nullptr;
        repairGeneratedMetadata(output, start);
        m.imageSha = shaFile(output); m.save(output.string() + ".manifest");
        std::cout << "Continuation LBA: " << start << "\nImage blocks: " << written / BlockSize
                  << " (before metadata normalization)\nFinal image blocks: " << fs::file_size(output) / BlockSize
                  << "\nOld-source reads during generation: 0\n";
        return m;
    } catch (...) {
        if (burn) { if (burn->cancel) burn->cancel(burn); burn->free_data(burn); free(burn); }
        if (fd >= 0) close(fd);
        std::error_code ec; fs::remove(output, ec); fs::remove(output.string() + ".manifest", ec); throw;
    }
}
}
