# DVD-R / UDF 2.01 VAT 实盘往返与正式本机版验收 — 2026-10-13

本张 Lenovo Ultraslim DVD XL6M 的 DVD-R 已完成 **Mac GUI 追加并实际读回 → Windows 人工读取并再追加 → Mac 最新目录与全部文件实际读回**。用户以返回盘能读取为条件授权正式化，已将受限 DVD-R VAT 支持纳入 **BurnLight 1.2.0（build 4）正式本机使用版**，安装到 `/Users/luzove/Applications/BurnLight.app`。这是当前受支持结构及本机实盘结果，不等于任意 UDF/光驱布局都通过。

小型机器材料：[验收 JSON](evidence/udf201-dvd-r-roundtrip-20261013.json)，SHA256 `87788bbf0861d377552951097ebeb883a90bf9e8308ef4d1aa40cedefbae021a`。完整本地材料 `/Users/luzove/Documents/Projects/BurnLight/artifacts/dvd-vat-return/20261013-093516`；包含用户文件和大缓存的 `artifacts` 不提交 Git。此前 [试用记录](udf201-dvd-r-trial-20261013.md)、CD-R 验收和原损伤八 Session 证据保持历史状态。

## 实际步骤与构建绑定

1. 用户执行试用 GUI 的正常 burn，任务 `F552EB7A-B8AC-4C32-87BF-0FC8DC46C60F`，默认冲突拒绝，输入 ZIP 为 16327 bytes，SHA256 `b37388f29ada6694bbfd62b72814d1d3180363be0cc315aa59db3e4bc9748a84`。归档 `actual-user-burn-archive` 证明使用 `3.02a09-appendcd-media6`、目标 Track 4/NWA 106784、写入 576 blocks、exit=0、正常完成 Session，以及全部旧内容写前 hash 与实际写后读回通过；盘从 3 到 4 个完成 Session，仍可追加。旧 payload 的生成读取计数为 0，不复制到新 Session。此操作由用户在 GUI 执行，本验收轮的 agent 没有再次写盘。
2. 用户明确反馈 Windows 能看到刻录文件，并追加 `swagger.json`，源大小 246137 bytes。这是 **Windows 人工反馈**；本轮未重新采集 Windows 版本、IOCTL、源 SHA256 或自动 hash 报告，不用 Mac hash 代替源 hash。
3. 返回 Mac 的首次检查正确拒绝旧 registry ID；刷新后绑定新 ID `4294987524`。实际只读检查任务 `AC3BF62B-CF25-464D-B3EF-6A168A4B8868` 和导出任务 `DADC8409-DD72-43A1-B9C3-17AFCF231230` 全新探测，导出器使用 `source-read`、`verification-read` 两份实际读取缓存，各自 `.tracks/.blocks` 限定范围。三个文件全量导出到独立目录，大小/hash/目录核对通过。
4. `tests/vat_oracle.py` 独立解析两缓存，逐文件比对全部内容、extent 和 UniqueID，两个结果完全一致；同时与实际 Mac burn 的 `after-read` 严格视图比对旧文件。导出文件、macOS `/Volumes/APPENDCD` 的三个挂载文件及独立解析 bytes/hash 全部一致。`swagger.json` 可解析为 JSON 对象，未保存内容到 Git。
5. 正式版删除 CLI/GUI 的 DVD 试用标识，版本升为 1.2.0/4；核心、后端与受限兼容策略未放宽。新安装包严格签名、完整 bundle hash 和 8 项受信任依赖核对通过；备份 `/Users/luzove/Applications/BurnLight-previous-20261013-094433-dvd-vat.app`。新 GUI 检查任务 `B543AB6B-1B88-4D1B-8034-E268DE0E71AB` 的原始记录见 `formal-inspect-record.json`，再次显示全部三个文件、5 Sessions/5 Tracks、可追加和明确 Windows 兼容提示，已无试用标识。

真实 burn/两次文件读回绑定的试用 CLI 与导出器 hash 在机器 JSON 的 checks/source pins 中；正式版安装后重新检查与离线镜像验证绑定以下产物，不能把先前写盘虚记为新构建执行：

- 独立 Debug CLI：`947342dc021765c3bb1e1d02136ccdc26e6fe1483359c9f77264cca66dc315de`，目录 `build-dvd-vat-release-20261013`。
- 正式 bundle CLI：`481f49bd45595ec51f25f6467bcacd52a49469017f836290030e2e59aff027a5`；导出器：`ff123f2c56e76c2f8cac17758d4c733e27b9e1ffacb0ba611e90229dc46ab474`。
- 正式 GUI：`6f5b4daea44b1f5594ca7284d40e2ed32fee285b17148312d148eef6f47e64a0`。
- 正式 ZIP：`build-dvd-vat-release-20261013-write/output/BurnLight-1.2.0-local-arm64.zip`，SHA256 `84aa431239bc30b0598235df1079a9f6fdd97a6253dacb3dc375300fbb7671f0`。
- 75 项源码 hash 的前后绑定见 `source-binding.json`。相对试用 checkpoint 的代码变更仅 `src/main.cpp`（帮助文字）、`ContentView.swift`（试用提示）、`gui/scripts/build.sh`（版本号）；文档和报告另更新。五项原 CLI/runtime/provenance 的基线 hash 全部保持。

## 最新入口、范围和历史引用

现场设备 `1,0,0`，BSD `/dev/disk4`，registry ID `4294987524`；这些值仅为验收快照，运行重新探测。5 个完成 Session/5 Tracks，appendable，无 unfinished Session，latest=113520，NWA=120160，剩余 2177728 blocks/4459986944 bytes。minfo 的 session 6 是下一空白 Session，不算完成刻录。

已记录 Track 区间（end-exclusive）：`[0,65264)`、`[93952,94112)`、`[100272,100624)`、`[106784,107360)`、`[113520,114000)`；间隙和未来容量不视为已记录数据。

Session 5 Anchor **113776** → main VDS **106816** / reserve VDS **106880**，两者均在 Session 4。最新 VAT **113999** → previous VAT **107359**（Mac 写入），由当前入口及映射确定权威三文件视图。旧日志 payload 属于 Session 3，Mac ZIP 属于 Session 4；最新 Windows 文件在 Session 5，有两个 allocation extents。没有把历史目录合并为当前目录。

| 文件 | bytes | 实际 extent（LBA/长度/offset） | UniqueID | SHA256 |
|---|---:|---|---:|---|
| 20261008_1-T-20261008103937-7cf51ef0.log | 3068 | 100576 / 3068 / 0 | 16 | `9ff8d85860f793293d639b242076ca9e592d3c1f3a39cf28bf113e71bc5cd314` |
| AlgoLogFileWatcher-delivery.zip | 16327 | 107076 / 16327 / 0 | 17 | `b37388f29ada6694bbfd62b72814d1d3180363be0cc315aa59db3e4bc9748a84` |
| swagger.json | 246137 | 113808 / 65536 / 0；113856 / 180601 / 0 | 18 | `4f9bf438f233b913b77a3c071eea3a8607c11eaf0dfa253535f58b22b1a8d4f4` |

新文件使最新视图与 Mac Session 4 的旧二文件视图明确不同。两个旧文件的 hash/extent/UniqueID 全部保持。

## Windows 偏差与离线后续追加

返回盘使用此前用户授权的 `windows-fid-location-v1`，两处 FID 的 location 为 expected 13401/recorded 6531、expected 13402/recorded 0。独立 oracle 与核心继续验证 checksum/CRC、最新 VAT、历史字节/身份来源及已记录范围，界面和收据明确标识兼容视图。此盘读取通过是 **受限 Windows 兼容 PASS**，`strictConformance=false`，不宣称 ECMA Tag Location 严格符合；没有新增宽泛容错或退回历史目录。

用返回盘实际缓存和仅用于离线验证的 55-byte 文件生成下一次 576-block continuation。Debug CLI 和 **已安装正式 bundle CLI** 均 build/verify 通过，独立复合视图严格解析通过；previous VAT=113999、新 VAT=120735，三个旧文件 hash/extent/UniqueID 全部保持。新镜像目录无 FID 偏差，复用旧 ICB/payload。材料 `return-offline-append.json`、`formal-bundle-offline-append.json`；这些镜像没有写盘，也不计为另一次物理追加 PASS。

## 验收状态和安装验证

| 项目 | 状态 | 证据 |
|---|---|---|
| DVD VAT Mac GUI 正常追加 | PASS 实盘 | `actual-user-burn-record.json`、`actual-user-burn-archive` |
| Windows 读取并再追加 | PASS 人工 | `baseline.json` 用户原始陈述；没有自动报告 |
| Mac 返回最新目录/全文件 | PASS 受限 Windows 兼容 | `independent-return-check.json`、`readback-bindings.json`、`returned-export-archive` |
| DVD 合法跨 Session 引用 | PASS | Session 5 Anchor → Session 4 VDS、VAT/data 历史引用；Track/缓存边界校验 |
| 返回盘后续追加生成 | PASS 离线 | `formal-bundle-offline-append.json`，不是新物理写入 |
| 正式 GUI 集成 | PASS | `formal-inspect-record.json`，最新 3 文件；153 GUI assertions |
| 原能力回归 | PASS 离线 | 7/7 CTest、50 conflict、6 VAT export、迁移路径下 ISO/Joliet init-build/verify |
| 正式安装/包验证 | PASS 本机 | `installation.json`、`release-check.json`；签名/完整 hash/8 pins/无外部动态依赖 |
| 原损伤 8 Session CD-R 全文件读取 | **NOT_PASS_MEDIUM_ERROR** | 原报告保留；不能用本张 DVD 或离线结果回填 |

构建使用独立目录、macOS26 SDK/arm64，未覆盖 `build/appendcd`、全局 runtime、Homebrew 库或历史发布包。ZIP 解压到含空格/引号/美元符号/中文路径后，与 build 和安装 bundle 全部 hash 一致；签名和离线首刻镜像验证通过。`git diff --check` 通过，最终 Git 状态及源码绑定保存于证据目录。本轮无 push、远程发布、额外物理写盘、recover、自动关闭会话、强制解锁或永久安全设置修改。

正式本机支持保持 type-1 + Virtual Partition Map / UDF 2.01 当前结构，CD-R 使用 vat9，DVD-R 使用 media6，16-block DVD ECC 对齐和设备/NWA/容量/旧内容复核保留。其他 UDF 映射、RW/BD、任意第三方布局和 VAT recovery 不开放。macOS 首刻继续 ISO9660 + Joliet，未新增 UDF 2.01 首刻。ASan/UBSan 为先前核心修改验收材料，本轮仅展示/版本变更未新增 sanitizer 运行；不把 macOS 不支持的 leak detection 记为 PASS。当前是本机 ad hoc 使用版，未做 Developer ID 公证；其他实际光驱/介质布局仍需各自验收。
