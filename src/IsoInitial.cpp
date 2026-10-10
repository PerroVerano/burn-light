#include "IsoCore.hpp"
#include "GeneratedMetadata.hpp"
#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>

namespace ac {
Manifest buildInitialSession(const fs::path &files, const fs::path &output) {
    // 独立的首刻生成器：不导入旧盘，不改变已验证的 continuation 写入参数。
    require(!fs::exists(output) && !fs::exists(output.string() + ".manifest"), "Output/manifest already exists; refusing overwrite");
    auto check = [](int result, const char *operation) { require(result > 0, std::string(operation) + ": " + iso_error_to_msg(result)); };
    IsoImage *rawImage = nullptr;
    check(iso_image_new("APPENDCD", &rawImage), "initial image");
    std::unique_ptr<IsoImage, decltype(&iso_image_unref)> image(rawImage, iso_image_unref);
    iso_tree_set_replace_mode(rawImage, ISO_REPLACE_NEVER);
    iso_tree_set_follow_symlinks(rawImage, 0);
    Manifest m;
    mergeIsoFiles(rawImage, files, m.entries);
    require(!m.entries.empty(), "Nothing to record in initial session");
    IsoWriteOpts *rawOpts = nullptr;
    check(iso_write_opts_new(&rawOpts, 0), "initial write options");
    std::unique_ptr<IsoWriteOpts, decltype(&iso_write_opts_free)> opts(rawOpts, iso_write_opts_free);
    // 首个 Session 从 LBA 0 开始。库的 appendable 选项控制旧 extent 重用，
    // 光盘是否保持可追加由 cdrecord -multi 控制，两者不能混淆。
    check(iso_write_opts_set_iso_level(rawOpts, 3), "ISO level 3");
    check(iso_write_opts_set_joliet(rawOpts, 1), "Joliet");
    check(iso_write_opts_set_rockridge(rawOpts, 0), "no RR dependency");
    check(iso_write_opts_set_joliet_long_names(rawOpts, 1), "Joliet long names");
    check(iso_write_opts_set_joliet_longer_paths(rawOpts, 1), "Joliet long paths");
    check(iso_write_opts_set_joliet_utf16(rawOpts, 1), "Joliet UTF-16");
    check(iso_write_opts_set_allow_deep_paths(rawOpts, 1), "deep paths");
    check(iso_write_opts_set_allow_longer_paths(rawOpts, 1), "long ISO paths");
    check(iso_write_opts_set_appendable(rawOpts, 0), "standalone image");
    check(iso_write_opts_set_ms_block(rawOpts, 0), "initial LBA");
    check(iso_write_opts_set_tail_blocks(rawOpts, 32), "read-ahead tail blocks");
    int fd = ::open(output.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    require(fd >= 0, "Cannot create initial ISO");
    burn_source *burn = nullptr;
    try {
        check(iso_image_create_burn_source(rawImage, rawOpts, &burn), "initial generator");
        uint64_t predicted = uint64_t(burn->get_size(burn)), written = 0;
        Block buffer{};
        for (;;) {
            checkInterrupted();
            int n = burn->read ? burn->read(burn, buffer.data(), buffer.size()) : burn->read_xt(burn, buffer.data(), buffer.size());
            require(n >= 0, "Initial generator failed");
            if (!n) break;
            for (size_t offset = 0; offset < size_t(n);) {
                auto w = ::write(fd, buffer.data() + offset, size_t(n) - offset);
                if (w < 0 && errno == EINTR) { checkInterrupted(); continue; }
                require(w > 0, "Initial ISO write failed");
                offset += size_t(w);
            }
            written += uint64_t(n);
        }
        require(written == predicted && written % BlockSize == 0 && written / BlockSize < UINT32_MAX, "Initial generator size mismatch");
        require(fsync(fd) == 0, "Initial ISO fsync failed"); close(fd); fd = -1;
        burn->free_data(burn); free(burn); burn = nullptr;
        repairGeneratedMetadata(output, 0);
        m.imageSha = shaFile(output);
        m.save(output.string() + ".manifest");
        std::cout << "Initial Session LBA: 0\nImage blocks: " << fs::file_size(output) / BlockSize << '\n';
        return m;
    } catch (...) {
        if (burn) { if (burn->cancel) burn->cancel(burn); burn->free_data(burn); free(burn); }
        if (fd >= 0) close(fd);
        std::error_code ec; fs::remove(output, ec); fs::remove(output.string() + ".manifest", ec);
        throw;
    }
}
}
