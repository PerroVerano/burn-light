# Windows 连续更新后的 UDF/VAT 兼容修复与只读验收

2026-10-19（Asia/Shanghai），BurnLight 更新为 **1.2.2 / build 6**。当前 DVD-R 最新目录的 9 个文件已完成两次全新光盘读取、独立解析及本地副本 SHA-256 比对，共 **178,427,683 bytes**；JAR/ZIP 内部条目 CRC 通过。本轮没有写盘。最终安装版原生窗口已重新检查，正常显示完整目录；检查任务 `C32D5DB2-BE26-4F70-9582-246C54D11BD8` 的 inspect/list 均退出 0。

## 问题与修复范围

原 1.2.1 对同一份真实缓存稳定报 `Windows zero-location FID lacks exact first-allocation VAT origin proof`。首次阻断文件为 `aisea.jar`：虚拟 ICB 430 / UniqueID 22 保持，文件 EFE 在连续 Windows VAT 事务中从 222073、222132 等地址迁移，最新为 408221。旧规则把“文件身份已存在”和“物理 EFE 不变”绑定在一起，无法读取更新后的文件。

修复继续要求 CRC、Tag、Microsoft 来源、完整 FID 字节、同一逻辑目录及偏移、目录 UniqueID 和原始目录搬移证明。先证明当前文件身份的分配事务，再沿 previous VAT 逐次验证相同虚拟编号及 UniqueID：每次 EFE 迁移必须位于对应事务、有效已记录 Track 内，且身份和类型不变；中途缺失映射、身份变化、回指旧地址和损坏元数据仍拒绝。最新目录与内容始终来自最新 VAT。

本盘还包含两个既有边界：虚拟编号 429 曾映射到实际记录的全零块 138789，后来成为新文件 ICB；`System Volume Information` 是 flags=3 的隐藏目录，其目录项也是零位置 FID。现在以 VAT 的 UniqueID 分配区间区分编号此前的用途，旧映射仅接受已验证的不同文件身份或真实读取的全零块；缺失缓存绝不视为零。隐藏文件/目录使用相同来源证明，FID 目录标志必须与 EFE 类型一致。沿用显式 `windows-fid-location-v1` 标记，不宣称严格 UDF 规范符合。

## 本次真实光盘证据

- Lenovo Ultraslim DVD XL6M；DVD-R，12 个已完成 Session / Track；latest Session 367936，NWA 414432，latest VAT 408271，仍可追加，无 unfinished Session。
- 目录包含 9 个文件及 1 个子目录，包括 `System Volume Information/IndexerVolumeGuid`。GUI inspect/list 均退出 0，显示 UDF 2.01 / VAT 及显式兼容说明。
- 导出任务 `BF69C0FE-82FF-4B16-BE4F-F53A1E80B7DC` 的 `source-read` 和 `verification-read` 是两份独立实际读取的稀疏缓存，均保留有效块索引和 Track 区间。导出器复读全部缓存元数据和文件内容，出具 `OPTICAL_EXPORT_VERIFIED`；用户权限复制后又复核本地 hash。
- 独立 Python 解析器分别验证两份缓存的 Tag/CRC、VAT 链、当前目录、兼容来源、实际数据地址及全部 payload；两个结果和本地副本逐文件一致。全量文件大小、SHA-256、UniqueID、extent 和 ZIP CRC 条目数见机器记录。
- 完整用户文件、缓存、应用和大日志保留在忽略的 `artifacts/vat-update/20261019/` 与原 GUI 任务归档，不纳入源码提交。未取得 Windows 原始源文件 hash；此证据证明当前盘的两次实际读取与本地副本一致。

## 测试与安装

最终源码的七组 CTest、113 项 VAT 检查、193 项 GUI 断言、10 个 VAT 导出场景、50 项冲突检查、8 项短卷检查和 10 个既有导出场景通过。ASan/UBSan 七组通过；`detect_leaks=0`，不算 leak detection 通过。新增独立编码器覆盖连续更新、同 Session 多事务、旧编号用途、真实零块、隐藏目录、最新内容与保留 extent；反例包含身份/厂商变化、UID 分配时序、删除后映射缺口、回指、CRC 错误、缺块与目录类型伪装。

最终安装为 `/Users/luzove/Applications/BurnLight.app`；与已通过实盘验证的测试包共用完全相同的八个工具/库 hash，保留原安装的物理写入开关，仅本轮实际操作限定为只读。原 1.2.1 备份为 `/Users/luzove/Applications/BurnLight-previous-20261019-b18a77af.app`。签名、安装文件逐项 hash 和受保护的原 CLI/media6/initial7/vat9 基准均核对。版本包位于 `build-vat-update-gui-20261019-release/output/`。最终原生窗口运行进程路径已核对；光盘任务均结束、全局锁可取得、active marker 已移除、Disk Arbitration 正常运行。前后 Track 几何、Session 起点、NWA 和剩余容量相同。

[机器验收记录](evidence/udf201-windows-updates-20261019.json)绑定最终源码、测试包/安装文件、真实导出任务及完整文件 hash。本轮不替代历史损伤介质的验收，也未新增物理刻录或 Windows 实机执行证据。
