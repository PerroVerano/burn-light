# BurnLight 本机使用版 1.1.1

新 Git checkout 的基准恢复、构建步骤与文件范围见 [仓库说明](../docs/repository.md)。本机安装、发布包和实盘材料路径是历史交付位置，应用二进制与用户材料不随 Git 上传。

原生 SwiftUI + AppKit，Apple Silicon / macOS 26。用户确认本次发布为**本机正式使用、保留系统认证、不需开发者账号**。用户后来明确授权当前光盘及 Desktop/Burn 实盘验收，并要求加入同路径文件跳过/替换。1.1 可显式构建启用刻录的本机包；只读 1.0 归档仍保留。Windows 新任务结果单独验收。

## 安装与运行

已安装到 `/Users/luzove/Applications/BurnLight.app`。自包含包携带独立编译的 appendcd、只读导出器、media6 / initial7 两个既有后端、readcd、libisofs 和 libudfread；运行不需要项目目录或 Homebrew。原 `build/appendcd` 和两个原 runtime 保持基线；共享核心已加入显式冲突策略、Session 地址一致检查和短卷导入兼容；默认冲突拒绝、生成镜像校验及写入后端保留。修改前源码另有快照。

```sh
open /Users/luzove/Applications/BurnLight.app
```

发布包：`gui/output/BurnLight-1.1.1-local-arm64.zip` 与同名 `.sha256`。可将 `.app` 搬到本机其他用户可访问的正常应用目录。使用本机 ad hoc 签名，没有 Developer ID、公证或 helper，不修改 SIP/TCC/xcode-select 等永久系统设置；这不是面向其他机器的公证发行包。

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

完整 Session 且可追加的 CD-R/DVD-R 是目前原核心物理读取范围；ISO9660/Joliet 或受支持 UDF 1.02。封盘、ROM、空白/未完成 Session、其他文件系统以及 inline/unmapped UDF 暂不支持导出。文件导出保存名称、目录、内容，未提供原权限/时间戳/扩展属性还原。

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

剩余验收边界：Windows 读取/打开原七文件和再次追加已由用户实机确认，返回 Mac 的八文件读回另见本轮报告；CD-R/DVD-R GUI 首刻、DVD-R GUI 实盘追加与 skip 实盘 burn 未完成；历史 CLI 两盘闭环不算新 GUI PASS。其他光驱/多驱、大盘/满盘/拔盘、真实桥接异常或服务故障未测试。未知写入缺少原镜像或可信身份时仍需人工检查，不能强制解锁。旧系统/Intel、Sandbox/书签、Developer ID 公证、SMAppService/XPC helper 与材料清理 UI 不在本机发行范围。
