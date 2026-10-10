# BurnLight 1.1.1 本机版：GUI 实盘与 Windows 往返验收

日期：2026-10-10。发行范围：本机 Apple Silicon / macOS 26，SwiftUI + AppKit，系统管理员认证、本地 ad hoc 签名，无开发者账号与常驻 helper。用户明确指定当前已有完整 Session 的光盘、优先 `/Users/luzove/Desktop/Burn`，授权生成小文件，并解除源码限制以加入同路径跳过/替换。

## 最终结果

本任务指定的 CD-R 已完成 **GUI 两次追加 → Mac 实际读回 → Windows 七文件可见并正常打开 → Windows 追加 AlgoLogFileWatcher.java → 修复并通过 Mac GUI 八文件实际读取/导出**。GUI 1.1.1 已安装并实际运行；本轮返回盘核验没有再写盘。

| 项目 | 结果及证据 |
| --- | --- |
| 最终编译 / 安装 / 发布 | PASS，明确 MacOSX26 SDK、arm64/macOS 26；windows-return/build.log、release-check.json 核对构建 / 安装 / ZIP、deep strict 签名、7 个依赖 pin、无 Homebrew 运行依赖、中文/引号/美元路径搬移后的离线 init-build/verify |
| GUI / 状态 / 文件边界 | PASS，最终 136 条断言；冲突决策及预览绑定、参数/历史、全局物理锁、失败/未知锁定与原镜像补验、staging/导出/归档/恢复、严格 inspect/list 绑定；windows-return/validation.json |
| 六组 CLI 回归 | PASS，最终 6/6，六组定义保持，drive_selection 增加矛盾/重复/越界地址测试；原历史 artifacts 未覆盖。首次新重复地址测试失败、修复和最后通过日志均保存；windows-return/cli-regression.json/log |
| ISO/Joliet/UDF 冲突 | PASS，50 条离线断言，显式 skip/replace 的实际目录/payload、空文件/嵌套、旧 extent、全部跳过、名称/类型与参数边界；windows-return/conflict-offline.json |
| 短卷导入 | PASS，独立 mkisofs -no-pad 生成完整 31-block Joliet 镜像，读取/导出/追加/严格 verify，以及损坏卷与生成镜像 32-block 约束共 8 条；windows-return/compact-session.json |
| sanitizer | PASS，最终核心的 ASan/UBSan drive_selection、8 条短卷与 50 条冲突用例；windows-return/asan-run.json。此 macOS runtime 不支持 detect_leaks，未声称 leak detection 通过 |
| 导出离线回归 | PASS，10 个场景，包括历史实际光盘 cache 的独立 oracle；windows-return/adapter-offline.json |
| 第一次实际 GUI burn（1.1.0） | PASS，Desktop/Burn 两文件合计 809823 bytes，经用户快照、物理预览/verify、GUI 确认、认证、核心写入/实际读回及目录刷新；4 个历史文件保持，新增 Java 7304 bytes 与 DOCX 802519 bytes。CD-R 从 2 到 3 Sessions/Tracks |
| skip GUI 预览（1.1.0） | PASS，重名提示选择 skip，真实光盘只读 build/verify 保留 Java 7304 bytes / LBA=18932，仅增加 receipt 的预览；没有提交 skip burn |
| replace GUI burn（1.1.0） | PASS，独立副本追加 54 bytes 注释，Desktop 原文件未改；GUI 选择 replace 并重新预览、确认 burn。Java 最新为 7358 bytes / LBA=26292，另加 54 bytes receipt；其他 5 文件的 extent/hash 保持。CD-R 到 4 Sessions/Tracks |
| 写后七文件 GUI 导出（1.1.0） | PASS，actual optical 复读与用户权限本地拷贝/hash/目录完整性通过；physical-conflict-check.json、local-copy/ |
| Windows 实机 | MANUAL_PASS，用户直接报告七文件全部显示且正常打开、DOCX/Java 大小一致，并再次刻录 AlgoLogFileWatcher.java；windows/manual-result.json 保存原话。Windows IOCTL/自动 hash/版本与型号报告未采集 |
| 返回盘 GUI 读取与导出（1.1.1） | PASS，严格 inspect/list 均选择第五 Session，显示 8 文件，GUI 导出全部；每个文件在 source-read、verification-read 两份实际光盘有效 cache 上分别计算 hash，再与本地副本核对。原七文件的大小/hash/extent 均不变，Desktop 原文件也未变；windows-return/return-readback-check.json |
| 最终进程/系统状态 | PASS，所有 CLI 有明确退出收据，无 writer/readcd/osascript 或 active 标记，Disk Arbitration 正常运行；windows-return/process-state.json。任务已归档，未硬杀进程或修改永久系统安全设置 |

当前光驱 Lenovo Ultraslim DVD XL6M，Registry ID=4295034345、device=1,0,0。当前 CD-R **5 Sessions / 5 Tracks，latest=33464，NWA=40666，剩余 319177 blocks / 653674496 bytes，完整且仍可追加**。这些是核心实际诊断值，GUI 不把它们作为下次写入参数。

新增 `AlgoLogFileWatcher.java` 为 **2564 bytes**，实际数据 LBA=33493，SHA256 `e1dfef26bb3ed4f724edf967227635aea4488bb2f9c91c4eb46504dda1983e02`。Windows 原文件 hash 未提供；该文件证明范围是两次独立实际光盘读取一致，并且本地拷贝一致。

## 最终产物及真实构建绑定

- 安装：`/Users/luzove/Applications/BurnLight.app`，版本 1.1.1，GUI SHA256 `c68460e6e478afe1676a8717d08f3416bb0c0679f01ed6f4ea97b2b9dabf8ec4`。
- 发布：`gui/output/BurnLight-1.1.1-local-arm64.zip`，SHA256 `ef76dbc72b6e8b82b00fbbb4f59501fd0f65eae44eac4589137471c62402c4a1`，同名 .sha256 保存。旧 1.1.0/1.0 ZIP 和旧安装备份保留。
- 最终返回盘检查：`44A8C222-4F9D-4F9F-B051-F4EE920C3233`；导出：`46B5B7F1-66D9-424E-885D-74110C6BA19A`；本地副本为 `/Users/luzove/Documents/Projects/BurnLight/gui/evidence/write-acceptance-20261010/windows-return/local-copy/BurnLight-export-46B5B7F1-66D9-424E-885D-74110C6BA19A`。
- windows-return/final-gui-jobs/index.json 保存最终 3 个 GUI 任务、6 个 CLI 请求，其中 0 个写入；实际 GUI 与发行包的依赖绑定见 final-build-binding.json。
- 实际 burn 在 1.1.0 执行，GUI SHA256 `218c2f74ae82cb65ce66a9b60a50ed7b4e48f4dca88245cad44fd3a8c0e5bf1b`，绑定在 physical-build-binding.json。1.1.1 增加扫描/短卷安全兼容检查，以离线及返回盘只读验收验证；没有把此前 burn 宣称为新二进制的额外实盘写入。
- 第一遍 burn：`EE38FFEE-330A-4CCB-A17C-97791DB8145B`，Session start=18904，镜像 SHA256 `bd33566ceee2738cb884b9a3a855875afc753bc1076324db933509b3c813e185`。
- skip 预览：`2B6BDEB9-2903-4ED8-844A-8B685A2C01C0`；replace 预览：`19894BD0-4D83-4652-8BB1-AAD2BD7F7947`；replace burn：`3651DFC7-29CC-4495-BB56-3D57B4FDF778`，start=26262，镜像 SHA256 `d2dfe0ef3fa4849a8cac52b1705bb0d14511aa6b2918bedd7f72edfcf1a4a944`。
- 写后七文件导出：`DAFB4311-0B9B-4ECF-86FE-1DCE256DDC45`。原 physical-jobs/ 保存 9 个关键 GUI 任务、21 请求、2 个 burn；另一个 staging 记录见 additional-staging-records.json。
- 完整原任务 tar、日志/收据/清单保留；原持久 Archives 在 `~/Library/Application Support/BurnLight/`。绝对 cache 引用原文不改，诊断材料不是可任意搬移的独立 verify 包。

## Windows 返回盘发现的问题及修复

1. 旧 GUI 的首次返回盘检查 `4EC88338-ABC8-4609-BFE5-D383BF6F9364`：inspect 得到最新起点 33464，而 list 的 msinfo 错给 0；同次 minfo 仍为 33464。结果错列首 Session 三文件，**不计为通过**。新 DiscScanner 原子交叉核对 msinfo/minfo 的 latest/NWA/leadout，矛盾时最多再查三次只读地址，仍不一致即失败；GUI 再核对 inspect/list 的身份、Session/Track、地址、容量和状态。没有降级使用旧起点或在 GUI 猜测 LBA。
2. 严格地址检查后的任务 `5516692E-4588-4998-AA67-A08242DA6BD2` 读到正确起点，但原导入器拒绝 Windows PVD/SVD 的 31-block 卷。改为检查描述符在导入卷内；UDF 识别限定于已知输入范围，短离线 ISO 不扫描越界。生成镜像仍要求原 32-block 下限与精确块数、双端序、目录/路径表、extent/hash、独立 library 导入。初次短卷回归失败和最后通过均保留。
3. 上述两次失败材料分别保存于 windows-return/failure-old-session/ 与 failure-short-volume/，对应各自旧应用 hash；failure-analysis.json 记录修复闭环。最终任务确认第五 Session 和八文件，不覆盖失败证据。

## 源码与原基线保护

原冲突扩展修改 IsoCore.hpp/cpp、UdfSession.hpp/cpp、main.cpp；后续一致性与短卷兼容修改 DiscScanner.hpp/cpp、IsoVerifier.cpp、UdfReader.hpp/cpp、main.cpp，drive_selection.cpp 增加地址拒绝用例。最终基线共比较 47 项，授权变化为 **10 个共享源码文件 + 1 个测试文件，其余 36 项不变**；原 build/appendcd、静态库、两个 runtime、backend patch、其他原 tests 和 CMake 保留。最终差异、hash 在 windows-return/core-final.patch、final-source-sha256.json、baseline-final.json；初始五文件冲突 diff 仍为 core-conflict.patch。

`build/burn --on-conflict skip|replace` 仅显式选择才处理同路径普通文件；默认仍拒绝，其他命令拒绝参数。目录合并、Windows 名称/类型拒绝、ISO/Joliet/UDF writer、未替换旧 extent、CD/DVD 参数、写前身份/介质/地址/容量/metadata/hash、实际 readback 和 Disk Arbitration guard 保留。replace 只更新最新 Session 引用，不能擦除或回收旧数据。GUI 不直接执行 cdrecord、不猜 LBA、不自动切换 init/burn、不重烧失败任务、不在物理任务中更新应用或 helper。

修改前快照为 artifacts/before-conflict-policy/20261010-140717 与 artifacts/baseline-before-gui/20261010-101609；最新索引仍核对。目录没有 Git 仓库，未 commit/push。

## 明确未完成项

- GUI CD-R/DVD-R 空白首刻、DVD-R GUI 实盘追加、skip 实盘 burn，以及其他型号/多台光驱、满盘/拔盘/复杂 UDF/真实桥接断联和服务故障未实测。历史 CLI 两种盘闭环不能回填为这些 GUI PASS。
- Windows IOCTL/自动 hash 与新增 Java 的 Windows 源 hash 未取得；现有 Windows 通过来自用户实际操作报告，Mac 八文件证明范围如上。
- ROM/RW/BD、封盘或不完整 Session 导出、任意 UDF、旧 macOS/Intel、Sandbox/书签、Developer ID/公证/XPC helper、空间清理 UI 不在本机版范围。当前归档未观测到节省稀疏分配空间，不承诺材料体积。
- 未知写入缺少可信退出/原镜像，或材料损坏，仍需人工检查；不强制解锁或自动重烧。未使用 Electron、Windows VM，也未改永久系统安全设置。
