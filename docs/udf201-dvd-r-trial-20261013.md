# DVD-R / UDF 2.01 / VAT 试用开放（2026-10-13）

用户明确要求在当前 DVD-R 上开放试用。试用包已安装到 `/Users/luzove/Applications/BurnLight.app`，当前盘的原生 GUI 检查和追加预览通过，“追加刻录…”已启用，界面显示 DVD-R VAT 试用标识。本轮没有点击刻录，也没有真实写盘；DVD VAT 的物理追加、实际写后读回和 Windows 往返仍为 **PENDING**。

## 实现与范围

取消物理 inspect/list/build/export 对 UDF 2.01/VAT 的 CD-R 专属限制，仍只接受原 CD-R/DVD-R 介质及已实现的 type-1 + Virtual Partition Map 结构。VAT 解析器与生成器未修改，保留地址空间、最新事务、CRC、已记录范围、缓存缺块、循环和历史来源校验。

新增 `recordingBackend` 按介质选择执行器：CD-R VAT 保持 vat9 可变包路径；DVD-R VAT 使用已有 media6 的 DVD incremental 写入及 Session 收尾，参数为 `-multi -data`，不传 CD 的 `-packet`、`-xa`、`-appendcd-vat`。DVD VAT 写前额外要求起点和镜像长度按 16 扇区对齐；原 VAT 生成器已将镜像长度对齐到 32 块。media6 的真实 Track/NWA/容量、设备独占、写前全部旧内容基线及写后实际读取检查保留。该复用尚未取得 DVD VAT 物理成功证据，不能由既有 DVD ISO/UDF 1.02 的成功回填。

不扩大到 RW/BD、任意 UDF 布局或 VAT recovery；首刻仍生成 ISO9660/Joliet，失败后不自动重烧或完成会话。GUI 对 DVD-R VAT 显示试用说明。格式依据为 [OSTA UDF 2.01](https://13thmonkey.org/documentation/UDF/udf201.pdf)及其 [2000 年 DVD 互换性勘误](https://www.13thmonkey.org/documentation/UDF/udf201er.pdf)；本次不宣称消费型 DVD 录像机互换性。

## 当前实盘只读及实际预览

目标：Lenovo Ultraslim DVD XL6M，设备 `1,0,0`，Registry ID `4294986360`，`/dev/disk4`。DVD-R，3 个已完成 Session / 3 Tracks，无未完成 Session，可追加；最新入口 100272，NWA 106784，剩余 2191104 blocks / 4487380992 bytes。这些地址来自本次探测，不作为下次固定目标。

| 实际原生任务 | ID | 结果 |
| --- | --- | --- |
| 检查介质与最新目录 | `FE045723-06ED-4563-820F-B0F1404A0BD8` | inspect/list 均成功，显示唯一当前日志 |
| 固定 Desktop/Burn 输入 | `C898CA2B-4DAF-42E1-836A-47CB848B2141` | 1 个 ZIP，内容与此前快照一致 |
| 构建并验证追加预览 | `A4A63316-3AC4-46A4-852F-906EADB49785` | build/verify 成功；追加按钮实际启用 |

当前权威文件为 `20261008_1-T-20261008103937-7cf51ef0.log`，3068 bytes，实际有效缓存读取 SHA256 `9ff8d85860f793293d639b242076ca9e592d3c1f3a39cf28bf113e71bc5cd314`，数据 LBA 100576。没有把历史目录合并到当前视图。

最新 Anchor 100528 → main VDS 100304 / reserve VDS 100320；Partition Start 100544；最新 VAT ICB 100623 → previous VAT 100559，两次 VAT 事务同属 Session 3。FSD 100578、根 ICB 100579、日志 ICB 100580。该盘最新布局本身不提供跨物理 Session VDS 示例，不能替代原八 Session 专项证据。

新增预览输入：`AlgoLogFileWatcher-delivery.zip`，16327 bytes，SHA256 `b37388f29ada6694bbfd62b72814d1d3180363be0cc315aa59db3e4bc9748a84`；默认冲突拒绝。镜像 576 blocks / 1179648 bytes，SHA256 `c8ca812d7acb79780e99cb1ab2cf616f4219e62ee41e40b04f7efed2130d42cb`。

独立 `tests/vat_oracle.py` 严格解析当前实际有效缓存和真实 GUI 生成镜像的离线复合视图：**PASS_INDEPENDENT_STRICT_DVD_VAT_ACTUAL_PREVIEW**。新 VAT 107359 → previous 100623，旧日志 hash/extent/UniqueID 保持，新 ZIP 内容与固定输入一致，起点和长度通过 16 扇区对齐。只复制 `.blocks` 标记的有效块，缺块不当作零。离线假设 Track 4 不代表已经完成第四次物理刻录；当前盘仍为三 Session。

## 构建、回归与安装绑定

- 独立 Debug CLI：`build-dvd-vat-trial-20261013/`；七组 CTest 全部通过，含原六组及 VAT 专项。
- GUI：`build-dvd-vat-gui-20261013/`，153 项断言、50 项冲突检查、6 个 VAT 导出场景通过。本轮新增介质/后端分流及 DVD VAT 事务绑定检查。首轮新增测试误把介质文字当作 burn argv 参数，修正断言后重跑通过，首轮日志保留。
- 可写试用包：`build-dvd-vat-gui-20261013-write/output/BurnLight.app`，2026-10-13 09:22:50 安装；签名、受信任文件和完整安装 hash 比对通过。
- 旧安装保留：`/Users/luzove/Applications/BurnLight-previous-20261013-092250-f3f42895.app`。五个受保护基准 CLI/runtime/provenance hash 保持；没有修改 Homebrew、全局后端或历史报告。

直接从 Documents 执行管理员脚本的初次检查被 macOS 权限拒绝，CLI 未启动；之后通过已安装 GUI 的既有认证、私有任务、物理锁和 guard 完成实际检查。未更改 TCC/SIP。未强杀、强制解锁、init、recover、recover-finish 或封盘。

本轮证据根目录为 `artifacts/dvd-vat-trial/20261013-091539/`，包含修改前 hash/源码、构建日志、原生任务、实际光盘缓存和预览、独立验证、安装备份及服务状态。root-owned 预览布局诊断已只读另行保存。用户内容和大缓存留在忽略目录，未提交 Git。最终 `git diff --check` 通过；没有 commit、push 或远程发布。

完整源码和二进制 hash 清单见[机器记录](evidence/udf201-dvd-r-trial-20261013.json)。本轮结果为 **DVD_VAT_TRIAL_GUI_READ_AND_PREVIEW_PASS**，物理写入次数 **0**，不能报告 DVD VAT 真实追加或 Windows 往返 PASS。
