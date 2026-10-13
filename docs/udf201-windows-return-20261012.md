# UDF 2.01/VAT Windows 返回盘验收（2026-10-12）

**替换盘的 Mac → Windows → Mac 内容往返已实际证明。** Windows 11 25H2 资源管理器正常打开 Mac 追加后的文件，再追加 `MybatisCodeHelperConfig.json`；返回 Mac 后，最新正常 CLI、独立解析器和 GUI 导出适配器全新读取三个文件通过，两个旧文件的 hash 和实际 payload 地址保持。使用用户明确授权的受限 `windows-fid-location-v1`，这张返回盘有两处 FID 位置偏差，**不宣称规范严格符合**。

完整原任务仍有物理验收边界：原损伤八 Session 盘不能全文件读取；修复后的 vat9 正常 burn 全流程尚未实测。之前的 Mac 实际追加是 vat8 写数据后，经另一次明确授权用 media6 完成已有会话，不能回填为 vat9 正常 burn PASS。macOS 首刻保持既有 ISO9660/Joliet 路径；其历史物理证据和当前离线首刻回归分别记录，没有新增 UDF 2.01 首刻。

## 用户报告与要求调整

用户人工报告：“Windows读取成功，可以正常打开，我又从Windows中刻录了一个‘MybatisCodeHelperConfig.json’的文件。”随后确认 Windows 11 25H2、拖入资源管理器、源大小 11266 bytes、未采集源 SHA256，盘已返回 Mac。没有 Windows 自动 hash/IOCTL 报告；Mac 光盘实际 hash 与挂载 hash 相等不能代替 Windows 源 hash 比对。

严格导入遇到两处 checksum/CRC 正确但位置错误的 FID，曾按原要求拒绝。已向用户说明必须改变严格拒绝要求才能兼容当前实际盘、且该选择不授权再次写盘；用户明确回复：“允许显式受限兼容分支。唯一目标就是完成macos和Windows的光盘双向读写以及初刻兼容，其余可以不择手段”。本次实现采用下述有历史证明的有限兼容。授权与范围保存于机器摘要及 `compatibility-authorization.json`；没有再次写盘、关闭会话、改安全设置或替换安装版。

## 实时盘片与权威入口

证据目录：`artifacts/udf201-acceptance/20261012/windows-return-131452/`。重新识别 Lenovo Ultraslim DVD XL6M、CD-R、卷 `10 10 2026`、device `1,0,0`、whole-disc `/dev/disk4`、registry `4295074771`、盘 ID `0x380500`。registry 随重连变化，没有沿用写前绑定。

3 个完成 Session / 3 条 Data Track，无 unfinished Session，仍可追加。NWA 26434，剩余 333409 blocks / 682821632 bytes。minfo 的 4 个 Session 包括下一空白 Session 4，不是已刻录四次。

| 已记录数据 | 区间，end-exclusive |
| --- | --- |
| Session 1 / Track 1 | `[0,352)` |
| Session 2 / Track 2 | `[11754,12330)` |
| Session 3 / Track 3 | `[19232,19532)` |

最新 VRS 为 NSR03，Anchor 19488（19232+256）→ 主 VDS 11786 / 备用 11850，均在 Session 2。物理分区从 Session 1 的 288 起，Partition Number 8192；map 0 为物理、map 1 为 Virtual。最新 VAT ICB **19531 → 12329 → 351 → 319**。19519 也是同一 Track 内较早 VAT 事务，但不是最新视图。TOC leadout 19534 比 minfo 数据末端 19532 多两块，未计入有效数据范围。

FSD 实际在 12042，root virtual 422 经最新 VAT 映射到 19504；root FE 引用两个物理目录 extent：12043 / 4096 bytes 与 19502 / 2116 bytes。旧 docx ICB 336 保持；Mac 文件 virtual 423 的 FE 被 Windows 更新到 19505，payload 仍在 12046。新 JSON virtual 424 → EFE 19506。普通 GUI 用户看到版本和兼容视图提示，详细地址在诊断中。

| 文件 | bytes | 实际 payload LBA / bytes | 实读 SHA256 |
| --- | ---: | --- | --- |
| `20261010更新步骤.docx` | 14336 | 327 / 14336，保持 | `7e0e6e9e44fca400aa9c4e7a4cf27b171a6046d8acac0ea88e5260414427dc39` |
| `burnlight-vat-mac-20261012.txt` | 56 | 12046 / 56，保持 | `faaabcbc28a26670d4b26b2a47b350d98d8a03c7778e22274931e636c6778b37` |
| `MybatisCodeHelperConfig.json` | 11266 | 19496 / 11266 | `80b979156dc47c0a217974d2e8fcfa25c135a3c2596d2e14801ee2d144240a61` |

挂载 `disk4s0` 经实时 diskutil 确认为同一 whole-disc 的 UDF，名称、大小、hash 与独立实际 payload 读取一致。新 JSON 在 Session 2 视图中不存在，证明读取最新事务。用户文件、大缓存和完整物理任务仅保存于忽略的 artifacts，不提交 Git，不输出 JSON 内容。

## Windows FID 偏差与有限兼容

| 当前 FID 所在绝对 LBA / 目录字节偏移 | 应为物理分区块号 | 实存 Tag Location | checksum / CRC |
| --- | ---: | ---: | --- |
| 19502 / 4096 | 19214 | 11757 | 通过 |
| 19503 / 6144 | 19215 | 0 | 通过 |

第一个 FID 与之前 Mac 目录尾部 FID 字节完全相等，搬到新 extent 后保留了旧位置。第二个新 Windows FID 的位置字段为 0。以上为实读事实，不推断 Windows 内部算法，也不把偏差称作合法跨 Session 地址。物理 directory allocation 的 Tag Location 应是物理分区块号，依据 [OSTA UDF 2.01 §2.3.1.3、§2.3.10](https://13thmonkey.org/documentation/UDF/udf201.pdf) 和 [ECMA-167 第三版 Part 4 §7.2.8](https://ecma-international.org/wp-content/uploads/ECMA-167_3rd_edition_june_1997.pdf)。

核心兼容须同时满足：

- 当前 VAT ICB 与当前目录 FE 为已识别 Microsoft 元数据；FID kind/version/checksum/CRC 完整通过。
- 非零错误位置在已验证 previous VAT 中找到同一虚拟目录、相同 UniqueID/字节偏移、正确历史位置以及完整字节相等的 FID；历史目录/子 ICB 严格校验通过，且目录位于较早 Session。本盘证明来源为目录 ICB 12047、VAT ICB 12329（均 Session 2）。
- 零位置 FID 必须位于已有搬移证明的同一目录，flags/implementation-use 受限，并指向当前 Session 的 Microsoft 普通 EFE；所有历史 VAT 未映射该虚拟 ICB，FID UniqueID 等于 EFE UniqueID，且在最新 VAT UniqueID 范围内。
- 实际 Track、有效缓存、分区容量、CRC、最新 VAT、循环及资源边界保持；历史证明最多 100000 次探测、累计 64 MiB 目录数据。历史 map 使用独立参数，不能修改当前 VAT、降级最新目录或合并历史目录。

兼容名称进入 CLI 输出、view hash、GUI 目标绑定/提示、导出收据和 writer 来源日志；未知/重复或用于非 VAT 的名称拒绝。从兼容来源追加时生成位置字段正确的新目录，不复制偏差。独立 oracle **默认仍严格拒绝**，只有显式 `windows_fid_compatibility=True` 才核对同一来源证明，并记录 `strictConformance=false`。

## 三组实际读取证据

初始正常 CLI、默认严格 oracle、GUI 导出都因这两处位置错误拒绝；GUI 没有成功收据或部分导出。全部 1228 已记录块的只读 helper 取证使用项目 IOKit/readcd、设备绑定、物理锁和 guard，逐 Track 读取不跨间隙，保留 `.blocks`。诊断版结果 `FAIL_FID_TAG_LOCATION_NOT_AUTHORITATIVE_IMPORT_PASS` 保留，不回填为产品成功。

用户授权和实现之后另行执行：

| 检查 | 实际输入 / 当前构建 | 结果 / 证据 |
| --- | --- | --- |
| 正常物理 CLI `list --hash-files` | 新光盘读取缓存，CLI `f70766d1…` | 退出 0；全部三文件和两处兼容诊断；`compatible-normal-physical-read/` |
| 独立兼容解析 / 挂载 / 旧基线比对 | 上述实际缓存，独立 oracle `b64d0b19…` | 三文件名称/大小/hash 一致，旧 payload 327/12046 保持；`independent-compatible-normal-read.json` |
| 最终 GUI 导出适配器 | 当前 bundle CLI `5fc44ac5…` / adapter `ce081564…`，源读取及新 verification 读取 | `OPTICAL EXPORT VERIFIED`，三文件导出收据、两份缓存均独立解析且 hash/extent 一致；`independent-compatible-gui-export.json` |

当前 view 为 `d8900ba6e358f218120b4785bc1e369b141b8f33be209a43d7adbd9a6f0b73aa`，包含 `windows-fid-location-v1`。GUI 导出与预览使用该绑定；设备 registry 使用本次重新发现值。GUI 验证实际运行导出适配器和真实光盘读取，未操作原生 GUI 窗口写盘。

## 离线追加、回归及产物

从真实返回缓存生成 46-byte 匿名文本 `burnlight-vat-post-windows-offline.txt` 的后续镜像，建模 start=26434、576 blocks / 1179648 bytes，镜像 SHA256 `ac87b3797d8a655b30a8ccb3159f9288156e004abe6b4e4b8834262ce1ef644a`。独立默认严格 oracle 检查新 VDS、最后 VAT ICB 27009 → 19531、新目录 Tag/CRC/计数/布局通过；三个旧文件的 hash/extent/inline offset/UniqueID 保持，旧 payload 未复制到新镜像。`compatible-offline-append/report.json` 明确 `notBurned=true`，不是实际 Session 4。

| 最终受控检查 | 结果 |
| --- | --- |
| 原六组 CTest + VAT 专项 | 7/7 通过，当前 CLI hash `f70766d1…` |
| VAT 独立 fixture / append / 边界 | 71 项通过，包括独立八 Session、非零地址/分区、两次追加、旧 inline、skip/replace、稀疏缺块、最新 VAT 损坏、目录分段、严格拒绝及有限兼容边界 |
| ASan/UBSan 同七组 | 7/7，通过；`detect_leaks=0`，不宣称 leak detection PASS |
| GUI 模型与绑定 | 146 断言通过；物理任务模拟与真实适配器验收分别记录 |
| bundle 内 CLI 原七组 | 7/7 通过 |
| 冲突 / 31-block 短卷 | 50 / 8 项通过 |
| 原导出 / VAT 导出 | 10 / 5 场景通过，包括 Windows 兼容收据 |
| 本地包信任清单 / 签名 | 八项工具和库 hash 一致，codesign deep/strict 通过 |

只读包：`build-udf201-gui-compatible/output/BurnLight.app`。可写本地验收包：`build-udf201-gui-compatible-write/output/BurnLight.app`；同目录 `BurnLight-udf201-windows-compatible-local-arm64.zip`。两包共用已验证工具，只改写入开关和签名；未安装、未用该包写盘。旧基准 CLI、media6、initial7 和 provenance 的五项 hash 全部保持。没有 push 或远程发布。

## 九项验收状态

每项实际输入、对应构建 SHA256、检查结果及证据路径见 [机器摘要](evidence/udf201-windows-return-20261012.json) 的 `criteria`/`checks`/hash maps。旧实测保留原构建，不用本轮离线成功重写旧记录。

| 判据 | 当前状态 / 输入 |
| --- | --- |
| CURRENT_8SESSION_READ_PASS | **NOT PASS**：原损伤盘一个文件有不可纠正介质错误；替换盘不回填 |
| CROSS_SESSION_REFERENCE_PASS | 原盘 Anchor→Session 6 VDS 元数据、独立八 Session fixture、当前返回盘 Session 3→Session 2 VDS→Session 1 分区/旧内容实际读取通过；Windows FID 偏差另标 |
| SINGLE_SESSION_REGRESSION_PASS | 替换盘写前真实全文件基线和独立单 Session fixture 通过；原辅助盘身份未复验 |
| VAT_OFFLINE_APPEND_PASS | **PASS OFFLINE**：71 项和真实返回视图生成结果的独立严格验证；未烧录 |
| EXISTING_FEATURE_REGRESSION_PASS | **PASS CONTROLLED**：原六组、ASan/UBSan 与冲突/短卷/导出通过；历史不可重现实盘场景不回填 |
| GUI_INTEGRATION_PASS | **PASS**：146 项受控检查 + 最终适配器三文件实际导出/独立复读；原生窗口写盘未执行 |
| MAC_PHYSICAL_APPEND_PASS | 保留前阶段 **vat8 数据 + 单独授权 media6 完成会话 + 正常全文件读回通过**；vat9 正常 burn 完整物理复验待执行 |
| WINDOWS_READ_AND_REAPPEND_PASS | **人工确认 + Mac 实际返回内容证明**；Windows 11 25H2 资源管理器；自动报告与源 hash 比对未采集 |
| MAC_RETURN_READBACK_PASS | **PASS WITH AUTHORIZED WINDOWS COMPATIBILITY**：最新正常 CLI / 独立 oracle / 实际 GUI 三文件导出；`strictConformance=false` |

本轮只读与本地生成，没有物理写入，盘仍可追加。物理锁、退出收据和 Disk Arbitration guard 正常释放；最终 daemon 正常运行、无活动或未知光盘 worker。新增 vat9 实测如需写盘，必须单独明确授权具体目标和文件，不能复用已消耗的原追加/会话完成授权。

## 修复后正常 burn 的待授权实测

已用正常物理 `build` 重新识别当前盘并生成只读预览，退出 0。318 个新读取 metadata 块与此前全文件真实读取基线逐块一致；独立复合视图再次验证新目录严格正确、三个旧 payload 未复制，hash/extent/UniqueID 保持。`build` 本身显示旧 hash 未重新读取；完整 hash 证据来自前述实际 CLI/GUI 两次读取，`burn` 写前仍须全新复读它们。

| 可审核项目 | 本次预览 |
| --- | --- |
| 目标盘 | Lenovo XL6M，CD-R `10 10 2026`，device `1,0,0`，registry `4295074771`，盘 ID `0x380500` |
| 当前完成状态 | 3 Sessions / 3 Tracks，无 unfinished，NWA 26434，剩余 333409 blocks |
| 唯一新增文件 | `burnlight-vat-post-windows-offline.txt`，46 bytes |
| 文件 SHA256 | `d839c5541edb618dc95092caed380bc30829b670d7ee5ca7605e0f506ed28928` |
| 冲突选择 | 默认拒绝；当前名称不存在 |
| 镜像 | 576 blocks / 1179648 bytes；`normal-vat9-physical-preview/session-next.iso` |
| 预览镜像 SHA256 | `f55d5960314c2f93fd998d9b8dd070d847728df3a70b9d578a61c135cd1174d2` |
| 旧文件基线 | 上表全部三文件，真实 hash 和 payload LBA 327 / 12046 / 19496 |
| 使用后端 | 独立 vat9 `b192be9c…`；保持 multi，可追加，不封盘 |

授权后才执行一次正常 CLI `burn`：重新绑定设备/盘型/最新视图/NWA/容量，生成并独立核对本次实际镜像，写前复读全部旧 metadata/hash；写入后重新探测真实 Session/Track/VAT，读回整个新镜像及所有新旧文件、检查旧 payload 地址，并做独立解析。实际生成时描述符时间戳和相应 CRC 可能改变，实际镜像另行记录 hash；不能把预览 hash 当成实际写入 hash。失败立即停止，不重烧、不自动 finish/init/recover。此预览没有消耗光盘空间，不属于物理 PASS；具体输入和镜像绑定见 `normal-vat9-physical-preview.json`。
