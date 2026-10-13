# BurnLight 正式本机使用版 1.2.1

2026-10-13 用户全量试用反馈无异常，1.2.1 / build 5 按用户要求暂时结档。当前交付与验收边界见 [结档记录](../docs/release-1.2.1-closeout-20261013.md)；后续问题另行修复。

新 Git checkout 的基准恢复、构建步骤与文件范围见 [仓库说明](../docs/repository.md)。本机安装、发布包和实盘材料路径是历史交付位置，应用二进制与用户材料不随 Git 上传。

2026-10-12 新源码增加 CD-R / UDF 2.01/VAT 的目录、预览与导出支持，显示真实版本并将预览/导出绑定到最新 VAT 视图。只支持 type-1 + Virtual Partition Map；其他版本/映射在预览或写入前明确失败。内嵌旧文件保持原 ICB 的内容地址。首次刻录继续 ISO9660/Joliet，DVD recovery 不含 VAT。

当前独立只读测试包为 `build-udf201-gui-compatible/output/BurnLight.app`，未覆盖已安装应用或历史发布包。使用 `BURNLIGHT_BUILD_ROOT="$PWD/build-udf201-gui-compatible" ./gui/scripts/test.sh` 重建；新的 vat9 后端独立构建和纳入 bundle 信任清单，旧 media6/initial7 pin 保持。真实八 Session 盘的目录读取成功，但一个旧文件报介质错误；详见 [初始报告](../docs/udf201-acceptance-20261012.md)。

独立可写验收包为 `build-udf201-gui-compatible-write/output/BurnLight.app`，同目录附 zip；相同验证代码/八项工具与库 hash，仅配置开启写入并重签名，未安装、未通过此包写盘。若需从源码完整重建，使用 `BURNLIGHT_BUILD_ROOT="$PWD/build-udf201-gui-compatible-write" BURNLIGHT_ENABLE_WRITES=1 ./gui/scripts/build.sh`。vat9 正常 burn 的完整物理验收仍未完成。

随后换入的同格式单 Session 盘已通过 GUI 导出适配器的全部文件导出和独立真实复读，详见 [换盘验收](../docs/udf201-replacement-acceptance-20261012.md)。此结果不代表原损伤盘全文件读取、GUI 写盘或 Windows 往返已通过。

随后 CLI 实盘追加发现 vat8 未结束 Session 的缺陷，已修复并新增回归；CLI/打包拒绝旧 vat8，GUI 不自动结束失败会话或重烧。经用户单独授权完成已有会话后，新旧两文件实际导出通过。Windows 11 25H2 资源管理器再次追加 JSON 后，现盘 3 个完成 Session，最终导出适配器从新入口/VAT 对全部三文件实际导出、两次独立实读和内容比对通过，旧 payload 地址保持。见 [当前返回盘记录](../docs/udf201-windows-return-20261012.md)；[会话完成](../docs/udf201-session-completion-20261012.md)和 [失败阶段](../docs/udf201-mac-attempt-20261012.md)保留，不算 GUI 写盘或 vat9 正常 burn 完整物理 PASS。

当前 Windows 返回目录的两处 FID 位置偏差经用户明确授权使用受限 `windows-fid-location-v1`，界面标记 Windows 兼容视图，目标绑定/日志/导出收据保存该标识。核心证明历史来源并继续校验 CRC/VAT/实际读取范围，不以此宣称规范严格符合。146 条 GUI 断言、七组内置 CLI 回归、50 项冲突、8 项短卷、10 项原导出及 5 项 VAT 导出回归通过；原生窗口的实际写入操作未在此阶段执行。

最新阶段已完成修复后正常 CLI vat9 写入与第 4 Session 实际读回，最终 GUI 导出适配器四文件提取/两次严格独立实读通过。最终只读包为 `build-udf201-gui-publication/output/BurnLight.app`，可写本地包为 `build-udf201-gui-publication-write/output/BurnLight.app`（同目录 zip），未安装。核心增加同一光驱的最多 10 秒介质发布等待，不重试 writer；GUI 146 项及上述全部离线回归重新通过。当前新目录无 Windows FID 偏差，旧三个 hash/地址保持，盘仍可追加。用户随后确认最新 Session 4 的四文件在 Windows 均可见且能打开；Windows 自动 hash 报告未采集。见 [最新报告](../docs/udf201-vat9-normal-burn-20261012.md)；此前待 vat9 实测的段落为历史阶段边界，原损伤八 Session 全文件读取仍未通过。

原生 SwiftUI + AppKit，Apple Silicon / macOS 26。用户确认本次发布为**本机正式使用、保留系统认证、不需开发者账号**。用户后来明确授权当前光盘及 Desktop/Burn 实盘验收，并要求加入同路径文件跳过/替换。1.1 可显式构建启用刻录的本机包；只读 1.0 归档仍保留。Windows 新任务结果单独验收。

## 安装与运行

2026-10-13 DVD-R / UDF 2.01/VAT 已纳入 **1.2.0 正式本机使用版**。本张 Lenovo XL6M DVD-R 经 GUI 追加 ZIP/实际读回、Windows 人工读取并再追加 `swagger.json`、Mac 返回盘三文件两次实读与导出通过；旧 hash/extent/UniqueID 保持。DVD 使用 media6 incremental，CD-R VAT 使用 vat9；不放开 RW/BD 或 VAT recovery。去除试用提示，保留受限 Windows 格式偏差提示。正式包、安装及构建绑定见 [往返验收](../docs/udf201-dvd-r-roundtrip-20261013.md)，此前 [试用记录](../docs/udf201-dvd-r-trial-20261013.md)保持历史状态。

2026-10-13 又修复追加按钮状态说明：预览成功时仍会显示尚未解除的历史写入锁定原因，按钮准入与说明共用判断。原损伤八 Session 盘的一条历史任务经退出收据、原始读错误和对应核心控制流程核对，确认在写前复读失败，未调用数据写入；保留原归档并纠正该任务索引后，健康六 Session 盘的原生追加预览成功，“追加刻录…”已启用。151 项 GUI 检查通过，新包已更新安装，详见[按钮修复验收](../docs/gui-append-button-fix-20261013.md)。本轮没有真实刻录，未知执行器与未确认的写后任务仍保持准入保护。

2026-10-12 的六 Session 修复包已安装到 `/Users/luzove/Applications/BurnLight.app` 并正常启动，旧版保留为 `BurnLight-previous-20261012-170149-3d6f90f5.app`。原生“检查介质与目录”实际通过，显示最新五文件（含 `MybatisCodeHelperConfig.json`、`lssjk.jar` 和两个 Mac 文本）、UDF 2.01 / VAT、六 Session 和可追加状态。Windows 保留旧 FID 的来源与合法已删除记录已验证；五文件实际导出/两次复读、四个旧文件 hash/地址保持及全部离线 GUI 回归通过。本轮没有写盘。详见 [修复验收](../docs/udf201-gui-six-session-readback-20261012.md)。只读包为 `build-udf201-gui-history/output/BurnLight.app`，已安装的可写包为 `build-udf201-gui-history-write/output/BurnLight.app`；首刻仍使用原 ISO9660 + Joliet 路径。下文“本次 VAT 本地包尚未覆盖安装”及前述 publication 包是首次交付的历史状态。

历史版本已安装到 `/Users/luzove/Applications/BurnLight.app`，本次 VAT 本地包尚未覆盖安装。历史自包含包携带独立编译的 appendcd、只读导出器、media6 / initial7 两个既有后端、readcd、libisofs 和 libudfread；运行不需要项目目录或 Homebrew。本次包另增加隔离的 vat9 后端。原 `build/appendcd` 和两个原 runtime 保持基线；共享核心已加入显式冲突策略、Session 地址一致检查和短卷导入兼容；默认冲突拒绝、生成镜像校验及写入后端保留。修改前源码另有快照。

```sh
open /Users/luzove/Applications/BurnLight.app
```

当前发布包：`build-gui-cleanup-20261013-final-write/output/BurnLight-1.2.1-local-arm64.zip` 与同名 `.sha256`。历史 `gui/output/BurnLight-1.1.1-local-arm64.zip` 保留。可将 `.app` 搬到本机其他用户可访问的正常应用目录。使用本机 ad hoc 签名，没有 Developer ID、公证或 helper，不修改 SIP/TCC/xcode-select 等永久系统设置；这不是面向其他机器的公证发行包。

## 清理本地存储

1.2.1 新增左下角“清理本地存储…”：先预览占用和清单，支持逐项选择及 7/30 天或全部已结束任务。默认清理有完整归档的临时副本，保留可恢复历史；另可清理过期历史归档并留下小型任务摘要。确认后永久删除，无自动清理，无管理员认证。

当前快照/预览、活动或未知 worker、待检查或失败刻录和坏索引不会因清理而解除保护。共享快照核对全部归档副本，删除临时副本前独立复读归档 hash。原始输入、用户导出、项目验收材料和应用安装包不纳入。空间按 `st_blocks` 估算，稀疏镜像不按逻辑长度计费。详细实现和验证见 [清理验收](../docs/gui-storage-cleanup-20261013.md)。

## 使用

1. 启动自动发现光驱，单驱自动选择，多驱明确选择。刷新/换设备使原介质与预览失效。
2. “检查介质与目录”经系统认证串行 inspect/list，显示盘型、空白、可追加、容量、Session/Track 与最新完整 Session 目录。核心交叉核对 msinfo/minfo，GUI 核对 inspect/list 的身份与几何；矛盾结果拒绝显示为最新目录。空白盘不调用 list。
3. 光盘表支持 Command/Shift 多选文件或目录；目录包含后代，未选择时导出全部。“导出到本地…”新建 `BurnLight-export-UUID`，保留相对目录与内容，不覆盖已有文件。原 C++ 目录/extent 读取、独立实际光盘复读、缓存和本地大小/hash/完整目录核对全部通过才提交最终目录；失败保留隐藏 `.partial` 和诊断。
4. 选择本次新增目录，以用户权限建立只读快照，保留名称/子目录、排除新增输入的普通 `.DS_Store`，拒绝 symlink/特殊文件/控制字符，两遍读取、hash 和文件状态核对识别源变化。可取消 staging。已有光盘的 `.DS_Store` 导出仍保留。
5. 同路径普通文件在预览前列出并选择本次全部跳过或替换；同名目录合并，目录/文件类型、Windows 大小写/Unicode 名称碰撞仍拒绝。跳过保留旧文件 extent；替换只改变最新 Session 的引用，旧数据不会被擦除，消耗新空间。全部跳过且没有新内容时停止。
6. “离线首刻预览”调用 init-build；已录完整可追加盘调用只读 build，再独立 verify。成功仅表示镜像验证。追加预览不计算旧光盘 payload hash 基线，真正 burn 流程仍保留完整写前复查和全文件实际读回。
7. 预览成功后才启用明确 init/burn 的确认。使用同一份固定快照和冲突选择；核心重新复查并选择 CD/DVD 参数，实际写入、关闭 Session 和全文件读回通过才成功，GUI 再只读刷新最新目录。
8. “原镜像只读补验”先按用户权限固定原 ISO 和同名 manifest，再调用真实 readback。写后失败必须先检查介质，再使用失败任务原镜像；不自动重烧、不把 init/burn 自动互换、不从另一张盘的 PASS 解除失败记录。
9. “任务日志”在命令退出后展示 stdout/stderr，界面显示不确定进度。“历史记录”打开持久材料、导出诊断；诊断包含原文件和本地路径，只保存到用户选定目录，不自动上传。Windows 预期清单导出不等于 Windows PASS。

完整 Session 且可追加的 CD-R/DVD-R 保持原 ISO9660/Joliet、受支持 UDF 1.02 的物理读取范围；新源码还支持上述受限 CD-R/DVD-R / UDF 2.01/VAT，包含可解析的内嵌文件。封盘、ROM、空白/未完成 Session、其他文件系统及无法映射的结构仍拒绝导出；原 UDF 1.02 的 inline/unmapped 限制保持。文件导出保存名称、目录、内容，未提供原权限/时间戳/扩展属性还原。

## 权限与失败处理

应用以普通用户身份运行，macOS `do shell script … with administrator privileges` 负责系统认证，应用不收集/保存密码。每个参数独立 POSIX quoting，再进行 AppleScript 字符串编码；普通调用使用 executableURL + argv。没有 `sudo -S`、外层短超时或强杀 writer/osascript。管理员只访问私有 `/private/tmp` 输入/任务；Desktop/Documents 等本地目标由 GUI 用户权限打开。

所有物理请求（包括只读 inspect/list/build/readback/export）共享跨实例 flock 与 `/private/tmp/burnlight-physical-<uid>/active.json`。仅明确 CLI 退出收据或明确认证取消才释放准入。未知 worker 不启动新任务、不正常退出；“核对执行器退出收据”只在有效私有任务收据、无 worker 和正常 Disk Arbitration 服务均得到核对时恢复，并保留写后检查状态，绝不推断刻录成功、不发服务信号、不提供强制解锁。

关闭窗口不退出应用；任何任务/归档进行中或执行器状态未知时阻止 Cmd-Q。GUI 不控制终端另起的 CLI，原核心设备独占与服务 guard 继续拒绝冲突。

## 持久材料

索引：`~/Library/Application Support/BurnLight/Records/*.json`。可读日志、镜像、缓存、`.blocks` 和输入快照按 SHA-256 归档到同级 `Archives/UUID/`。原任务路径仍在 `/private/tmp/burnlight-job-UUID`，输入在 `/private/tmp/burnlight-input-XXXXXX/files`；完成后先保存任务结果，再归档。归档失败保留实际任务结果和警告，原临时材料不删除。

`archive.json` 保存原路径、完整目录/hash 与不可读额外文件的排除名单；例如 backend `toc.dat` 可能无法由 GUI 读取，核心可读 `before-toc.txt` 仍保留。稀疏复制使用系统 COPYFILE_DATA_SPARSE，在不支持时退回普通复制；当前测试文件系统没有观测到稀疏分配，不能据此承诺节省空间。缓存/归档可能很大，尚无空间管理或自动清理 UI。

continuation manifest 含绝对 cache 引用；归档原文不改。失败任务原镜像缺失时，可按记录核对完整归档，并恢复到该任务原位置，不覆盖不同内容、重新固定输入权限。损坏索引单独警告，不隐藏其他历史，并阻止物理写入。导出的诊断副本是证据，移动副本不能直接当作可迁移 verify 包。材料不自动清理。

## 源码构建与验证

```sh
cd /Users/luzove/Documents/Projects/BurnLight
BURNLIGHT_ENABLE_WRITES=1 ./gui/scripts/build.sh
BURNLIGHT_EVIDENCE_DIR="$PWD/gui/evidence/write-acceptance-20261010" ./gui/scripts/test.sh
BURNLIGHT_EVIDENCE_DIR="$PWD/gui/evidence/write-acceptance-20261010" python3 ./gui/scripts/cli-regression.py
BURNLIGHT_EVIDENCE_DIR="$PWD/gui/evidence/write-acceptance-20261010" python3 ./gui/Validation/export_offline.py
BURNLIGHT_EVIDENCE_DIR="$PWD/gui/evidence/write-acceptance-20261010" python3 ./gui/Validation/conflict_offline.py
BURNLIGHT_EVIDENCE_DIR="$PWD/gui/evidence/write-acceptance-20261010/windows-return" python3 ./gui/Validation/compact_session.py
# test.sh 刻意产出只读包；测试后显式启用刻录再打包
BURNLIGHT_ENABLE_WRITES=1 ./gui/scripts/build.sh
./gui/scripts/release.sh --no-build
# 空闲时正常退出应用，再安装；旧安装留作备份
python3 ./gui/scripts/install-local.py
```

明确 SDK：`/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk`；arm64 / macOS 26，实际机器为 macOS 26.6。SwiftPM 工程已提供，但本机 swift-package 自身因 BuildServerProtocol 缺符号崩溃，默认用 xcrun swiftc 编译相同源码；不修改全局工具链。构建需要既有 CMake/pkg-config、Homebrew libisofs/libudfread/readcd 与两个基线 runtime。构建/安装/打包检测物理 active 标记，拒绝任务期间替换。

`gui/Adapter/RuntimePaths.hpp` 仅用于独立编译的 GUI CLI，将三个工具路径定位到自身目录。dylib install name 仅改 bundle 副本后重签名。`Resources/Provenance.json` 保存原基线和依赖来源/hash，`LocalConfiguration.json` 保存 bundle 相对路径及七项 SHA-256，每次执行复查。它是本机使用信任检查，不等于签名 XPC helper 的分发级 root 信任协议。第三方许可随 bundle 保存。

原六组 CTest 定义在独立搬移包上使用新 bundle CLI 和另编译的原测试源码；Python 测试不会清理原历史 artifacts。`build` / `burn` 新增 `--on-conflict skip|replace`，不传时继续拒绝同路径文件，其他操作拒绝此参数。ISO/Joliet/UDF writer 保留原格式、未替换文件旧 extent、CD/DVD 参数、写前身份/地址/容量/metadata/hash、实际 readback 和 Disk Arbitration guard。六组原回归保持原定义，独立新增 50 条冲突断言及 ASan/UBSan；当前 macOS sanitizer 不支持 leak detection，此项未声称通过。

本轮实盘与冲突证据见 [1.1 验收](evidence/write-acceptance-20261010/acceptance.md)。原 [1.0 只读本机版验收](evidence/local-release-20261010/acceptance.md) 保留当时结果。此前开发和导出结果分别保存在 [首版只读验收](evidence/acceptance.md) 和 [导出功能验收](evidence/export-20261010/acceptance.md)，属于当时产物，不能替代本轮打包验证。

2026-10-10 历史阶段的剩余验收边界（DVD-R GUI 追加已由 2026-10-13 往返报告补齐）：Windows 读取/打开原七文件和再次追加已由用户实机确认，返回 Mac 的八文件读回另见本轮报告；CD-R/DVD-R GUI 首刻、DVD-R GUI 实盘追加与 skip 实盘 burn 未完成；历史 CLI 两盘闭环不算新 GUI PASS。其他光驱/多驱、大盘/满盘/拔盘、真实桥接异常或服务故障未测试。未知写入缺少原镜像或可信身份时仍需人工检查，不能强制解锁。旧系统/Intel、Sandbox/书签、Developer ID 公证、SMAppService/XPC helper 与材料清理 UI 不在本机发行范围。
