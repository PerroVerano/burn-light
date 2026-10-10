#include "UdfReader.hpp"
#include <udfread/udfread.h>
#include <udfread/blockinput.h>
#include <cstring>
#include <iostream>

namespace ac {
bool isUdfSession(Blocks &source, uint32_t last, uint32_t readableEnd) {
    require(uint64_t(last) + 16 < readableEnd, "Filesystem recognition area is outside recorded range");
    // 小 ISO/Joliet 卷可以少于 48 blocks；只在已知输入范围内查找 UDF VRS。
    for (uint32_t i = 16; i < 48 && uint64_t(last) + i < readableEnd; ++i) {
        auto b = source.read(last + i);
        if (!memcmp(b.data() + 1, "NSR02", 5) || !memcmp(b.data() + 1, "NSR03", 5)) return true;
    }
    return false;
}
namespace {
struct Input {
    udfread_block_input api{};
    Blocks *source;
    uint32_t start, end;
    Input(Blocks &blocks, uint32_t last, uint32_t readableEnd) : source(&blocks), start(last), end(readableEnd) {
        api.close = nullptr;
        api.size = [](udfread_block_input *p) { return reinterpret_cast<Input *>(p)->end; };
        api.read = [](udfread_block_input *p, uint32_t lba, void *buffer, uint32_t n, int) {
            auto &input = *reinterpret_cast<Input *>(p);
            try {
                for (uint32_t i = 0; i < n; ++i) {
                    uint32_t block = lba + i;
                    // 1.2.0 固定查找 VRS/首 Anchor。只重定向这些入口；VDS/partition 地址保持绝对 LBA。
                    if (input.start && ((block >= 16 && block < 32) || block == 256)) block += input.start;
                    require(block < input.end, "UDF block exceeds recorded range");
                    auto b = input.source->read(block);
                    memcpy(static_cast<uint8_t *>(buffer) + uint64_t(i) * BlockSize, b.data(), BlockSize);
                }
                return int(n);
            } catch (const std::exception &e) { std::cerr << "UDF source: " << e.what() << '\n'; return 0; }
        };
    }
};
void walk(udfread *volume, UDFDIR *directory, const std::string &prefix, UdfTree &tree, int depth) {
    require(depth < 128 && tree.entries.size() < 100000, "UDF directory resource limit");
    std::map<std::string, std::string> names;
    udfread_dirent raw{};
    while (udfread_readdir(directory, &raw)) {
        checkInterrupted();
        std::string name = raw.d_name; validateName(name);
        require(names.emplace(windowsKey(name), name).second, "UDF Windows name collision: " + prefix + name);
        std::string path = prefix + name;
        Entry entry; entry.old = true;
        if (raw.d_type == UDF_DT_DIR) {
            entry.directory = true; tree.entries.emplace(path, entry);
            std::unique_ptr<UDFDIR, decltype(&udfread_closedir)> child(udfread_opendir_at(directory, name.c_str()), udfread_closedir);
            require(bool(child), "Cannot open UDF directory: " + path);
            walk(volume, child.get(), path + "/", tree, depth + 1);
        } else {
            require(raw.d_type == UDF_DT_REG, "Unsupported UDF node: " + path);
            std::unique_ptr<UDFFILE, decltype(&udfread_file_close)> file(udfread_file_openat(directory, name.c_str()), udfread_file_close);
            require(bool(file), "Cannot open UDF file metadata: " + path);
            auto size = udfread_file_size(file.get()); require(size >= 0, "Invalid UDF file size"); entry.size = uint64_t(size);
            uint64_t blocks = (entry.size + BlockSize - 1) / BlockSize;
            require(blocks <= UINT32_MAX, "UDF file too large for block API");
            bool unsupported = false;
            for (uint32_t i = 0; i < blocks; ++i) {
                if (i % 512 == 0) checkInterrupted();
                auto lba = udfread_file_lba(file.get(), i);
                if (!lba) { unsupported = true; entry.sections.clear(); break; }
                auto bytes = uint32_t(std::min<uint64_t>(BlockSize, entry.size - uint64_t(i) * BlockSize));
                if (!entry.sections.empty() && uint64_t(entry.sections.back().block) + entry.sections.back().size / BlockSize == lba &&
                    entry.sections.back().size % BlockSize == 0 && uint64_t(entry.sections.back().size) + bytes <= UINT32_MAX)
                    entry.sections.back().size += bytes;
                else entry.sections.push_back({lba, bytes});
            }
            if (unsupported) tree.inlineFiles.push_back(path);
            tree.entries.emplace(path, entry);
        }
    }
}
}
UdfTree readUdfTree(Blocks &source, uint32_t last, uint32_t readableEnd) {
    require(readableEnd > uint64_t(last) + 256, "UDF Anchor is outside recorded range");
    Input input(source, last, readableEnd);
    std::unique_ptr<udfread, decltype(&udfread_close)> volume(udfread_init(), udfread_close);
    require(bool(volume), "Cannot initialize libudfread");
    require(udfread_open_input(volume.get(), &input.api) == 0, "libudfread cannot import this UDF session");
    UdfTree tree;
    if (auto id = udfread_get_volume_id(volume.get())) tree.volumeName = id;
    std::unique_ptr<UDFDIR, decltype(&udfread_closedir)> root(udfread_opendir(volume.get(), "/"), udfread_closedir);
    require(bool(root), "Cannot open UDF root directory");
    walk(volume.get(), root.get(), "", tree, 0);
    return tree;
}
}
