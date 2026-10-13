# BurnLight 1.2.1 本机版本暂时结档 · 2026-10-13

用户在本机试用后反馈：“好用，爱用。”“现在我通通测试了一遍没什么问题”，并明确授权提交、push 远程和暂时结档。本记录将该反馈作为**用户人工验收**，不代替各次构建、实盘读回或 Windows 自动报告。

当前交付为 **1.2.1 / build 5**，已安装在 `/Users/luzove/Applications/BurnLight.app`。本阶段按用户要求暂时结档，后续发现问题再进入新的修复与验收阶段；不继续扩大格式或硬件支持范围。

## 本阶段已交付

| 范围 | 验证与记录 |
| --- | --- |
| UDF 2.01/VAT 读取及追加 | 独立解析地址空间、合法跨 Session VDS/分区/数据引用、最新 VAT、当前目录及 FE/EFE/内嵌数据；独立 fixture 和 oracle 验证，见 [初始交付](udf201-acceptance-20261012.md) |
| 健康 CD-R 的 Mac/Windows 往返 | 换盘、Mac 追加/实际读回、Windows 人工读取及再追加、Mac 返回读回；修复后的 vat9 正常追加与 Windows 四文件读取，见 [正常追加验收](udf201-vat9-normal-burn-20261012.md) |
| 后续 Windows 再追加目录兼容 | 受限 `windows-fid-location-v1`、历史来源证明、删除记录处理；六 Session 五文件真实读取/导出及旧 hash/地址保持，见 [GUI 返回盘修复](udf201-gui-six-session-readback-20261012.md) |
| DVD-R 同一受支持 VAT 结构 | GUI 追加、Windows 人工读取并再追加 Swagger、Mac 最新三文件两次实际读回/导出、旧内容与地址保持，见 [DVD-R 往返](udf201-dvd-r-roundtrip-20261013.md) |
| GUI 追加按钮状态 | 准入与禁用原因一致，已核实的写前失败记录保留材料后纠正，实际预览与按钮启用通过，见 [按钮修复](gui-append-button-fix-20261013.md) |
| 本地存储清理 | 清单/空间预览、逐项选择、7/30 天保留期、临时副本或历史材料清理、当前及待检查任务保护、归档恢复，见 [清理验收](gui-storage-cleanup-20261013.md) |

macOS 首刻保持 ISO9660 + Joliet；原支持的 UDF 1.02、skip/replace、导出、CD-R/DVD-R 物理准入、退出收据与 Disk Arbitration guard 保留。没有新增 UDF 2.01 首刻、RW/BD、sparing/metadata partition、任意第三方布局或 VAT recovery。

## 最终构建与检查

最终源码绑定于 [1.2.1 清理机器记录](evidence/gui-storage-cleanup-20261013.json)：77 项源码 hash，193 项 GUI 检查（含 40 项新增清理及准入检查）、7/7 CTest、50 项冲突与 6 项 VAT 导出通过。最终构建包、ZIP 解压搬移包、安装包的全部文件 hash 一致，严格签名通过；搬移后的 ISO9660/Joliet 首刻镜像生成与验证通过。

1.2.1 的八项刻录核心、导出器、后端和库 hash 与 1.2.0 DVD-R 往返版本一致；原 `build/appendcd`、两个基准 runtime 及两份 provenance 的五项 hash 保持。已安装 GUI SHA256 为 `d5c14b503494d66cf025481967856f5954a0aa9c29f33413201a8a3c4e5e5dbb`，本机 ZIP SHA256 为 `5a232da76ccf4d1909147aa47bd1c1543dc5221b50bd6bb461be17897950faa9`。

本轮提交保留源码、合成测试、构建脚本、文档和小型验收摘要；用户文件、真实光盘缓存、完整物理日志、生成镜像和安装包保留本地，不加入 Git。历史报告与其中的阶段性待验收状态不改写，由本记录链接到后续结果。

## 保留的验收边界

原损伤八 Session CD-R 已成功解析最新入口、跨 Session VDS、VAT 与目录，但全文件读取仍为 `NOT_PASS_MEDIUM_ERROR`；健康替换盘和 DVD-R 的成功不回填原盘。Windows 反馈为人工验收，未采集的源 SHA256、IOCTL 或自动报告继续按原记录标注。受限 Windows FID 偏差兼容不宣称严格规范符合。本机包采用 ad hoc 签名，未作 Developer ID 公证。

用户本次反馈确认当前本机版本使用无异常，故暂时结档。结档不新增写盘授权、不执行再次刻录或介质操作，也不删除历史证据。
