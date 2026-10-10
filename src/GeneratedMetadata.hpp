#pragma once
#include "Util.hpp"
namespace ac {
// 修复实测且由官方源码证实的 Joliet '..' 字段顺序错误及无数据空文件占位地址。
// 只作用于本程序刚生成的 continuation；不修改旧 Session 或库的安装文件。
void repairGeneratedMetadata(const fs::path &image, uint32_t start);
}
