# Windows 连续追加后的六 Session 读取与 GUI 修复验收

2026-10-12（Asia/Shanghai）：**当前六 Session UDF 2.01/VAT CD-R 的最新五文件读取、实际导出、两份全新缓存的独立内容验证和已安装 GUI 原生窗口列表均通过。** 修复版已安装到 `/Users/luzove/Applications/BurnLight.app`。本轮没有写盘，不把这次读取结果算作新构建的物理追加通过。

用户从 Windows 资源管理器再次追加后，旧 GUI 因零位置 FID 保留旧 EFE 而失败：`Windows zero-location FID ICB is not current transaction metadata`。修复增加首次分配 VAT 来源证明与合法已删除记录处理，保留 `windows-fid-location-v1` 显式标识。严格 oracle 仍拒绝盘上的 Tag Location 偏差，不宣称规范严格符合。实现与规范依据见[重启前检查点](udf201-gui-continuation-fix-20261012.md)；该历史记录未覆盖。

## 实际恢复、输入和最新入口

用户正常重启后，实际启动 UUID 从 `5AD91AB9-EDCB-4BC1-A913-764461443275` 变为 `71271690-BAF5-4691-9582-C6AC2AC24EC0`。旧只读 helper/drutil 已结束，旧 tmp 任务与 marker 不存在，共享 flock 可获得，Disk Arbitration 为 `Ss`。旧取证被系统重启中止，没有退出收据，不标记补读通过，也没有伪造收据、强杀或强制解锁。

最初连接的是另一张 UDF 1.02 CD-R，最新起点 43000、NWA 50604，修复版全新 list 正常显示七文件。此结果仅作该盘读取回归；经提示，用户放回目标 VAT 盘后重新识别和读取，未混用其状态或目录。

目标盘：Lenovo Ultraslim DVD XL6M、设备 `1,0,0`、`/dev/disk4`、本次 registry ID `4294972320`、CD-R、卷名「10 10 2026」。完整六 Session/六 Tracks、latest 133718、NWA 140918、剩余 218925 blocks / 448358400 bytes、appendable、无 unfinished Session。minfo 的第七 Session/Track 是下一空白可写项。

| 完成 Track/Session | 已记录区间（end-exclusive） |
| --- | --- |
| 1 | [0, 352) |
| 2 | [11754, 12330) |
| 3 | [19232, 19532) |
| 4 | [26434, 27010) |
| 5 | [33912, 126816) |
| 6 | [133718, 134016) |

最新 Anchor 133974 属于 Session 6；主 VDS 26466、备用 VDS 26530 属于 Session 4；物理分区起点 288 属于 Session 1。最新 VAT 134015 的链为 126815 → 27009 → 19531 → 12329 → 351 → 319。FSD 26722 属于 Session 4；当前 root ICB 133984 属于 Session 6。最新 view hash：`9154a4495dc1984fc68fcff2007a584adcc8391ae937725b2e48e27f252aa7dc`。

实际补齐历史目录和 jar EFE 后，JAR 的零位置 FID 来源证明为 Session 5 VAT 126815、同一虚拟 root 的目录 ICB 60832、保持的文件 ICB 126782；源目录中搬移 FID 又由 Session 4 VAT 27009 / root ICB 26729 严格证明。最新目录权威性没有降级到旧 VAT；清零 ICB 的已删除 Office 临时记录只从活动命名空间排除。

## 内容与 GUI 实验

新 GUI 包的 CLI 全新 list 成功。旧安装 CLI 对这份真实元数据缓存仍重现同一拒绝，证明故障与解析规则有关。新导出器绑定设备、介质和最新 view，使用正常 guard 实际提取五文件，并在另一份全新 `verification-read` 缓存复读。两个 cache 的 `.blocks` / `.tracks` 均保留；独立 Python oracle 分别校验最新目录、历史来源、CRC、实际地址及全部 payload，本地副本也匹配。

| 文件 | bytes | 实际数据 extent（LBA + byte length） | SHA256 |
| --- | ---: | --- | --- |
| 20261010更新步骤.docx | 14336 | 327+14336 | `7e0e6e9e44fca400aa9c4e7a4cf27b171a6046d8acac0ea88e5260414427dc39` |
| MybatisCodeHelperConfig.json | 11266 | 19496+11266 | `80b979156dc47c0a217974d2e8fcfa25c135a3c2596d2e14801ee2d144240a61` |
| burnlight-vat-mac-20261012.txt | 56 | 12046+56 | `faaabcbc28a26670d4b26b2a47b350d98d8a03c7778e22274931e636c6778b37` |
| burnlight-vat-post-windows-offline.txt | 46 | 26728+46 | `d839c5541edb618dc95092caed380bc30829b670d7ee5ca7605e0f506ed28928` |
| lssjk.jar | 189620624 | 34176+23068672; 45442+31518720; 60838+29347840; 75170+105377792; 126631+307600 | `697bc1ced0869f1203c8b07996494b699b7ff8e184baa4f7c1f0ea5f22b35e60` |

四个旧文件的 hash、extent 和 UniqueID 16/18/17/19 与先前四 Session 的真实基线一致。JAR 的 368 个 ZIP 条目 CRC 通过；Windows 原始 JAR 源 hash 未采集，这里证明两次实际光盘读取一致及副本完整，不冒充源文件比较。此次未重新执行挂载文件对照。

安装包来自 `build-udf201-gui-history-write/output/BurnLight.app`，由已验证只读包复制，只改 `allowPhysicalWrites=true` 和外层签名；八个受信任依赖 hash 完全保持。旧包保留于 `/Users/luzove/Applications/BurnLight-previous-20261012-170149-3d6f90f5.app`。完整 bundle 内容对照、八依赖 hash、deep/strict codesign 均通过。实际进程 PID 4367 的路径是新安装的主程序。

原生 GUI 任务 `BEF53587-1C46-43E7-8ED7-E19357DC2455`：drives、inspect、list 均正常退出 0、workerStateUnknown=false；窗口显示最新五文件、UDF 2.01 / VAT、兼容说明、六 Session/六 Tracks和可追加状态。用户原历史任务和归档保留；本次记录、命令收据、缓存、原生 AX 观察摘要另存证据。两份 root-owned 工作目录 `toc.dat` 未复制，光盘运行证据中的完整原始 TOC 文本已保存，原 workspace/archive 未动。

## 构建、回归与边界

源码 aggregate SHA256：`48b2632fbc4e6445dbbcf3c6b1e660f2cc0c3d4c30482830be50c845600cf84e`，与重启前检查点一致；独立 macOS26 SDK / arm64 构建。安装 CLI：`50f44fc8435cddb9347c7d9ee8558b9bd8f4c1f99b681495a7a1f5fd2c749b7a`；导出器：`8ce639e7e5159a17a770af008e1161b7e96f72a09fc6afc800ed8fd8a5789421`。基准 `build/appendcd`、两个已安装后端及 provenance 五项未变。

7/7 CTest（含 89 项 VAT）、146 项 GUI 断言、50 项冲突、8 项短卷、10 个既有适配器导出和6个 VAT 导出场景通过。重启前同源码 ASan/UBSan 的 7/7 和 89 项绑定保留，leak detection 不支持且不算通过。GUI CLI 回归另明确指定新 adapter build 重新完成，默认旧 adapter 路径产生的前一次日志不作为本轮新核心测试绑定。

| 项目 | 本轮结论与范围 |
| --- | --- |
| CURRENT_8SESSION_READ_PASS | 原损伤主样本仍 NOT_PASS_MEDIUM_ERROR；当前六 Session 不能回填 |
| CROSS_SESSION_REFERENCE_PASS | 当前六 Session 实读及独立八 Session fixture PASS |
| SINGLE_SESSION_REGRESSION_PASS | 同源码独立 fixture PASS；原单 Session 实盘结果仅保留历史构建绑定 |
| VAT_OFFLINE_APPEND_PASS | 同源码 89 项（含连续追加/旧 extent/历史来源）PASS |
| EXISTING_FEATURE_REGRESSION_PASS | 七组和 GUI 专项离线 PASS；另一张 UDF 1.02 盘本轮只读列表 PASS |
| GUI_INTEGRATION_PASS | 新包离线、实际导出及已安装原生五文件列表 PASS |
| MAC_PHYSICAL_APPEND_PASS | 本轮 NOT_RUN；此前 vat9 真实写入 PASS 保留其历史构建，不回填当前包 |
| WINDOWS_READ_AND_REAPPEND_PASS | 先前替换盘人工读取/资源管理器追加记录保留；本轮实际读取其最新五文件，未新增 Windows 自动报告 |
| MAC_RETURN_READBACK_PASS | 当前六 Session 五文件两份全新实际读取和独立验证 PASS |

最新包未新增物理刻录，不宣称原损伤八 Session 的完整功能验收已完成。macOS 首刻仍是现有 ISO9660 + Joliet，未扩展 UDF 首刻或 VAT recovery。

证据：`artifacts/udf201-gui-fix/20261012-165222-restart/`。完整 source/input/build/installation/evidence hash、服务恢复、Git 状态与上述范围绑定于[机器验收记录](evidence/udf201-gui-six-session-readback-20261012.json)。历史验收文件不改写，用户 payload 与大缓存仅在忽略的 artifacts 中保存。本轮 `git diff --check` 通过，无 commit/push/远程发布。
