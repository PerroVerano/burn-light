# UDF 2.01/VAT Mac 实盘追加、失败取证与待授权操作（2026-10-12）

此文保留失败和待授权阶段的状态。随后用户单独授权完成已有 Session，正常实际读回与导出通过；当前结果见 [会话完成记录](udf201-session-completion-20261012.md)。以下历史输入/hash/PASS 边界不被改写。

用户已明确授权在替换盘「10 10 2026」追加 `burnlight-vat-mac-20261012.txt`（56 bytes）并实际读回。一次实际写入已经发生；**新旧数据及 VAT 的实际取证验证通过，但 Session 2 未完成，MAC_PHYSICAL_APPEND_PASS 未通过，Windows 往返没有开始。** 未自动重烧、结束失败会话、封盘或 recovery。

这是单 Session 替换盘的后续操作，不能代替原八 Session 损伤盘的完整读取或闭环。前置完整基线及授权前预览见 [换盘只读记录](udf201-replacement-acceptance-20261012.md)。原盘记录和以前的物理验收保持独立。

## 实际写入与盘片当前状态

证据目录为 `artifacts/udf201-acceptance/20261012/replacement-095949/`。`write-authorization.json` 保存人类授权范围。写前再次实际读取旧 docx，确认名称、14336-byte 大小、SHA256 和 LBA 327 均与原基线一致；设备 registry `4295064341`、盘 ID `0x380500`、NWA 11754 和原 VAT view 未变。

辅助脚本的 Documents 权限错误及短暂介质节点缺失均发生在 writer 启动前，没有写入。第一次 vat8 writer 在 IOKit 插件创建阶段失败，也未获得 SCSI 接口；随后只读比较证明同一 binary hash 的临时目录副本可工作、原构建路径不能工作，GUI 包副本也可工作。没有将 `0xe00002c7` 武断归为 TCC 原因，没有改 SIP/TCC 或系统安全设置。详见 `backend-readonly-comparison.json`。

真正写入使用 CLI SHA256 `b9a8777b3bbfc8d10335dfba9757c2577a89f3fd1b4a705ecae29955e4aa6065`、临时隔离 vat8 后端 SHA256 `4b349604776697c97893a3cf01feb08c7bee5e2887ef53bd7e820e4cd9e628bf`，均独立于受保护的 media6/initial7。七组 CTest 在这个 runtime 绑定构建上通过。

`physical-burn-runtime.log` 与 `physical-burn-runtime/run-37xDnX/cdrecord-burn.txt` 记录一次真实 PACKET 写入：1179648 bytes / 576 sectors，起点 11754，后端 exit 0。写前重新生成的实际镜像 SHA256 为 `1c2b48079ea8f6d2da43cfb1c61fd5b20ba1c7c5f82ba0f56c977ee642983bd1`；与早先预览的区别仅在 LVID/FSD/新 FE 的时间字段及相应 tag checksum/CRC，payload、extent 和 VAT 布局保持。实际写入之后没有再次写 payload。

| 当前真实物理字段 | 结果 |
| --- | --- |
| 光驱 / 介质 / 卷 | Lenovo Ultraslim DVD XL6M / CD-R / `10 10 2026` |
| 完成 Session | drutil 为 1；minfo 的 Session 2 **incomplete/appendable** |
| Track 1 | Session 1 Data `[0,352)` |
| Track 2 | Session 2 Data `[11754,12330)`，576 blocks，已记录 |
| Track 3 | Session 2 Blank，下一可写区域；不是完成的第三 Track/Session |
| TOC | 仍只列完成 Session 内的 Track 1 |
| NWA / minfo 剩余 | 12557 / 347286 blocks |
| drutil Track 数 | 2；不能据此称两个完成 Session |

原始 `postwrite-raw/{drutil-status,minfo,toc,msinfo}.txt` 绑定实际字段。正常 CLI 的只读导入被会话边界 guard 拒绝，见 `postwrite-normal-read-rejected.log`。没有使用旧目录或旧缓存来伪造当前正常读取 PASS。

## 已写内容的真实只读取证

此驱动 READ CAPACITY 仍报告旧完成 Session 的 352 blocks，普通 readcd 将显式读取范围裁到该容量，生成了零长度输出；程序正确拒绝了 size mismatch，没有把缺失块当作零。

为核对已经写入的数据，使用固定上游 cdrtools 源码的**独立、只读取证副本**：写模式、交互菜单、clone/noerror 等在设备访问前拒绝；读取范围由即时 minfo 的真实 Data Track 区间限定，IOKit 接口再次核对 registry ID。它仅允许明确传入的已记录末端越过旧 READ CAPACITY，未把未来 Blank Track 或 Session 间隙变成可读数据。普通 CLI/GUI、Homebrew readcd 和全局工具链没有改成这一模式。

来源 tar SHA256、CDDL 许可证、工具 hash、源码补丁、构建日志分别在 `forensic-readcd-provenance.json`、`forensic-readcd.patch` 和 `forensic-readcd-build.log`；实际有效块与 Track 索引在 `incomplete-session-forensic-read/actual-read/`。源码、helper 和证据均为本地取证用途，不把此视图当作完成 Session 验收。

所有 576 个实际新扇区的 hash 与实际烧录镜像一致。另从真实光盘读取旧 docx 的 payload；两文件 hash 和实际地址如下：

| 文件 | bytes | 实际 payload | 实际 SHA256 |
| --- | ---: | --- | --- |
| `20261010更新步骤.docx` | 14336 | 原 LBA 327 + 14336 bytes | `7e0e6e9e44fca400aa9c4e7a4cf27b171a6046d8acac0ea88e5260414427dc39` |
| `burnlight-vat-mac-20261012.txt` | 56 | LBA 12046 + 56 bytes | `faaabcbc28a26670d4b26b2a47b350d98d8a03c7778e22274931e636c6778b37` |

最新 Anchor 12010 → 新主 VDS 11786 / 备用 11850，物理分区仍为 288；最新 VAT ICB 12329 → previous VAT 351 → 319。VAT ICB 恰是 Track 2 最后有效数据扇区；两份 Anchor 和 VAT 布局通过独立 Python oracle，旧 extent/hash 保持。权威取证 view 为 `6c92bf278af8b439a7703040a252042b37e3fb50d8a7a0015c1f7fad50d9b5b1`。

核心验证和独立解析均通过，结果在 `incomplete-session-forensic-read.log`、`independent-physical-forensic.json`。其最终标志明确是 `INCOMPLETE_SESSION_FORENSIC_READ_PASS; NOT_PHYSICAL_APPEND_PASS`。这不能替代完成会话后的正常 CLI/GUI 读取、macOS 挂载或 Windows 读取验收。

## 实现缺陷与修复回归

上游 `getargs()` 为 packet 数据 Track 设置 `TI_PACKET`，同时清除 lead-in 的 `TI_TAO`，却没有给 lead-in 设置 `TI_PACKET`。`fixate_mmc()` 收到 lead-in，原判断因此只执行 cache flush，没有发出 CLOSE SESSION。这是本次新后端的实现缺陷；离线镜像测试没有发现，真实盘检查正确拦住了 PASS。

vat9 增加仅在 VAT executor 生效的 packet fixation 选择，从数据 Track 的实际标志识别 packet 会话，保留原非 VAT 分支选择。`backend_target` 新增实际 lead-in/data flag 组合回归。CLI 与 GUI 打包都拒绝旧 vat8；退出信息改成“数据已写入，尚未确认完成 Session”，不会称作“written/closed”。没有给 UDF 2.01 增加自动 recover/recover-finish。

修复后的七组 CTest、ASan/UBSan 同七组、GUI 142 条断言、搬移包七组、VAT 导出 4 项、冲突 50 项、短卷 8 项和既有导出 10 项均通过。leak detection 未执行/未算 PASS。新 GUI 为 `build-udf201-gui-v9/output/BurnLight.app`，未安装、测试包不允许物理写入。vat9 正常追加及 Session 结束仍缺少一次完整物理执行，不能用受控断言回填。

## 可审核的待授权会话完成方案

`session-close-plan/` 和加强设备/工具绑定的 `session-close-v2-plan/` 均是 **plan-only**：再次读取并核对真实新镜像、两文件 hash、盘 ID、NWA 12557 及未完成 Session 状态，未发关闭/写入命令。后者核对精确 registry ID、两 Track 区间和 backend/readcd hash，共用现有 GUI 物理锁，保留未知 worker 标记和明确退出收据；具体绑定在 `session-close-v2-plan-binding.json`。

拟执行的操作仅结束已有 Session 2，保持光盘可追加，不再写文件或新 payload。使用未修改的现有 media6 标准 `-fix -multi` 会话结束路径；执行前必须再核对同一设备身份、盘 ID、两 Data Track 区间、完整真实内容 hash 和上述未完成状态。变化或失败即停止，不重试。该操作会写会话结束的 leadout/TOC 区域，消耗相应 CD-R 空间，最终 NWA 以实测为准。

执行 helper 必须显式传入 `--execute`；目前没有执行。之后用修复后的正常 CLI `readback` 和一份新缓存，验证实际完成 Session、所有新旧 hash/extent、VAT 尾部、可追加状态及系统服务恢复，再与 macOS 挂载比对。然后才进入 Windows 人工/自动读取、按原方式追加和 Mac 返回读回。

**原授权是一项正常追加及读回。失败后的额外会话完成操作受原 prompt“不要自动…recover 或 recover-finish”等约束，不能自行执行，需要用户针对当前状态另行明确授权。** 此方案是本次实盘的有界验收操作，不是 GUI/CLI 新增的通用恢复功能。

| 判据 | 当前状态 |
| --- | --- |
| CURRENT_8SESSION_READ_PASS | 原损伤盘全文件读取未通过，保持独立 |
| CROSS_SESSION_REFERENCE_PASS | 原八 Session 实际跨引用元数据 + 独立 fixture PASS |
| SINGLE_SESSION_REGRESSION_PASS | 替换盘写前完整读取、挂载 hash 比对 PASS |
| VAT_OFFLINE_APPEND_PASS | 修复后离线及独立验证 PASS |
| EXISTING_FEATURE_REGRESSION_PASS | 修复后原六组及 sanitizer/GUI 离线回归 PASS |
| GUI_INTEGRATION_PASS | 修复后受控 GUI/导出 PASS；写前实盘适配器导出 PASS；GUI 写盘未执行 |
| MAC_PHYSICAL_APPEND_PASS | **未通过**：已写内容真实取证 PASS，Session 2 未完成；额外完成操作未授权 |
| WINDOWS_READ_AND_REAPPEND_PASS | 待验收，尚未执行 |
| MAC_RETURN_READBACK_PASS | 待验收，尚未执行 |

各阶段输入、不同 CLI/backend hash、最终源码与构建绑定、证据 hash、回归结果和保护对象对照见 [机器可读摘要](evidence/udf201-mac-attempt-20261012.json)。完整功能仍未验收完成。
