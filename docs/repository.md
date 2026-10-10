# Git 仓库范围与重新构建

仓库保存 C++ CLI/core、现有后端 patch/header、原六组回归、SwiftUI/AppKit GUI、独立导出适配器、构建/打包/验证脚本、合成小文件、说明与少量验收摘要。

本地 build、GUI output/编译缓存、光盘 ISO/cache/manifest、完整物理任务归档、本次用户输入 DOCX/Java、导出的用户文件、toc.dat、.DS_Store 均不提交。`gui/evidence/` 只按 .gitignore 中的明确名单提交说明和小型摘要；摘要中的绝对路径和任务 ID 是历史诊断位置，未附对应用户数据。历史 hash 和构建绑定保留当时结果，不当作 Git 当前源码的完整 hash。

唯一保留的二进制归档是 GUI 前的 `artifacts/baseline-before-gui/20261010-101609/baseline.tar.gz`（约 3.4 MB）及其两个 JSON 索引。GUI 打包要核对其中的原 CLI 和 media6/initial7 hash；该基准包包含源码、小型测试材料、原 CLI/core 和两个基准后端，不含 GUI 用户输入或光盘大缓存。源 hash 和上游 CDDL notice 随项目保存。

## 新 checkout 的本机 GUI 构建

目标仍是 Apple Silicon / macOS 26；明确 SDK 为 `/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk`。构建依赖 CMake/pkg-config 与 Homebrew 的 cdrtools、libisofs、libudfread；后端从源码重建还需要 smake。当前已验收本机的版本见 README 与验收报告。

```sh
git clone https://git.luzove.top/luzove/burn-light.git
cd burn-light
brew install cdrtools libisofs libudfread cmake pkgconf smake
python3 gui/scripts/restore-reference.py
# 默认只读包；从源码实际编译 Swift 与共享 C++
./gui/scripts/test.sh
python3 gui/scripts/cli-regression.py
python3 gui/Validation/conflict_offline.py
python3 gui/Validation/compact_session.py
# 本机刻录包必须显式启用；不会在此步骤执行物理刻录
BURNLIGHT_ENABLE_WRITES=1 ./gui/scripts/build.sh
./gui/scripts/release.sh --no-build
# 空闲时正常退出应用再安装；旧安装留作备份
python3 gui/scripts/install-local.py
```

`restore-reference.py` 先核对整个归档及成员 hash，仅恢复缺失的 `build/appendcd`、两个 `~/.local/libexec/appendcd/` 后端及其 provenance。已有内容不同、符号链接或物理任务活跃/未知时拒绝，不覆盖现有二进制。`--check` 只检查不写文件。这些是本机历史基准输入；GUI 自身仍重新编译当前源码，默认行为不会变成自动刻录。

SwiftPM 工程保留，本机 SwiftPM 自身缺符号，默认用明确 SDK 的 xcrun swiftc。打包和运行的原权限、信任 pin、串行准入与失败锁定不变。

## 从当前源码构建 CLI

保留上述 `build/appendcd` 供 GUI 基准核对；当前源码 CLI 生成到独立目录：

```sh
cmake -S . -B build-cli -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk \
  -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build-cli -j 4
ctest --test-dir build-cli --output-on-failure
./build-cli/appendcd help
```

此 CTest 为合成/离线回归，不做物理写入。新 checkout 中会生成 artifacts/offline 和 artifacts/udf-tests 合成材料；`gui/Validation/export_offline.py` 还含原本机实际光盘 cache 场景，完整的 10 场景须有原本机材料，不能仅从 Git 摘要重现该物理证据。`physical_check.py` 与 `windows_return_check.py` 同样是对原本机收据/cache 的审计，不是可在空仓库制造 PASS 的用例。

若需要从上游重建后端，使用 `scripts/build-cdrecord.py` 的固定源码 SHA256 与现有 patch，并在新的目录生成，不覆盖已验收 runtime。新二进制会有自己的 hash；不能自行把它当成 GUI 历史基准或偷偷修改既有 pin。

GUI 的 CD-R 两次写入、Windows 人工验收与八文件返回盘读取详见 [验收报告](../gui/evidence/write-acceptance-20261010/acceptance.md)。DVD-R GUI、空白首刻、skip 实盘等未完成项保持原报告边界。仓库不包含凭据，也不自动上传运行时诊断或用户文件。
