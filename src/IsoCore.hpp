#pragma once
#include "Util.hpp"
#include <libisofs.h>
#include <set>

namespace ac {
struct Section {
    uint32_t block = 0, size = 0;
    bool operator==(const Section &) const = default;
};
struct Entry {
    bool directory = false, old = false;
    uint64_t size = 0;
    std::vector<Section> sections;
    std::string sha;
};
using Tree = std::map<std::string, Entry>;
// 仅显式选择时处理同路径普通文件；目录/文件和 Windows 名称碰撞仍拒绝。
enum class ConflictPolicy { Reject, Skip, Replace };
class Blocks {
public:
    uint64_t reads = 0;
    virtual ~Blocks() = default;
    virtual Block read(uint32_t lba) = 0;
    virtual void prefetch(uint32_t, uint32_t) {}
};
class FileBlocks : public Blocks {
    int fd_;
    uint32_t base_;
    bool sparse_ = false;
    std::set<uint32_t> valid_;
public:
    uint64_t count;
    FileBlocks(const fs::path &path, uint32_t base = 0);
    ~FileBlocks() override;
    Block read(uint32_t lba) override;
};
class OverlayBlocks : public Blocks {
    std::shared_ptr<Blocks> old_, next_;
    uint32_t start_;
public:
    OverlayBlocks(std::shared_ptr<Blocks> oldSource, std::shared_ptr<Blocks> nextSource, uint32_t start);
    Block read(uint32_t lba) override;
};
struct Manifest {
    std::string filesystem = "iso9660+joliet";
    uint32_t importedStart = 0, start = 0, sourceBase = 0;
    std::string sourcePath, imageSha;
    Tree entries;
    void save(const fs::path &path) const;
    void saveWindowsExpected(const fs::path &path, const std::string &media = "DVD-R") const;
    static Manifest load(const fs::path &path);
};
class IsoRuntime {
public:
    IsoRuntime();
    ~IsoRuntime();
};
class IsoImporter {
    IsoImage *image_ = nullptr;
public:
    bool joliet = false, rockridge = false;
    explicit IsoImporter(std::shared_ptr<Blocks> source, uint32_t start);
    ~IsoImporter();
    IsoImporter(const IsoImporter &) = delete;
    IsoImage *image() { return image_; }
    Tree tree() const;
    void merge(const fs::path &files, Tree &expected, ConflictPolicy policy = ConflictPolicy::Reject);
};
std::string hashEntry(Blocks &source, const Entry &entry);
void mergeIsoFiles(IsoImage *image, const fs::path &files, Tree &expected, ConflictPolicy policy = ConflictPolicy::Reject);
Manifest buildInitialSession(const fs::path &files, const fs::path &output);
Manifest buildSession(std::shared_ptr<Blocks> source, uint32_t importedStart, uint32_t start,
                      const fs::path &files, const fs::path &output,
                      const std::string &sourcePath = "", uint32_t sourceBase = 0,
                      ConflictPolicy policy = ConflictPolicy::Reject);
}
