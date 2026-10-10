#pragma once
#include "IsoCore.hpp"
namespace ac {
struct UdfTree {
    std::string volumeName;
    Tree entries;
    std::vector<std::string> inlineFiles;
};
UdfTree readUdfTree(Blocks &source, uint32_t last, uint32_t readableEnd);
bool isUdfSession(Blocks &source, uint32_t last, uint32_t readableEnd = UINT32_MAX);
}
