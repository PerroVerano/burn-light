# Git 仓库范围与重新构建

仓库保存 C++ CLI/core、现有后端 patch/header、原六组回归、SwiftUI/AppKit GUI、独立导出适配器、构建/打包/验证脚本、合成小文件、说明与少量验收摘要。

当前本机版本为 1.2.2 / build 6，2026-10-19 的 Windows 连续更新兼容修复与当前 DVD-R 只读验收见 [本次记录](udf201-windows-updates-20261019.md)。1.2.1 / build 5 的历史结档见 [结档记录](release-1.2.1-closeout-20261013.md)。`docs/evidence/` 保存 UDF/VAT、GUI 修复及清理的小型机器摘要；这些摘要中的路径/hash 是验收时的绑定，真实用户数据和大缓存仍保留本地。

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

此 CTest 为合成/离线回归，不做物理写入。七组测试包含原六组与新增 VAT 专项，生成材料默认放在当前构建目录的 `test-artifacts/`，不清理历史验收目录；可用 CMake 的 `APPENDCD_TEST_ARTIFACT_ROOT` 指定专用目录。`gui/Validation/export_offline.py` 还含原本机实际光盘 cache 场景，完整的 10 场景须有原本机材料，不能仅从 Git 摘要重现该物理证据。`physical_check.py` 与 `windows_return_check.py` 同样是对原本机收据/cache 的审计，不是可在空仓库制造 PASS 的用例。

若需要从上游重建后端，使用 `scripts/build-cdrecord.py` 的固定源码 SHA256 与现有 patch，并在新的目录生成，不覆盖已验收 runtime。新二进制会有自己的 hash；不能自行把它当成 GUI 历史基准或偷偷修改既有 pin。

GUI 的 CD-R 两次写入、Windows 人工验收与八文件返回盘读取详见 [验收报告](../gui/evidence/write-acceptance-20261010/acceptance.md)。DVD-R GUI、空白首刻、skip 实盘等未完成项保持原报告边界。仓库不包含凭据，也不自动上传运行时诊断或用户文件。

## UDF 2.01/VAT 独立构建

```sh
python3 scripts/build-cdrecord.py --vat --output-dir build-vat-backend-v9
cmake -S . -B build-cli -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk \
  -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build-cli -j 4
ctest --test-dir build-cli --output-on-failure
BURNLIGHT_BUILD_ROOT="$PWD/build-udf201-gui-v9" ./gui/scripts/test.sh
```

VAT 后端为独立 `appendcd-vat-cdrecord`（vat9），仅接受 CD-R、单个可变包 Track、明确 NWA、multi、无 padding；不覆盖 media6/initial7。vat9 修复 packet lead-in 未标记导致不结束 Session 的缺陷，CLI/CMake/GUI 打包拒绝 vat8。构建到 `build-vat-backend-v9`，固定上游源码 hash、CDDL notice 和生成 provenance 均保留。CLI 没有 VAT 后端时仍可读取和生成离线镜像，物理写入明确失败。GUI 将 vat9 纳入相对路径信任清单，离线测试仍为只读包；不要改变历史基准 pin。

本机实际只读比较发现，同一后端 hash 在原 Documents 构建路径中无法创建 IOKit 插件，临时目录和 GUI 包副本能工作。独立 CLI 可用 `-DVAT_CDRECORD_PATH=/private/tmp/<本次隔离runtime>/appendcd-vat-cdrecord` 绑定已复制并核对 provenance/hash 的运行副本；此目录为临时产物，删除后必须重新配置。不得通过改 SIP/TCC、覆盖已安装 runtime 或猜设备地址规避。相关实盘失败和修复保留在 [原记录](udf201-mac-attempt-20261012.md)；单独授权完成已有会话后又经 Windows 再追加，Mac 的实际三文件读取/导出见 [返回盘记录](udf201-windows-return-20261012.md)。vat9 正常 burn 完整物理复验仍待验收。

当前独立包位于 `build-udf201-gui-compatible/output/BurnLight.app`（只读）与 `build-udf201-gui-compatible-write/output/BurnLight.app`（启用确认写入）。代码共用受限 Windows FID 兼容分支；默认严格独立 oracle 与显式兼容 oracle 分别记录结果。新的匿名 fixture 不依赖 writer 生成，71 项 VAT 测试可离线重现。实盘缓存、用户文件、root helper、完整日志在忽略的 `artifacts/udf201-acceptance/`；机器摘要只保存 hash、地址、检查状态与必要文件名，历史阶段报告保持原内容。

新增同设备 IOMedia 发布等待后，最终包更新为 `build-udf201-gui-publication/output/BurnLight.app`（只读）与 `build-udf201-gui-publication-write/output/BurnLight.app`（可写本地验收包，同目录 zip）；之前 compatible 包保留。使用 `BURNLIGHT_BUILD_ROOT="$PWD/build-udf201-gui-publication" BURNLIGHT_EVIDENCE_DIR="$PWD/build-udf201-gui-publication/evidence" ./gui/scripts/test.sh` 独立重建/验证，避免改写历史材料。修复后的正常 vat9 burn 已实测完成 Session 4，最新四文件实际严格读回/导出通过；用户随后确认四文件在 Windows 均可见且能打开，见 [最新验收](udf201-vat9-normal-burn-20261012.md)。Windows 自动 hash 报告未采集，原损伤八 Session 全文件读取仍未通过。

`tests/vat_fixture.py` 为独立匿名编码器，`tests/vat_oracle.py` 是独立 Python 读回 oracle；均不调用 writer。真实盘只读缓存、用户内容和生成镜像位于忽略的 `artifacts/udf201-acceptance/20261012/`，Git 只保存代码与 [验收摘要](udf201-acceptance-20261012.md)。
