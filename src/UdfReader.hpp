#pragma once
#include "IsoCore.hpp"
namespace ac {
struct UdfTree {
    std::string volumeName;
    std::string filesystem = "udf1.02", diagnostics, viewIdentity, compatibility;
    uint16_t revision = 0x0102;
    Tree entries;
    std::vector<std::string> inlineFiles;
};
UdfTree readUdfTree(Blocks &source, uint32_t last, uint32_t readableEnd);
bool isUdfSession(Blocks &source, uint32_t last, uint32_t readableEnd = UINT32_MAX);
}
