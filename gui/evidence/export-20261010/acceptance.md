# 光盘文件导出到本地：验证记录

2026-10-10（Asia/Shanghai）。本轮仍为离线 / 只读交付，**零光盘写入**。

已交付可实际编译、签名、运行的 SwiftUI/AppKit GUI。光盘文件表支持多选文件或目录；没有选择时导出全部。用户选择目标后，独立只读适配器复用原样 C++ 核心读取最新完整 Session，校验目录和实际 payload，另建光盘缓存复读 metadata/payload；GUI 再按当前用户权限拷贝并复核本地大小、SHA-256 和完整目录。最终目录仅在全部成功后以不覆盖方式提交，失败保留 `.partial` 与日志。默认 appendcd、两个 runtime 及 C++ 源码原样。

| 验证项 | 结果与证据 |
| --- | --- |
| 实际编译 / 签名 / 启动 | PASS；`build.log`。明确 MacOSX26 SDK，独立 `.adapter-build/` + Swift Core/App；本机 ad hoc 签名校验通过。CUA 实际启动，写按钮禁用。 |
| Swift 适配 / 本地拷贝 / 状态 | PASS；`validation.json` / `validation.log`，97 项断言。目录包含后代、文件包含父级、路径穿越、未知选择、hash 变化、零字节、中文、旧 `.DS_Store`、本地重复目录不覆盖、任务记录、写后检查锁保留与零写请求。 |
| 独立 C++ 适配器离线导出 | PASS；`adapter-offline.json` / `.log`，10 组。Joliet 全量/目录/嵌套文件；UDF 多 Session 旧 extent + 新文件；保存的真实 CD Windows Session；错误大小、路径穿越、缺失路径、物理 LBA 参数和 writer 命令拒绝。导出的 hash 与原输入文件/历史实际读回 hash 独立比较。离线测试本身不读取实体光盘。 |
| 原六组回归 | 6/6 PASS；`cli-regression.json` / `.log`。复制原测试二进制和 CTest 定义到新临时树，未清理历史 artifacts。 |
| 真实 GUI 检查与目录 | PASS；Job `8C5BF454-C8A0-4B4C-9C3A-E67E075BFA44`；原 CLI `drives → inspect → list`，两次系统认证桥接退出收据为 0。 |
| 真实 GUI 全盘文件导出 | PASS；Job `A6A8C375-60B9-4B95-A9F4-A2805E8E9A21`。GUI 原生目录选择器发起；adapter receipt=0，`OPTICAL_EXPORT_VERIFIED`，4 文件 / 246241 bytes。包含 Windows 追加的 `swagger.json` 与 3 个 Mac 首刻文件。 |
| 真实 GUI 所选目录导出 | PASS；Job `04C9EDD9-9DDC-4110-9070-5F02787E6515`。实际点选“中文目录”，导出 1 个子文件 / 39 bytes，保留目录层级。 |
| 导出本地文件的独立核对 | PASS；`actual-copy-check.json`。两次输出文件均属于当前用户，每个大小/hash 与历史 `windows-session2-expected.json` 一致。原内容均保留在本目录 `local-copy/`。 |
| GUI 展示与 Finder | PASS；CUA AX/截图可见两次“任务完成”、全盘/所选按钮、任务期间其他物理按钮禁用；点击“显示导出文件”，Finder 实际打开第二次导出目录，显示“中文目录”。见 `ui-check.md`。 |
| 物理任务结束 / 基线保护 | PASS；`process-state.txt` 显示 diskarbitrationd=`Ss`，无 appendcd/burnlight-export/readcd/cdrecord/osascript 残留，active 标记不存在。`baseline-final.json` 核对原 src、build/appendcd、静态库和两 runtime 等 47 项全部一致。 |

当前实测光驱是 Lenovo Ultraslim DVD XL6M，device `1,0,0`，Registry ID `4295022013`，BSD `/dev/disk4`。CD-R 2 Sessions / 2 Tracks，latest=11702、NWA=18904，余量698243072 bytes，仍可追加。适配器在导出前、复读前、复读后均由原核心复查介质几何；没有改变 Session 或写入边界。

实际退出收据、stdout/stderr、任务索引、选择文件、optical export 清单、local export 清单及原任务缓存归档在 [physical-jobs/index.json](physical-jobs/index.json)。归档只收集本轮三项检查/导出任务，保留原始日志和绝对路径。不可读的额外 backend `toc.dat` 按索引明确排除，对应核心 `before-toc.txt` 保留。没有通过修改那些 root 文件权限收集证据。

本轮 GUI/adapter SHA256 及所有依赖 hash 在上述索引中；最终 GUI SHA256 为 `1846dd118d305da1c6e006125a643a1477fa0b309a112f87ba80f84deb550e92`。

当前剩余边界：

- 只支持原 CLI 范围内的可追加、完整 Session CD-R/DVD-R；封盘、CD-ROM/DVD-ROM、空白/未完成 Session、其他文件系统、inline/unmapped UDF 与 symlink/特殊文件不支持。
- DVD-R/UDF 的新导出路径本轮仅做离线验证；实体导出验证使用当前 CD-R/Joliet。其他型号/多真实光驱、大文件/满盘/拔盘与桥接故障的硬件注入未执行。
- 保存名称、目录、内容；原权限、时间戳、扩展属性的档案还原未提供。没有自动缓存清理/完整容量规划或物理读取强制取消。
- 当前为本机开发包装，dylib 实际需要 macOS 26。自包含分发、旧版系统、Sandbox/书签、公证、SMAppService/XPC helper 和 GUI 实盘写入/Windows 验收仍未完成。
