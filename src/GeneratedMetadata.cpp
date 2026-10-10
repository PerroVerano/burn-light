#include "GeneratedMetadata.hpp"
#include "IsoCore.hpp"
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <set>
#include <unistd.h>

namespace ac {
static uint32_t both(const uint8_t *p) {
    uint32_t little = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    uint32_t big = uint32_t(p[7]) | uint32_t(p[6]) << 8 | uint32_t(p[5]) << 16 | uint32_t(p[4]) << 24;
    require(little == big, "Normalizer: inconsistent both-endian field"); return little;
}
static void setBoth(uint8_t *p, uint32_t n) {
    for (int i = 0; i < 4; ++i) { p[i] = uint8_t(n >> (i * 8)); p[7 - i] = p[i]; }
}
void repairGeneratedMetadata(const fs::path &image, uint32_t start) {
    FileBlocks blocks(image, start); uint32_t originalEnd = start + uint32_t(blocks.count);
    int fd = ::open(image.c_str(), O_RDWR | O_CLOEXEC | O_NOFOLLOW); require(fd >= 0, "Normalizer cannot open generated ISO");
    size_t parentRepairs = 0, emptyRepairs = 0;
    std::vector<uint32_t> descriptors;
    try {
        auto save = [&](uint32_t lba, const Block &b) {
            require(pwrite(fd, b.data(), b.size(), off_t(lba - start) * BlockSize) == ssize_t(BlockSize), "Normalizer write failed");
        };
        for (uint32_t vd = start + 16; vd < start + 80; ++vd) {
            auto b = blocks.read(vd); require(memcmp(b.data() + 1, "CD001", 5) == 0, "Normalizer invalid descriptor");
            if (b[0] == 255) break;
            bool joliet = b[0] == 2 && b[88] == '%' && b[89] == '/';
            if (b[0] != 1 && !joliet) continue;
            require(both(b.data() + 80) == blocks.count, "Normalizer unexpected generated volume size");
            descriptors.push_back(vd);
            std::set<uint32_t> visited;
            std::function<void(uint32_t, uint32_t, uint32_t, uint32_t, int)> walk;
            walk = [&](uint32_t extent, uint32_t bytes, uint32_t parent, uint32_t parentBytes, int depth) {
                require(depth < 128 && visited.size() < 100000 && visited.insert(extent).second, "Normalizer directory cycle/resource limit");
                require(extent >= start + 16 && uint64_t(extent) + (uint64_t(bytes) + BlockSize - 1) / BlockSize <= originalEnd, "Normalizer metadata out of range");
                for (uint64_t offset = 0; offset < bytes; offset += BlockSize) {
                    uint32_t lba = extent + uint32_t(offset / BlockSize); auto directory = blocks.read(lba); bool changed = false;
                    std::vector<std::pair<uint32_t, uint32_t>> subdirs;
                    for (size_t pos = 0; pos < BlockSize && directory[pos];) {
                        uint8_t *r = directory.data() + pos; size_t length = r[0];
                        require(length >= 34 && pos + length <= BlockSize && r[32] && 33 + size_t(r[32]) <= length, "Normalizer bad directory record");
                        uint32_t block = both(r + 2), size = both(r + 10);
                        if (r[25] & 2) {
                            bool special = r[32] == 1 && r[33] < 2;
                            if (special && r[33] == 1 && joliet) {
                                require(block == extent || block == parent, "Normalizer refuses unexpected Joliet parent extent");
                                if (block != parent || size != parentBytes) {
                                    setBoth(r + 2, parent); setBoth(r + 10, parentBytes); changed = true; ++parentRepairs;
                                }
                            } else if (!special) subdirs.push_back({block, size});
                        } else if (!size && (block < start || block >= originalEnd)) {
                            // 空文件没有可保留的数据 extent；引用已验证的尾部零块，消除 0xfffffff0 哨兵地址。
                            auto zero = blocks.read(originalEnd - 1);
                            require(std::all_of(zero.begin(), zero.end(), [](uint8_t v) { return v == 0; }), "Expected zero tail block missing");
                            setBoth(r + 2, originalEnd - 1); changed = true; ++emptyRepairs;
                        }
                        pos += length;
                    }
                    if (changed) save(lba, directory);
                    for (auto [block, size] : subdirs) walk(block, size, extent, bytes, depth + 1);
                }
            };
            uint32_t root = both(b.data() + 158), rootBytes = both(b.data() + 166);
            walk(root, rootBytes, root, rootBytes, 0);
        }
        require(fsync(fd) == 0, "Normalizer fsync failed"); close(fd); fd = -1;
        std::cout << "Generated metadata corrections: Joliet parent records=" << parentRepairs << ", empty-file records=" << emptyRepairs << '\n';
    } catch (...) { if (fd >= 0) close(fd); throw; }
}
}
