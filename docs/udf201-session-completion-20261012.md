# UDF 2.01/VAT 已写 Session 完成与正常读回（2026-10-12）

用户单独授权“完成现有 Session 并读回”后，已结束替换盘上此前写入的 Session 2。**正常 CLI 全新实际读回、独立解析、macOS 挂载比对及最终 GUI 导出适配器的两次独立实际读取均通过。光盘现为 2 个完成 Session / 2 条已记录 Track，仍可追加，无 unfinished Session。** Windows 读取、再次追加及 Mac 返回读回尚未执行。

本次只完成已有会话，没有再次写文件、生成新数据 Track、封盘或重试。此前数据写入使用 vat8，发现漏结束 Session 后已修复为 vat9；本次结束由未修改的 baseline media6 `-fix -multi` 完成，正常读回使用最新 vat9 绑定 CLI。这个组合的实盘结果不能替代修复后 vat9 正常 `burn` 全流程的独立物理复验，也不回填原损伤八 Session 盘的全文件读取。

## 授权、执行和实时状态

证据目录：`artifacts/udf201-acceptance/20261012/replacement-095949/`。授权在 `session-close-authorization.json`；已审核的 helper/source hash 和 plan-only 结果仍保留。执行前重新实际读取完整新 Track 和两个文件，精确核对 registry ID、盘 ID、两 Track 区间、未完成会话、NWA 12557、实际镜像 hash 及 backend/readcd hash；内容读取结束后再次 minfo，状态完全一致才执行一次关闭。

helper 共用现有物理锁，未知 worker 标记拒绝准入；使用设备绑定和原 Disk Arbitration guard。`session-close-v2-execution/session-close.txt` 记录 baseline media6 退出 0，fixation 耗时 12.722 秒。明确退出收据保存后释放标记；没有自动第二次关闭。

| 当前字段 | 实测 |
| --- | --- |
| 光驱 / 介质 / 卷 | Lenovo Ultraslim DVD XL6M / CD-R / `10 10 2026` |
| device / registry / 盘 ID | `1,0,0` / `4295064341` / `0x380500` |
| 完成 Session / 已记录 Track | 2 / 2 |
| Session 1 / Track 1 数据 | `[0,352)` |
| Session 2 / Track 2 数据 | `[11754,12330)`，576 blocks |
| 最新完成 Session 起点 | 11754 |
| 下一可写地址 / 剩余 | 19232 / 340611 blocks（697571328 bytes） |
| 可追加 / unfinished | yes / no |

minfo 的 `number of sessions: 3` 和 Blank Track 3 是下一空白会话，不是已刻录第三次。minfo 的数据末端/leadout 字段为 12330，TOC leadout 为 12332；额外尾部地址不计入实际文件数据区间，未将这些地址当作可导入块或 VAT。最新 VAT ICB 12329 仍是最后有效数据扇区，完整 576-sector 实际镜像 hash 未变。

## 正常读取、独立解析和导出

正常 CLI SHA256：`e9a9e0d1900eba188aadba6137201b65fb95dc998250e41157a04095563f7f3e`。正常 `readback` 使用全新缓存，输出 `PHYSICAL READBACK PASS` 和 `old hashes verified=1 (unverified=0)`；证据在 `completed-normal-readback/optical/run-FhOBBX/`。没有使用未完成会话的取证 reader 来代替这次正常读取。

读回启动脚本曾因错误 Python 路径退出 127，改正后 root Python 又在读取 Documents 中的 CLI 时遭 EPERM；两次均发生在 CLI/光盘访问前。随后由用户进程把同一已验证 hash 的最终 CLI 复制到私有临时 runtime，成功启动正常读回。相关失败、运行副本绑定均保留，未改 SIP/TCC、安装应用、原 runtime 或全局工具链。

实际新镜像 SHA256：`1c2b48079ea8f6d2da43cfb1c61fd5b20ba1c7c5f82ba0f56c977ee642983bd1`。当前 view：`6c92bf278af8b439a7703040a252042b37e3fb50d8a7a0015c1f7fad50d9b5b1`。

| 当前引用 | 绝对 LBA | 所属 Track / Session |
| --- | ---: | --- |
| 最新入口 Anchor | 12010 | 2 / 2 |
| 新主 / 备用 VDS | 11786 / 11850 | 2 / 2 |
| 保持的物理分区起点 | 288 | 1 / 1 |
| 最新 VAT → previous | 12329 → 351 → 319 | 2 / 2 → 1 / 1 → 1 / 1 |
| 当前 FSD / root ICB | 12042 / 12047 | 2 / 2 |
| 旧 docx ICB / 新文件 ICB | 336 / 12048 | 1 / 1、2 / 2 |

| 文件 | bytes | 实际 payload | 实际 SHA256 |
| --- | ---: | --- | --- |
| `20261010更新步骤.docx` | 14336 | 原 LBA 327 + 14336 bytes | `7e0e6e9e44fca400aa9c4e7a4cf27b171a6046d8acac0ea88e5260414427dc39` |
| `burnlight-vat-mac-20261012.txt` | 56 | LBA 12046 + 56 bytes | `faaabcbc28a26670d4b26b2a47b350d98d8a03c7778e22274931e636c6778b37` |

独立 Python oracle 用正常实际读回的 `.blocks` / `.tracks` 有效缓存解析当前目录、文件内容、地址空间、checksum/CRC/Tag Location、两 Anchor、主/备用 VDS 一致性、VAT 尾部/previous 和 LVID，见 `independent-completed-normal-readback.json`。旧 ICB、payload/hash 保持，新 Session 没有复制旧 payload。

macOS 实际挂载节点 `disk4s0` 属于同一 whole-disc `disk4`，UDF 挂载两文件的名称、层级、大小、hash 全部匹配，见 `post-completion-mounted-comparison.json`。最终只读 GUI 包在临时目录以相同八项信任 hash 运行导出适配器；再次实际扫描、导出及另一份全新读取均通过。独立 oracle 解析其两缓存，导出文件 hash 也一致，见 `post-completion-gui-export-verification.json`。这是实际适配器导出；GUI 窗口交互为既有 142 条受控断言，没有通过 GUI 写盘。

## Windows 往返待执行

材料为 `windows-handoff-20261012.zip`，包含本地只读 PowerShell 收集器、当前两文件预期清单、操作说明，以及待追加源文件 `burnlight-vat-windows-20261012.txt`。源文件 57 bytes，SHA256 `ad23bb38b333053f08859a087f9a9d75e94a704a98b3c98617be02ce1e6c1464`，尚未写到光盘。

先在 Windows 正常打开旧 docx 和 Mac 新文件，采集带预期清单的写前报告；按原有 Windows 方式追加现成源文件，再正常打开三文件并采集写后报告，记录 OS、光驱及实际刻录方式。写后最新 Session 地址未知，收集器不沿用写前 ExpectedJson，也不预设 Session 数一定增加。返回 Mac 后重新探测最新入口/VAT并读取所有文件，对照源 hash 和本次旧 extent 基线。

## 判据与构建边界

本次没有源码功能修改。修复后的七组 CTest、55 项 VAT 专项、同七组 ASan/UBSan、GUI 142 断言、搬移包七组、冲突 50 项、短卷 8 项、VAT 导出 4 项、既有导出 10 项仍绑定相同最终源码/产物；未把不支持的 leak detection 算作 PASS。三个原基准 executable 和两 provenance 的 hash 均保持，历史验收未覆盖，未安装或 push。

另交付 `build-udf201-gui-v9-write/output/BurnLight.app` 及同目录 `BurnLight-udf201-vat9-local-acceptance-arm64.zip`，为可写本机验收包，未安装或启动刻录。它复用经过上述验证的相同编译代码和八项信任文件，仅将配置中的 `allowPhysicalWrites` 改为 true 并重新签名；deep/strict ad hoc 签名和 vat9 版本检查通过，绑定在 `write-enabled-gui-package.json`。原只读测试包保持独立。启用配置不等于 GUI 写盘或 vat9 全流程物理验收 PASS。

| 判据 | 当前状态 |
| --- | --- |
| CURRENT_8SESSION_READ_PASS | 原损伤盘全文件读取未通过，保持独立 |
| CROSS_SESSION_REFERENCE_PASS | 原八 Session 实际跨引用元数据 + 独立 fixture PASS；本次旧分区/ICB/VAT/payload 引用也通过 |
| SINGLE_SESSION_REGRESSION_PASS | 替换盘写前完整真实读取、挂载比对 PASS |
| VAT_OFFLINE_APPEND_PASS | 最终受控离线及独立验证 PASS |
| EXISTING_FEATURE_REGRESSION_PASS | 最终原六组及 sanitizer/GUI 离线回归 PASS |
| GUI_INTEGRATION_PASS | 受控 GUI + 最终适配器两文件实盘导出 PASS；GUI 写盘未执行 |
| MAC_PHYSICAL_APPEND_PASS | **本次追加并经单独授权完成 Session 后 PASS**；不是 vat9 正常 burn 全流程的物理复验 |
| WINDOWS_READ_AND_REAPPEND_PASS | 待验收，尚未执行 |
| MAC_RETURN_READBACK_PASS | 待验收，尚未执行 |

正式八 Session 主样本的完整闭环尚未成立，完整功能未验收完成。各项输入、CLI/backend/source hash、证据和保护对象绑定见 [机器可读摘要](evidence/udf201-session-completion-20261012.json)。本次失败阶段保留在 [此前记录](udf201-mac-attempt-20261012.md)。
