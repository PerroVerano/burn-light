#pragma once
#include "UdfReader.hpp"

namespace ac {
struct UdfAddress {
    uint32_t block = 0;
    uint16_t reference = 0;
    bool operator==(const UdfAddress &) const = default;
};
struct VatVolume {
    uint16_t revision = 0, physicalReference = 0, virtualReference = 0, partitionNumber = 0;
    uint32_t session = 0, anchor = 0, mainVds = 0, reserveVds = 0, partition = 0, capacity = 0;
    uint32_t vatIcb = 0, previousVat = UINT32_MAX, files = 0, directories = 0;
    uint64_t nextUniqueId = 16;
    bool reserveUsed = false;
    Block pvd{}, iuvd{}, pd{}, lvd{}, fsd{}, integrity{};
    std::vector<uint8_t> vatHeader;
    std::vector<uint32_t> vat;
    std::map<std::string,UdfAddress> icbs;
    UdfAddress root;
    UdfTree tree;
    std::string diagnostics;
};
// 只解析卷入口与分区映射；非 2.01/VAT 返回版本信息供现有 1.02 分支判断。
VatVolume readUdfVolume(Blocks &source, uint32_t session, uint32_t end);
VatVolume readVatVolume(Blocks &source, uint32_t session, uint32_t end);
void validateUdfTag(const uint8_t *bytes, size_t available, uint16_t id, uint16_t version, uint32_t location);
}
