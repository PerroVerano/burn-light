# UDF 2.01/VAT：修复后正常追加实盘验收（2026-10-12）

**修复后的 vat9 正常 burn 全流程实际通过。** 在 Windows 再追加后的同一替换 CD-R 上，按用户授权追加唯一 46-byte 文本，从 Windows 兼容来源生成正确的新目录，自动正常完成本次 Session，并实际比对全部 576 个新扇区和四个文件。独立默认严格 oracle 解析最新视图通过，三个旧 payload 的实际地址、hash 和 UniqueID 保持，旧 payload 未复制。

Mac 写后实际状态为 **4 个完成 Session / 4 Tracks，appendable，无 unfinished Session**，NWA 33912，剩余 325931 blocks / 667506688 bytes。Windows 11 25H2 已人工确认此前 Mac 文件并追加 JSON，Mac 返回读取已通过；用户随后确认 **最新第 4 Session 的四文件均可见且能打开，包括本次 46-byte 文本**。因此替换盘 Mac→Windows→Mac 追加读取闭环及修复后正常 vat9→Windows 读取均实际成立。Windows 自动 hash / IOCTL 报告未采集，不能将人工确认写成自动比对 PASS。原损伤八 Session 盘全文件读取仍未通过，不能据本替换盘回填。历史阶段分别保留：[初始八 Session](udf201-acceptance-20261012.md)、[替换盘基线](udf201-replacement-acceptance-20261012.md)、[vat8 失败](udf201-mac-attempt-20261012.md)、[单独授权完成已有会话](udf201-session-completion-20261012.md)、[Windows 返回盘与有限兼容](udf201-windows-return-20261012.md)。

## 授权、预检失败与修复

用户明确回复“授权此文件追加并读回”，目标 Lenovo XL6M、CD-R `10 10 2026`、device `1,0,0`、3 个完成 Session、NWA 26434；唯一文件 `burnlight-vat-post-windows-offline.txt`，46 bytes，SHA256 `d839c5541edb618dc95092caed380bc30829b670d7ee5ca7605e0f506ed28928`，默认冲突拒绝。只读预览 576 blocks / 1179648 bytes，三个旧文件的实际完整基线和独立复合镜像均通过。证据：`artifacts/udf201-acceptance/20261012/vat9-normal-burn-141004/authorization.json`。

第一次命令在复核三个旧 hash 后，因原生 IOMedia 尚未重新发布而在 burn 的首次 preflight 拒绝，退出 1。没有生成 burn 镜像、调用写入后端或写盘；失败命令/收据保留为 `execution/` 和 `failure-assessment.json`。随后增加 `waitForOpticalMedia`：已有 BSD 节点立即继续；暂缺时最多 40 次 / 10 秒，只重查已选光驱的 registry ID、原生地址、厂商/型号/固件和 registry path，变化或消失即拒绝，超时明确失败，并响应中断。等待用于扫描预检时的原生介质发布，包含写前和写后重新探测；**不重试 writer**，也不推测 BSD 节点或 LBA。

新增确定性离线测试覆盖延迟发布、已可用不轮询、超时、同地址换服务、路径变化、目标消失和资源上限。真实连续 `list --hash-files` → `inspect` 复验触发等待且通过，仍为 3 个完成 Session / 原 NWA / 原最新 VAT / 三个原 hash。原失败从未进入实际写入，同一目标/文件的一次追加授权尚未消耗；完成修复和回归后执行这一次实际写入。整个阶段**写入后端仅启动一次、writer 重试零次**，没有额外 fix、recover、init 或封盘。两个验收包装错误（Python 路径重复后缀、只读 GUI 任务遗漏工作目录）也在真实设备操作前拒绝，原错误和纠正记录保留，不算产品或物理成功。

## 实際盘布局与内容

重新发现 device `1,0,0`、registry `4295074771`、whole-disc `/dev/disk4`、盘 ID `0x380500`，写前实际 metadata 与全部旧 hash 复核通过。调用正常 CLI `burn`、`--expected-view d8900ba6…`，由核心再次生成/验证镜像、复读全部旧内容、复查介质/NWA/容量、调用隔离 vat9 的 variable packet / `pktsize=0` / Mode 2 Form 1 / `nopad` / `multi`。

后端实际 WRITE 起点 26434，读/写 1179648 / 1179648 bytes（576 sectors），fixation 完成、退出 0。正常 CLI 新探测确认第 4 Session 完成，再全新读回新 Track 和全部旧文件，退出 0。**不再依赖前次 vat8 的手动完成流程。**

| 已记录范围，end-exclusive | Session / Track |
| --- | --- |
| `[0,352)` | 1 / 1 |
| `[11754,12330)` | 2 / 2 |
| `[19232,19532)` | 3 / 3 |
| `[26434,27010)` | 4 / 4 |

最新入口 Anchor 26690 → main VDS 26466 / reserve 26530（本次新结构），物理分区仍从 Session 1 的 288 起，Number 8192，physical map 0 / Virtual map 1。最新 FSD 26722、root virtual 466 → ICB 26729，最新 VAT ICB **27009 → 19531 → 12329 → 351 → 319**。诊断和独立 oracle 验证其 Tag/CRC/地址空间、VAT 计数、previous 引用、新目录及旧内容。

Track 最后有效数据块为 27009，即最新 VAT ICB；minfo 数据 end 为 27010。TOC 的 leadout 地址 27012 及后端 total-size 578 sectors 包含额外两块，并没有写成 578-block payload，也未将其作为可读文件数据。576 个实际数据块与本次实际镜像逐字节相等，验证了本次 `nopad` 写入的最终有效 VAT 位置。

| 文件 | bytes | 实际 payload LBA | SHA256 |
| --- | ---: | ---: | --- |
| `20261010更新步骤.docx` | 14336 | 327，保持 | `7e0e6e9e44fca400aa9c4e7a4cf27b171a6046d8acac0ea88e5260414427dc39` |
| `MybatisCodeHelperConfig.json` | 11266 | 19496，保持 | `80b979156dc47c0a217974d2e8fcfa25c135a3c2596d2e14801ee2d144240a61` |
| `burnlight-vat-mac-20261012.txt` | 56 | 12046，保持 | `faaabcbc28a26670d4b26b2a47b350d98d8a03c7778e22274931e636c6778b37` |
| `burnlight-vat-post-windows-offline.txt` | 46 | 26728，新文件 | `d839c5541edb618dc95092caed380bc30829b670d7ee5ca7605e0f506ed28928` |

实际写入镜像 SHA256：`d70f7fd55e6a3058835577b48e08ab37ac91eace5398e23c83d6620703132de1`；预览 hash `f55d5960…` 单独保留，不作为实际镜像。核心和独立解析器都验证本次实际镜像 / 光盘；没有用 writer 预期字段作唯一证据。

最新视图 identity：`aa0c0ca213be264bf466433f1164f1526ee70648c0f341292bdbb38b9796ed30`。Windows 第 3 Session 来源使用明确的 `windows-fid-location-v1`；本次生成的新目录所有位置字段正确，**最新视图不需要兼容分支**，独立默认严格 oracle 为 `strictConformance=true`。这仅说明已验证的当前受支持结构，不宣称历史 Windows 目录全盘规范符合。

兼容规则中的历史表指 latest VAT 的 **previous 链**，不是扫描所有曾写下的事务再合并。第 3 Session 的 19519 是未链接到最新 previous 链的较早事务；最新 19531 明确指向 12329，诊断保留两者区别，当前目录由最新表决定。

## 独立读取与 GUI 交付

`independent-normal-burn-readback.json` 绑定实际 after-read cache、CLI / 后端 / 独立 oracle hash，证明全部新扇区相等、四文件内容、旧地址/UniqueID 与未复制旧 payload。macOS 挂载 `disk4s0` 经实时 diskutil 确认为同一 whole-disc 的 UDF，全部四文件名称/大小/hash 一致：`final-mounted-comparison.json`。

最终 GUI 只读适配器另做源读取和全新 verification 读取，绑定新设备身份和当前 VAT view；完整提取四文件，两份实际缓存由独立默认严格 oracle 验证，导出收据无 Windows 兼容标记。验收记录为 `independent-final-gui-export.json`。此为实际适配器导出和真实读回，原生窗口点击写入未执行。

最终产物为 `build-cli/appendcd`、`build-udf201-gui-publication/output/BurnLight.app`（只读）和 `build-udf201-gui-publication-write/output/BurnLight.app`（启用确认写入）；可写包同目录附 zip。独立构建、系统认证和八项工具/库信任 hash 保持；未安装、不覆盖旧包和三个基准 runtime，不修改全局工具链。首次刻录继续 ISO9660/Joliet，VAT 不纳入 DVD recovery。

最终 7/7 CTest、71 项 VAT、7/7 ASan/UBSan、146 条 GUI 断言、bundle 内原七组、50 项冲突、8 项短卷、10 项原导出、5 项 VAT 导出通过；wait 的七项边界纳入 drive_selection。ASan `detect_leaks=0`，不报告 leak detection PASS。全部绑定见 [机器摘要](evidence/udf201-vat9-normal-burn-20261012.json)。

## 九项判据与剩余实测

| 判据 | 当前状态 |
| --- | --- |
| CURRENT_8SESSION_READ_PASS | 原损伤盘全文件读取 **NOT PASS**，不能用替换盘回填 |
| CROSS_SESSION_REFERENCE_PASS | 原盘 Anchor→Session 6 VDS 元数据、独立八 Session 与当前替换盘旧分区/旧数据跨 Session 实读通过；第 3 Session FID 偏差明确另标 |
| SINGLE_SESSION_REGRESSION_PASS | 替换盘原单 Session 完整物理基线与独立 fixture 通过；原辅助盘未按原身份复验 |
| VAT_OFFLINE_APPEND_PASS | 71 项及真实 Windows 来源的独立严格新目录/旧地址验证通过 |
| EXISTING_FEATURE_REGRESSION_PASS | 最终原六组与 GUI/ASan/UBSan 回归受控通过，缺失历史物理场景不回填 |
| GUI_INTEGRATION_PASS | 146 项 + 最终四文件实际适配器导出/两次严格独立实读通过；原生窗口写盘未执行 |
| MAC_PHYSICAL_APPEND_PASS | **PASS NORMAL VAT9 BURN**：本次一次后端调用、Session 完成、全镜像/四文件实读、旧地址保持 |
| WINDOWS_READ_AND_REAPPEND_PASS | Windows 11 25H2 人工读取/打开及资源管理器再追加 + Mac 实際返回证明；最新 Session 4 的四文件也已人工确认均可见且能打开；自动报告、JSON 源 hash 未采集 |
| MAC_RETURN_READBACK_PASS | 第 3 Session Windows 返回盘实际导入/导出 **PASS WITH AUTHORIZED COMPATIBILITY**；随后第 4 Session 是 Mac 追加后的严格实读，不冒充新的 Windows 返回 |

最新 Windows 人工确认直接回复为“**四个文件均可见且能打开**”，针对明确列出的当前四文件，记录为 `windows-session4-human-confirmation.json`。没有报告新的 Windows 再追加，不能推断物理 Session、NWA 或 VAT 随这次只读确认发生变化；上表物理状态来自 Mac 写后的实际探测。物理读回证据中的 Windows 未确认标记反映其生成时间，最终机器摘要单独绑定随后人工确认。等待 Windows 前的报告、机器摘要、文档及本地包已另存 `before-windows-session4-confirmation/`，历史阶段不覆盖。

Mac 最终检查时盘仍可追加，没有 unfinished Session，无活动或未知光盘 worker，物理锁/退出收据正常释放，Disk Arbitration 正常恢复。[只读 Windows 工具包](../build-udf201-gui-publication-write/output/BurnLight-vat9-session4-Windows-readonly-kit.zip)包含最新预期清单和项目 PowerShell 脚本，可选补充自动 hash / IOCTL，不含用户 payload。使用前按包内说明核对真实光驱盘符；脚本尚未在 Windows 执行，不能算自动 PASS。替换盘实测闭环已通过，不要求再次写文件。完整原八 Session 判据因原盘损伤仍无法完成，所有边界保留。
