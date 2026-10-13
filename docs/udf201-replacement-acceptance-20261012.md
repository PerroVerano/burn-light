# UDF 2.01/VAT 换盘后的只读验收与写入预览（2026-10-12）

此文保存授权前的只读阶段结果。随后用户授权了追加，实际新旧内容验证通过，但会话未完成；修复和待授权操作见 [后续实盘记录](udf201-mac-attempt-20261012.md)。以下 hash 与状态仍绑定本阶段输入，不回填后续 PASS。

用户确认原八 Session 盘物理损伤，并换入另一张同格式盘。本次新盘的完整只读导入、macOS 挂载比对和 GUI 导出适配器实盘复读全部通过，专用追加镜像也通过核心及独立 Python 验证。**本次未写盘，Mac 物理追加及 Windows 往返仍待授权和验收。**

这是新样本，不能把它的成功回填为原八 Session 全文件读取通过。原盘的跨 Session 元数据取证、不可纠正扇区和未完成边界保留在 [原验收记录](udf201-acceptance-20261012.md)。换盘后没有沿用原盘地址、缓存或预览镜像，也没有要求更换同型号光驱。

## 实时目标与完整旧文件基线

独立证据目录：`artifacts/udf201-acceptance/20261012/replacement-095949/`。CLI 为 `build-cli/appendcd`，SHA256 为 `a8adab13d942a3f40b8eacba3071efd481248fa7b0075c70df26d8b13e694907`，与已通过七组 CTest、ASan/UBSan 和 GUI 回归的源码绑定相同。

| 项目 | 新盘实际结果 |
| --- | --- |
| 光驱 | Lenovo Ultraslim DVD XL6M；`1,0,0`；registry `4295064341` |
| 介质与卷名 | CD-R / Mode 2 Form 1；`10 10 2026` |
| 完成 Session / Track | 1 / 1；没有 unfinished Session；appendable |
| 已记录 Track | `[0, 352)`，不把 TOC 的额外尾部地址算作数据 |
| 最新 Session / NWA | 0 / 11754 |
| 剩余可写 | 348089 blocks / 712886272 bytes |
| 格式 | NSR03；UDF 0x0201；Tag Version 3 |
| 分区 | start 288；capacity 359552；Number 8192；Access Type 2 |
| 映射与入口 | type-1 + type-2 Virtual；FSD Reference 1 / Block 0 |
| Anchor / 主 VDS / 备用 VDS | 256 / 32 / 64 |
| 最新 / previous VAT ICB | 351 / 319，均在同一个物理 Session |
| 最新 FSD / root / 文件 ICB | 334 / 335 / 336 |

权威视图 identity：`cbdf89f4cac120b35933c26407988057debd18be4aabb3dbf4c9772ce7e326eb`。最新 VAT header 184 bytes、implementation use 32 bytes、422 entries，minimum read/write 及 maximum write revision 均为 0x0201。声明的分区容量延伸到未来空白区；实际读取仍按完成 Track 和有效缓存块限制。

当前只有一个根目录普通文件 `20261010更新步骤.docx`，14336 bytes，实际 payload 为 LBA 327 + 14336 bytes，SHA256 为 `7e0e6e9e44fca400aa9c4e7a4cf27b171a6046d8acac0ea88e5260414427dc39`。

`physical-read.log` 和 `run-YAQWzS/read-baseline.manifest` 记录实际光学读取；`mounted-comparison.json` 记录 `/Volumes/10 10 2026` 的名称、层级、类型、大小和全部文件 SHA256，均与光学基线一致。独立 Python oracle 另解析同一有效缓存并核对实际文件 hash，结果在 `independent-source-read.json`。

GUI 包 `build-udf201-gui/output/BurnLight.app` 的导出适配器绑定同一设备身份和 VAT view，先导出文件，再用独立新缓存复读全部元数据及 payload。`physical-export.log`、`physical-export/export.json` 和两份 `.blocks`/`.tracks` 记录 `OPTICAL_EXPORT_VERIFIED`，导出文件 hash 与光学基线、macOS 挂载文件一致。这是实际 GUI 适配器读取/导出验证；没有执行 GUI 物理写入。适配器和包内 CLI 的 hash 见 `export-binding.json`。

取证后 Disk Arbitration 运行，无遗留 readcd、drutil 或光盘 worker，见 `services-final.txt`。没有修改系统安全设置、硬杀进程或故障注入。

## 可审核的追加预览

目标为以上新盘，默认冲突拒绝，保留卷名和 UDF 2.01/VAT 格式。唯一新增文件：

| 名称 | bytes | SHA256 |
| --- | ---: | --- |
| `burnlight-vat-mac-20261012.txt` | 56 | `faaabcbc28a26670d4b26b2a47b350d98d8a03c7778e22274931e636c6778b37` |

内容为 `BurnLight UDF 2.01 VAT Mac append acceptance 2026-10-12` 加一个换行。文件在 `preview/files/`，没有同名冲突。

`preview/session-next.iso` 为 576 blocks / 1179648 bytes，当前预览 start 11754，SHA256 为 `6eb310de4694be73dd32c9e615c358ba1dd845ddc7f12882528e164a2cf7d505`。新主/备用 VDS 为 11786 / 11850，新 VAT ICB 为 12329，previous 指向原 VAT 351。新文件 payload 为 LBA 12046 + 56 bytes，原 docx 的 ICB、payload LBA 327、大小和 hash 保持；旧 payload 没有写入新镜像。

生成器原 manifest 另存为 `session-next.generator.manifest`。仅在确认原文件名称、大小和每个 section 完全相同后，从本次实际 `read-baseline.manifest` 填入旧文件 SHA256；未修改镜像内容。核心 `verify` 记录 new files=1、old files=1、old hashes verified=1、unverified=0。独立 Python oracle 对按有效块/Track 索引组成的复合视图检查 tag、CRC、地址、文件内容、旧 extent、previous VAT、LVID 及最后 VAT 扇区，结果见 `preview/independent-verification.json`。

镜像成功不能代替实际 CD-R packet/link/padding 行为验证。授权后应重新扫描并重新生成，绑定设备、介质、NWA 和此 VAT view；再次从真实盘复读全部旧内容及元数据，任何变化即停止。随后只追加以上文件，用新的实际扇区缓存验证新旧文件 hash、旧地址、最新 VAT、实际 Track 数据尾、NWA、可追加状态及服务恢复。不得复用旧预览直接写入、自动重烧、init、erase、封盘或 recover。

## 验收状态与下一步

| 完成判据 | 当前状态 |
| --- | --- |
| CURRENT_8SESSION_READ_PASS | 原损伤盘未通过全文件读取；本次单 Session 新盘不能替代 |
| CROSS_SESSION_REFERENCE_PASS | 原八 Session 的实际跨引用元数据及独立 fixture PASS；本次单 Session 不新增此项实盘证明 |
| SINGLE_SESSION_REGRESSION_PASS | 独立 fixture + 本次完整实盘读取、挂载比对 PASS |
| VAT_OFFLINE_APPEND_PASS | 原连续追加专项 + 本次完整旧 hash 预览、独立解析 PASS |
| EXISTING_FEATURE_REGRESSION_PASS | 已绑定相同源码的原六组及专项、GUI、ASan/UBSan PASS；未重做历史物理写入 |
| GUI_INTEGRATION_PASS | 142 条受控断言、包内回归与 VAT 离线导出 + 本次实盘适配器导出 PASS；GUI 写盘待验收 |
| MAC_PHYSICAL_APPEND_PASS | 待明确授权；基线及镜像已具备，无物理写入 |
| WINDOWS_READ_AND_REAPPEND_PASS | 待用户 Windows 读取及按原方式追加，并收集人工确认/自动 hash、IOCTL、版本和方式 |
| MAC_RETURN_READBACK_PASS | 待 Windows 操作后返回 Mac，重新采集最新 Session/Track/VAT 和全文件 hash/extent |

所有本次输入、构建 hash、证据 hash、结果及原回归引用见 [机器可读摘要](evidence/udf201-replacement-20261012.json)。当前完整功能仍未验收完成，原八 Session 样本边界和替换盘的后续闭环分别记录。
