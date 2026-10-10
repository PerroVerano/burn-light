#pragma once
#include "UdfReader.hpp"
namespace ac {
Manifest buildUdfSession(std::shared_ptr<Blocks> source, uint32_t last, uint32_t next,
                         uint32_t readableEnd, const fs::path &files, const fs::path &image,
                         const std::string &sourcePath, uint32_t sourceBase = 0,
                         uint32_t fixedBlocks = 0, ConflictPolicy policy = ConflictPolicy::Reject);
void verifyUdfSession(Blocks &source, const Manifest &manifest, uint32_t blocks, bool hashOld);
}
