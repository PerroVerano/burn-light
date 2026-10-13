# GUI 追加按钮修复与本机验收（2026-10-13）

本次修复已安装到 `/Users/luzove/Applications/BurnLight.app`。在当前健康六 Session CD-R 上，原生 GUI 重新检查目录、建立输入快照、构建并验证追加预览均成功；“追加刻录…”按钮实际启用。本轮没有点击刻录按钮，没有写盘。

## 原因与历史记录核对

此前两次预览任务 `4778472A-284D-4B37-BED9-A8B14B73AD19`、`62A83B10-3E43-45D9-9E40-9459F0D1FD1E` 已构建成功。GUI 的全局 `needsInspection` 状态来自历史刻录任务 `3F220DD1-C24F-45B8-8DCB-E3374271ABDD`：它检查的是原损伤八 Session 盘，读取 LBA 62772 时失败。预览成功消息覆盖了屏幕状态说明，却没有解除历史写入锁定，用户看不到按钮禁用的真正原因。

核对该任务的原始归档、CLI 退出收据（exit 1、执行器状态已知）、`before-write-read` 错误路径、当时核心二进制 SHA256 与对应源码控制流程，确认失败发生在写前旧内容复读阶段，尚未调用 `recordSession` 数据写入。没有写入日志仅作为辅助证据，不单独用于证明未写入。当前健康六 Session 盘与该历史八 Session 盘的状态分别记录。

在没有活动光盘任务、活动标记和锁占用的情况下，只纠正这一个已确认的历史任务索引：`needsInspection` 改为 `failed`，`requiresInspection=false`，说明写前介质读错误。原索引已保存，原归档未修改；没有强制解锁、删除未知执行器标记、重新刻录或完成会话。本次是依据确切证据的一次任务核对，没有新增自动清除失败锁定的功能。未知执行器、未确认写后状态和任务索引错误仍阻止刻录。

索引原始 SHA256：`aeb8585a42bfacc6d28b5aeb9fce5a528ce4930ff11ce9f53aaf20a4ae47ff7e`。纠正后 SHA256：`73463525d9f0b132840e3387c2e70e9a61ef9352ed612da290cd9c30e97aa056`。核对依据及操作范围见证据目录中的 `failed-job-prewrite-assessment.json`、`job-index-reconciliation.json`。

## 源码、构建和安装

- `AppModel.swift` 新增 `writeDisabledReason`，`canWrite` 使用同一判断，保留全部原有准入条件。
- `ContentView.swift` 在按钮下显示实际禁用原因；历史任务尚未核对时，即使预览成功，仍解释为什么不能刻录。
- `ValidationMain.swift` 增加预览成功但历史锁仍存在、未知执行器、任务索引错误、预览绑定变化和正常准入的检查。
- 独立目录 `build-gui-button-fix-20261013` 构建并运行 GUI 检查：**151 项 PASS**，测试没有真实刻录。启用写入的安装包单独保存于 `build-gui-button-fix-20261013-write/output/BurnLight.app`，签名及安装后完整文件 hash 比对通过。

安装时间为 2026-10-13 08:44:51（本地时区）。旧应用保留为 `/Users/luzove/Applications/BurnLight-previous-20261013-084451-26228a28.app`。新 GUI 可执行文件 SHA256 为 `0ed6e17ea63b93f08079fddd54d19274eaf83d388d74c215fb6fe2e61262d400`。八个受信任核心、后端、导出及库文件 hash 与前一已验收安装包一致；CLI 为 `50f44fc8435cddb9347c7d9ee8558b9bd8f4c1f99b681495a7a1f5fd2c749b7a`。

本轮只修改三处 GUI 源码和使用说明，没有修改 CLI、VAT 读写器或后端，因此没有把此前的 CTest、导出、ASan/UBSan 或真实往返结果记录为本轮重新执行。原受保护 CLI、已安装全局后端及 provenance 文件的五个基准 hash 保持。源码绑定、安装包完整清单及最终 Git 检查见[机器记录](evidence/gui-append-button-fix-20261013.json)。

## 原生 GUI 实际检查

现场目标为 Lenovo Ultraslim DVD XL6M，设备 `1,0,0`，Registry ID `4294983430`，`/dev/disk4`；UDF 2.01/VAT，显式 Windows FID 兼容配置，六个已完成 Session/Tracks，无未完成 Session，可追加。最新入口 LBA 133718，NWA 140918，剩余 218925 blocks。下一条空白 Track 不计为已完成第七次刻录。

| 原生操作 | 实际任务 | 结果 |
| --- | --- | --- |
| 检查介质与最新目录 | `5A0BAD78-43C4-4C6A-AEFF-E03DDD81C785` | 完成，显示五个现有文件 |
| `/Users/luzove/Desktop/Burn` 建立输入快照 | `F1F958A2-9CD6-49AC-A8B9-BE8BB17341FF` | 完成，唯一 ZIP 与原快照一致 |
| 构建追加预览 | `8BD2F134-380D-4036-9224-FBA4F7E6038A` | drives/build/verify 均正常退出；预览验证通过 |
| 原生按钮状态 | 原生辅助功能状态读取 | “构建追加预览”和“追加刻录…”均启用；未点击刻录 |

唯一新增输入为 `AlgoLogFileWatcher-delivery.zip`，16327 bytes，SHA256 `b37388f29ada6694bbfd62b72814d1d3180363be0cc315aa59db3e4bc9748a84`。默认冲突拒绝。预览镜像为 576 blocks / 1179648 bytes，SHA256 `fa03154d2ff88983fb961a0786da2b5fe4c8dbf193840c5a17683921169263f7`。原生验证及输入绑定见 `native-preview-result.json`。

## 独立离线预览检查与边界

使用独立 `tests/vat_oracle.py` 严格解析实际 GUI 生成的预览及复合历史视图，结果 **PASS_INDEPENDENT_STRICT_OFFLINE_ACTUAL_PREVIEW**。复合输入包含此前实际读回且 hash 核对通过的六 Session 缓存、当前任务新读取的元数据，以及这次生成的预览镜像；只复制有有效块索引的缓存块，不把稀疏洞当作零。

预览中 VAT 位于 141493，previous VAT 为 134015；新 ZIP 与五个旧文件组成六文件目录，所有五个旧文件的内容 hash、extent 和 Unique ID 保持，新 ZIP hash 与输入一致。复合镜像中的假设新 Track 仅用于离线检查，不代表光盘已经有第七个完成 Session。本轮没有全量新读回旧 payload，也没有真实追加或新的 Windows 验收；完整旧内容的实际读取依据仍是 2026-10-12 的六 Session 读回记录。独立结果见 `independent-preview-result.json`。

原损伤八 Session 主样本的完整读取仍为 **NOT_PASS_MEDIUM_ERROR**，不能用当前健康盘的按钮或预览结果回填其验收。此前真实 Windows↔Mac 往返的范围保持原记录，不新增完整功能 PASS。

## 证据与最终范围

本轮证据根目录：`artifacts/gui-button-fix/20261013-084140/`。包括修改前源码、原失败任务完整归档及索引、核对依据、151 项检查结果、安装/备份清单、三次原生任务材料、输入快照、实际 manifest、独立离线验证和服务状态。只略去工作目录中的 root-owned `toc.dat` 重复副本，原始光盘 TOC 及生成布局诊断已保存。真实用户内容和大缓存留在忽略目录，不提交 Git。

本轮结果为 **GUI_APPEND_PREVIEW_AND_BUTTON_PASS**。物理写入次数为 **0**；没有 init、erase、封盘、recover、recover-finish、强杀、推送或远程发布。旧证据报告未覆盖，最终 `git diff --check` 通过。
