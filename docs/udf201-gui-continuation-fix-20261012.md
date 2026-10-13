# Windows 再次追加后 GUI 目录读取失败：修复检查点

2026-10-12，用户安装新版 GUI 后从 Windows 资源管理器再次追加，GUI 检查退出 1、目录为空。最新真实 GUI 日志显示盘为 6 个完成 Session / 6 Tracks，latest 133718、NWA 140918、appendable、无 unfinished Session，盘 ID `0x380500`。错误为 `Windows zero-location FID ICB is not current transaction metadata`。**代码修复和离线验证已通过；最新盘实际全目录读取、GUI 构建/替换/窗口验证仍待系统 I/O 恢复，不能报告实盘修复 PASS。** 本阶段没有写盘。

历史 [四 Session 正常追加验收](udf201-vat9-normal-burn-20261012.md)保留当时真实结果，不能作为当前六 Session 的目录读取通过。

## 实际失败与地址证据

原 GUI 任务：`B1BD6FA1-C5D0-4BE1-82FE-3560BB8315E9`，2026-10-12 15:14:44（Asia/Shanghai）。CLI 明确退出 1、workerStateUnknown=false、Disk Arbitration CONT 已发送；当时没有残留任务标记或光盘 worker。原任务与归档保留于用户 Library 的 Records/Archives，未覆盖。

本次证据：`artifacts/udf201-gui-fix/20261012-151937/`。复制原归档实际读取的有效块到 sparse cache，同时保存 `.blocks` / `.tracks`；没有将缺块变成可用零块。`fid-metadata-analysis.json` 是独立元数据分析，**不是完整目录/文件内容 PASS**。

| 结构 | 实际地址/所属 Session |
| --- | --- |
| 最新 Anchor | 133974，Session 6 |
| 主 VDS | 26466，Session 4 |
| 物理分区起点 | 288，Session 1；Partition Number 8192 |
| 最新 VAT previous 链 | 134015 → 126815 → 27009 → 19531 → 12329 → 351 → 319 |
| 当前 root | virtual 466 → ICB 133984，Session 6 |
| 历史 Windows root | ICB 60832，Session 5，目录数据包含 45440 / 45441 |
| `lssjk.jar` FID | current root byte 10240，Tag Location 0，virtual child 468 |
| jar ICB 映射 | 126782，Session 5；最新与上一 VAT 指向同一实际地址 |

旧规则只允许零位置 FID 引用最新物理 Session 的新 EFE，无法处理下一次 Windows 事务保留该 FID 和旧 ICB 的情况。原缓存尚缺 jar EFE 和历史目录数据；必须真实补读后才能证明当前盘符合扩展规则。

当前目录还含 flags=5、清零 ICB、compression ID 255 的已删除 Office 临时记录。依据 [OSTA UDF 2.01 §2.3.4.2](https://13thmonkey.org/documentation/UDF/udf201.pdf)，已删除记录可保留在目录中，释放后清零 ICB，并使用 254/255 标记原名称编码；它不属于当前活动文件。结构及 CRC 无效的记录仍拒绝。

## 限定修复

`windows-fid-location-v1` 仍显式标记非严格位置兼容。新分支要求当前普通 EFE 为 Microsoft 结构、FID/ICB UniqueID 一致；沿有效 previous VAT 链追溯首次分配事务，核对同一虚拟目录、目录 UniqueID、相同字节偏移和完整 FID 字节。源事务必须为 Microsoft VAT/目录，旧 ICB 必须保持实际地址，来源 EFE 位于该事务已记录 Track、早于对应 VAT 且晚于其 previous VAT，所有更早表未映射该虚拟 ICB。来源目录还必须具有可由更早严格 FID 证明的搬移记录。无法证明即失败，不回退历史目录。

历史非零 FID 的来源校验以对应 VAT 事务的先后顺序为界，允许同一物理 Session 内的多次逻辑事务。历史搜索/字节、缓存、Track、CRC、版本、地址空间和循环限制保留。已删除分支仅支持本次所需的释放记录：合法 flags、清零 long_ad、无 implementation-use、254/255 名称编码；不解引用零 ICB，不隐藏活动文件，诊断记录排除的已删除项。累计 FID 数限制为 100000。

另修复介质枚举：真实 IOKit 当时同时保留旧、当前两个同名 `disk4` 对象，旧对象 state=1，当前 state=30。公开 `IOBSDNameMatching` / `IOServiceGetMatchingService` 实测返回当前 ID 4295079361。代码使用全部发布匹配，要求唯一，且只接受递归子对象与当前发布 BSD 服务的 registry ID 相同者；发布匹配不唯一、两个有效 whole-media 或身份变化仍拒绝。不会猜节点或取第一个重复对象。

## 离线验证与待办

独立编码器新增 Windows 保留零位置 FID / 下一 Session 与同 Session 下一 VAT / 已删除临时记录。原 CLI 可重现相同失败；修复后核心和显式兼容独立 oracle 通过，默认严格 oracle仍拒绝位置偏差。测试覆盖来源 FID 字节变化、错误 VAT/目录厂商标识、UniqueID、首次分配前已映射、缺失历史缓存，以及删除记录非法 ICB/编码/flags/CRC。兼容来源的离线追加生成严格新目录并保持旧 payload/内嵌 ICB 地址。

隔离构建 `build-udf201-history-cli`：7/7 CTest、89 项 VAT 通过；`build-udf201-history-asan` 同样通过 7/7 和 89 项，`ASAN_OPTIONS=detect_leaks=0`、`UBSAN_OPTIONS=halt_on_error=1`，不声称 leak detection PASS。新增第六个 GUI 适配器离线导出场景，尚待新 GUI 构建后执行。此前 146 条 GUI 验证属于旧构建，不能回填本次。

三块只读补读取证第一次在设备枚举前失败，退出 1，没有补读；第二次在 preflight 的 drutil 内核 I/O 等待，尚未进入 unmount/guard/capture。原 drutil 为 `?E`，父只读 helper 仍等待，Disk Arbitration 为 `Ss`，共享锁/标记保留。重新连接未释放等待；用户回复“好的，稍后我会重启”。启动 UUID、真实 worker 状态、只读 helper hash 和静态链接库快照保存在 `pending-restart-checkpoint.json`。不人为强杀，不伪造退出收据，不解除未知任务标记。历史 helper 的库路径随后被离线重建，正确绑定使用已保存的原静态库快照与 helper binary hash。

重启后须先核对新启动状态、原执行器结束、实际光驱/介质和共享锁，再完成三个缺失元数据块及全新 CLI/GUI 实际读取，独立核对当前目录和旧内容；运行新 GUI 离线导出/原回归，正常退出旧 GUI，保存备份后安装修复包并验证窗口出现当前文件列表。以上均待执行，无追加写盘授权或计划。[机器检查点](evidence/udf201-gui-continuation-fix-20261012.json)绑定源码、构建、原输入和检查结果。
