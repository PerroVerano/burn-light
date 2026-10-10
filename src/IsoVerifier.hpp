#pragma once
#include "IsoCore.hpp"

namespace ac {
struct Volume {
    bool joliet;
    uint32_t descriptor, spaceSize, root, rootSize, pathTableSize, littleTable, bigTable;
    Tree tree;
};
std::vector<Volume> readVolumes(Blocks &source, uint32_t start, uint32_t end = UINT32_MAX, bool validateTables = true);
void verifySession(const fs::path &image, const Manifest &manifest, std::shared_ptr<Blocks> oldSource, bool hashOld = true);
void verifyReadback(Blocks &disc, const Manifest &manifest, uint32_t imageBlocks);
void printVolumes(const std::vector<Volume> &volumes);
void probeFilesystem(Blocks &source, uint32_t start, const fs::path &report);
}
