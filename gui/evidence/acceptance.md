# 原生 GUI 离线 / 只读交付验收

日期：2026-10-10（Asia/Shanghai）。用户已明确选择“先交付离线/只读版本”。

本记录保留首版交付证据。随后新增的光盘文件导出功能、更新构建与验证结果见 [光盘导出验收](export-20261010/acceptance.md)。

**结论：本机原生 GUI 可编译、链接、启动；离线与当前 CD-R 的 GUI 只读链路 PASS。零光盘写入。GUI 实盘刻录、Windows 与分发打包验收未完成。**

| 检查 | 本轮证据 | 结论 |
| --- | --- | --- |
| 开发前归档 | `baseline-check.json`；归档 SHA256 `7fbc96176a7d118e66d0915f61b7037ac2a8068a5645ea302bad4fc9964d57fe`；归档所有文件 hash 匹配 | PASS；开始时只有 README 已在归档后修改 |
| 核心保护 | `baseline-final.json`；src/backend/scripts/tests/fixtures/CMake/原 CLI/静态库/两 runtime/provenance | 原样；本轮只新增 gui 与更新两处入口文档 |
| 实际编译/链接 | `build.log`；MacOSX26 SDK；arm64/macOS 14 target；ad hoc codesign verify | PASS；SwiftPM 自身崩溃另存 `swiftpm-toolchain-failure.log`，默认 swiftc 构建 |
| 原生 UI | CUA 启动/AX/截图观察；真实自动发现、原生 NSOpenPanel、文件表、日志、历史；模拟空白与 unfinished Session | PASS；最终构建标明“离线 / 只读版”，写入按钮禁用 |
| 自动验证 | `validation.json` / `validation.log`，76 项断言 | PASS；严格解析、身份/多驱/状态、转义、快照变化/篡改、取消、4 MB stdout/stderr、串行锁、失败不重烧、补验、模拟 init/burn、真实离线 CLI |
| 原六组回归 | `cli-regression.log` / `cli-regression.json` | 6/6 PASS；复制原二进制与测试定义到独立临时树，没有覆盖历史 artifacts |
| 真实 GUI inspect/list | Job `2A178843-5A35-48E0-9423-7556B5649D48`，系统认证桥接 CLI receipt=0 | PASS；CD-R 2 Sessions/2 Tracks，4 文件，698243072 剩余 bytes |
| GUI 用户 staging | Job `AD0F0FE2-BC59-459B-B044-E91B37EF4B14`，目录 `gui/Validation/Fixtures/new-files` | PASS；2 文件 / 84 bytes；位于私有输入快照 |
| 真实 GUI 追加预览 | Job `8D989F4C-FFDA-4D42-AA6C-77D1FE2788E4`，`build → verify` | PASS；62-block continuation，仅生成镜像；4 旧文件沿用 LBA 33 / 11729 / 34 / 35；generator 旧 payload reads=0 |
| 真实 GUI readback | Job `EEC4E98A-1D6F-425B-8BB0-4A3BFF612B44`，原 ISO 先按用户权限复制 | PASS；全镜像/metadata/path tables/3 个旧文件与 1 个 Windows 新文件 hash；`PHYSICAL_READBACK_PASS` |
| 最终物理状态 | `process-state.txt`；readback stdout/result.txt；`active.json` 不存在 | 2 Sessions / 2 Tracks；latest=11702，NWA=18904，appendable；diskarbitrationd=`Ss`；无残留 optical worker |
| 只读交付限制 | bundle `LocalConfiguration.json`：`allowPhysicalWrites=false`；自动验证分别拒绝 init/burn | PASS；UI 与 runner 双层限制，系统认证之前拒绝写入 |

真实 GUI CLI 日志、退出收据、task records 和原证据归档见 [gui-readonly-20261010/index.json](gui-readonly-20261010/index.json)。归档保留原 manifest 的绝对路径，原临时文件没有清理，不能把迁移后的 archive 当成可独立 verify 包。部分 backend 在 command cwd 写出的 root-owned `toc.dat` 没有读权限，因此在归档中明确列为 `unreadableExtraFilesOmitted`；对应 CLI 已保存的 `before-toc.txt`、各次探测、stdout/stderr、readback cache/blocks/result 与 Windows 预期清单均保留。没有为了收集证据修改这些 root 文件的权限。

本轮 readback 使用历史光盘的最新 Session 原 ISO：`artifacts/cd-initial-roundtrip/readback/windows-session2.iso`。实际光盘镜像 SHA256 是 `6d2c02194fd730709c0da73ba15f9dd8b4ded3903eb990d888a4e107ce7e4841`。GUI 预览的两个新测试文件只在预览中，未写入该盘。旧 CLI 的 Lenovo CD-R/DVD-R Mac → Windows → Mac 证据仍属于基线。

尚未完成：

- GUI 实盘 init/burn（CD-R、DVD-R 分别实验）与该 GUI/权限/打包路径的 Windows 读取和追加验收。
- 新型号、多真实光驱、旧版 macOS、App Sandbox/书签。
- 真实授权交互取消、桥接丢失/进程崩溃、长任务关闭窗口的专门硬件测试；目前对应状态/锁/失败分支采用模拟或代码约束，未注入真实 Disk Arbitration 故障。
- SMAppService/XPC helper、开发者签名/公证、CLI/readcd/两后端/dylib 依赖重定位与自包含分发包。
- 临时证据被系统清理后的恢复、完整依赖迁移导出、容量与清理 UI；未知 worker 的安全人工恢复仍按现有诊断边界处理。

SwiftUI 窗口关闭不退出 App，物理运行或未知状态时禁止 Cmd-Q；没有 writer/osascript 强杀按钮。此生命周期约束已实现，未把专门的真实长任务关闭窗口测试标为 PASS。
