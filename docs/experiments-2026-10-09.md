# 2026-10-09 实验记录

当前结论：**WINDOWS_USER_CONFIRMED_PASS + MAC_WINDOWS_ROUNDTRIP_MOUNT_PASS**。本张 Windows 资源管理器 master 模式创建的 UDF 1.02 DVD-R 已通过 Windows 可见性人工验收，以及 Windows 再追加后的 Mac 读回验证。2026-10-10 用户另报告 CD-R 连续三次测试通过。Windows IOCTL/哈希脚本未执行；原 GP70 Pro 和其他新型号没有物理刻录验收。本版已取消型号白名单，见第 12 节。

## 1. 设备与工作范围

发现：初始沙箱内 drutil 空输出；解除执行环境限制后能看到真实光驱。用户明确授权以当前 Lenovo Ultraslim DVD XL6M / DVD-R 为实验基准，替代原 GP70 Pro / CD-R。

证据：`artifacts/physical/run-gublMG` 与后续 run 的 drutil/INQUIRY/minfo。设备为 Removable CD-ROM，介质为 DVD-R sequential recording，已完成 1 Session/1 Track，appendable；msinfo `0,93952`；完整 Session leadout 65264；下一空白区起点 93952。minfo 的 “number of sessions=2” 包含下一空白 Session，不能直接作为已刻录 Session 数。

下一步：始终重扫型号、介质和真实 NWA；不把动态 `/dev/disk4` 当作写入目标。

## 2. libisofs 原始路线

发现：本机 API 支持 preferjoliet、appendable、ms_block、旧文件 sections。生成时旧 source 读取计数为 0；已有文件不重新写入。

证据：`artifacts/offline/result.txt` 和 `commands.log`。20 项离线断言覆盖中文、长名、大小写、多级目录、空文件、1 MiB 旧文件、第二次 continuation、源镜像绝对偏移映射、冲突与损坏拒绝。独立 `isoinfo -N 12128 -J -f` 可解析。该来源是合成 Joliet-only 镜像，不能当作 Windows 实测。

下一步：遇到真实 ISO/Joliet 盘时继续该路线；当前盘使用 UDF 路线。

## 3. 真实盘的实际格式

发现：用户以 Windows 资源管理器 master 模式刻录的当前盘不是 ISO/Joliet；不能要求用户改变操作来适配库。

证据：换盘前后都通过 readcd 读到 LBA 16 BEA01、17 NSR02、18 TEA01，16–47 未观察到 CD001 PVD。当前 `run-ZmCWBp` 可完整列出 UDF 文件树；`run-eNkyxX` 进一步读取 LVID。LVD 的 UDF revision=0x0102，type-1 map，单一只读物理分区，起点 288。

| 当前旧文件 | 大小 | 数据绝对 LBA |
| --- | ---: | ---: |
| JDBC Connection [HikariProxyConnection@3.txt | 49926 | 295 |
| README.md | 21569 | 320 |
| swagger.json | 246137 | 331 |

只读取名称/extent，不在日志输出旧文件内容。旧数据总量约 310 KiB，track 的 127 MiB 用量包含刻录布局/填充，不能据此推断文件大小。

下一步：按原操作生成的 UDF 1.02 实现 continuation，保留以上 extent。

## 4. UDF 实现与离线验证

发现：libudfread 能导入当前实际目录和 extent。新的 C++ UDF writer 保留旧数据 LBA，重写目录/FE；新文件放在新 Session。旧内容在 generator 阶段读取次数为 0。

证据：当前盘 metadata cache 生成的 `artifacts/current-dvd-next.iso` 共 577 blocks，在 NWA 93952；三个旧文件与三个新文件通过 metadata/extent/新 hash 验证。`artifacts/udf-tests/result.txt` 包含 14 项 UDF 离线断言，覆盖二次追加、旧/new extent、中文层级、CRC、备用 VDS、父 FID、分区几何、数据损坏、冲突和覆盖拒绝，并检查新的生成结果为 16 块对齐。

本机 `mkisofs -udf` 合成 fixture 的三汉字目录名出现 NUL 损坏；解析器正确拒绝。对照 fixture 改用已验证的二汉字旧目录名，新 writer 仍测试三汉字目录。该失败只说明本机 mkisofs fixture 路径有问题，不用于判断 Windows 格式。

普通 Debug 和 AddressSanitizer/UBSan 构建的两套测试均通过。尚未把该结果当作物理或 Windows 验收。

## 5. Disk Arbitration 故障记录

发现：早期真实 SIGINT 故障测试在清理时卡住，两次需要用户执行 CONT。不能宣布已可靠解决 Ctrl+C。

证据：用户两次确认执行恢复命令；之后 ps 确认 `diskarbitrationd` 为 Ss，相关故障测试进程已清理。修改后 `artifacts/guard-synthetic.txt` 四项虚拟进程测试通过：exception、lease writer 继承、SIGINT、SIGKILL。

下一步：生产路径父进程直接 CONT，watchdog 共享 release、5 秒有界清理、90 秒失联恢复。真实服务故障测试已禁用；保留未验收限制。

## 6. 真实追加与 Windows 验收

第一次真实 burn：`artifacts/physical/run-M7voaZ`。旧 metadata 经新的 readcd cache 比较，三个旧文件已建立 SHA256 基线；镜像生成/验证通过。cdrecord 在激光写入前返回 255：ADIP 容量错误为 736 blocks，`Blocks current=-93216`，`Data does not fit on current disk`。实际 NWA=93952，drutil/minfo 剩余=2203936。这属于后端容量判定失败，尚不是文件系统或 Windows 挂载失败。服务恢复正常。

调查证据：官方 `cdrtools-3.02a09.tar.gz` SHA256 `c7e4f732fb299e9b5d836629dadf5512aa5e6a5624ff438ceb1d056f4dcb07c2`，与 Homebrew 配方一致。`drv_dvd.c` 用零 rzone size 回退到错误 ADIP；`cdrecord.c` 容量检查的 toolarge 分支允许 `F_IGNSIZE` 绕过。驱动看到 multi 标志会选择 variable packet + incremental 模式，无需重新实现 MMC backend。

已限制 `-ignsize` 的启用范围：同一已确认设备/DVD-R，错误 ADIP 小于 NWA，minfo/drutil 可写空间一致，准备镜像和余量不超出实际边界。

第二次真实 burn：`artifacts/physical/run-7LAhRP`。cdrecord exit=0；在 LBA 93952 写入 1181696 bytes / 577 sectors，写入耗时 25.961 秒，fixate 1.664 秒。紧接的 drutil 检查暂时未识别成 DVD-R，旧版 CLI 终止。稍后只读 drutil 确认 DVD-R、Sessions=2、Tracks=2、appendable、剩余 2197184 blocks。没有重试写入。此失败属于写后就绪检测，已增加最多 30 秒的状态重读与独立 `readback` 命令。

真实读回：`artifacts/physical/run-LyStUD`，readback exit=0。msinfo `93952,100704`；Track 2 为 LBA 93952–94543 / 592 blocks（577 个镜像块 + 15 个 drive padding）。新 cache 逐字节比较 577 个镜像块 SHA256 `1eac529ac74de0eac2bf21b99f77b534ade82a5e0000947c69a14117a8478951` 相同；三个旧文件及三个新文件全部 SHA256 相同；严格 UDF metadata 及 libudfread 复读一致。result 为 PHYSICAL_READBACK_PASS / WINDOWS_ACCEPTANCE_PENDING。服务为 Ss，实验进程已退出。

从实际 Track padding 发现尾部两个 Anchor 相对物理 N 偏移 15 块；NWA+256 的主 Anchor 正确，Mac/libudfread 已读取成功，之后用户也确认 Windows 能挂载。后续生成已改为 16 块对齐，让 N 和 N-256 的冗余 Anchor 与物理 Track 末尾一致，离线断言通过。**当前已刻录的第二 Session 仍是原 577 块镜像**，没有为这项改进再消耗 Session；最新对齐版本未再次进行物理刻录。

## 7. Windows 最终可见性与双向追加

发现：2026-10-09 用户完成 Windows 人工验收，原话为“牛逼，Windows已经能看到这三个文件了。我又从Windows往回写了个bat文件，macos上也能看到”。三个待验收新文件为 `hello-from-macos.txt`、`中文测试.txt`、`中文目录/MixedCase.txt`。因此本张 DVD-R 的 Windows 最新 Session 可见性已通过，不再要求改变首次刻录方式。

证据：返回 Mac 后，先从系统挂载的 `/Volumes/10 09 2026` 读取全部文件，六个第一/第二 Session 文件的大小与 SHA256 全部匹配刻录前基线；新 BAT 为 `无限试用Navicat.bat`，500 bytes，SHA256 `1e9e82f83a71e5ce616471b3fbeade573c876ac6a6a0c3fec3d7baf2940aae75`。只读取字节用于 hash，没有执行 BAT。记录位于 `artifacts/windows-roundtrip/mac-mounted-readback.json`，result 为 MAC_WINDOWS_ROUNDTRIP_MOUNT_PASS。

随后 CLI 只读扫描 `artifacts/physical/run-taqeFU`：确认 Lenovo Ultraslim DVD XL6M / DVD-R，3 Sessions / 3 Tracks，appendable，msinfo `100704,107440`，剩余 2190448 blocks。TOC/minfo 显示 Windows 新 Track 3 为 LBA 100704–101279，共 576 blocks；下一空白区从 107440 开始。通过 readcd + libudfread 读取第三 Session 的 UDF 树：

| 文件 | 当前数据绝对 LBA | 与原数据地址关系 |
| --- | ---: | --- |
| JDBC Connection [HikariProxyConnection@3.txt | 295 | 第一 Session 地址不变 |
| README.md | 320 | 第一 Session 地址不变 |
| swagger.json | 331 | 第一 Session 地址不变 |
| hello-from-macos.txt | 94259 | 第二 Session 地址不变 |
| 中文测试.txt | 94260 | 第二 Session 地址不变 |
| 中文目录/MixedCase.txt | 94261 | 第二 Session 地址不变 |
| 无限试用Navicat.bat | 101005 | Windows 写入第三 Session |

本次扫描结束后确认 `diskarbitrationd` 为 Ss，扫描进程已退出，没有写入第四 Session。

结论：本张盘已完成 Windows Session 1 → macOS appendcd Session 2 → Windows 可见新文件 → Windows Session 3 → macOS 读取全部文件的实测闭环，并保持可追加。Windows 的第二次追加继续引用 macOS 生成的目录树及数据 extent，没有丢失前两次文件。

证据边界：Windows 可见性与 Windows 再追加操作由用户确认；Windows 端 GET_LAST_SESSION、TOC、文件 hash 脚本没有执行。Mac 端所有六个旧/新文件的 hash 和第三 Session TOC/extent 为本机实读。原始 GP70 Pro/CD-R、真实服务的最新 SIGINT/SIGKILL 修复、16 块对齐新版 writer 的物理刻录仍未验收。

下一步：CLI PoC 可继续用于已支持的当前 DVD-R 布局；扩大介质/设备或 UDF 布局范围时分别验收，不能把本次结果推断为所有 CD-R/DVD-R 都兼容。

## 8. 用户继续测试：Windows 第三 Session 后再用 Mac 追加失败

发现：用户提供 `burn ~/Desktop/Burn --work-dir /tmp` 的终端输出。UDF continuation 为 784 blocks，起点 108240；七个旧文件 hash 与两个新文件的验证通过，生成阶段旧 source 读取为 0。cdrecord 却打印 `Starting new track at sector: 0`，WRITE(10) 的 CDB 地址为 0，设备返回 Illegal Request / 21h 02h（invalid address for write），exit=254，`error after 0 bytes`。close track/session 又返回 72h 04h（empty or partially written reserved track）。这是刻录后端写入地址失败，不能归因于 UDF 名称或镜像内容。

证据：用户输出保存在 `artifacts/append-failure-after-windows/user-terminal.txt`。本机随后只读扫描 `artifacts/physical/run-UdL0su`，确认当前仍 3 个完整 Session，drutil Tracks=5，appendable，最后有效起点 100704，NWA=109040。minfo 显示已完成物理 Track 1/2/3，以及 Track 4（107440–108223）与 Track 5（108240–109023）两条各 784 块的预留 Blank Track；下一空白区为 Track 6。两次失败预留区域已改变盘布局，不能以“0 payload bytes”断言盘没有任何变化。

定位依据：本机 TOC 报 `first: 1 last 2`，但 minfo 显示已有 3 条完成的数据 Track。已校验来源的 cdrtools 3.02a09 `cdrecord.c` 使用 `read_tochdr` 的最后 track 号加 1 作为待写 trackno；`drv_dvd.c::next_wr_addr_dvd` 的 multi 路径随后用该编号查询 READ TRACK INFORMATION，并未检查查询返回值和 NWA valid。这条路径会选到已有 Track 3，能够解释为什么预检 `-msinfo` 的正确 NWA 与实际写入地址 0 不一致。该根因判断尚未通过修补后端后的物理实验验证。

本次调查没有写盘，结束后确认 Disk Arbitration 为 Ss。第二 Session 的 Windows 可见性成功记录仍成立，但第四 Session 的再次追加失败，当前版本不能据此声称任意轮次的 Windows/Mac 交替追加可靠。

下一步：修正 cdrecord DVD track 编号/NWA 获取，并在无法确认真实物理目标 Track 的情况下提前拒绝写入；修复前不要重复 `burn`，也不把 `burnfree` 的通用提示当作解决方案。

## 9. 三次重试后的 Track/NWA 修复与预留区恢复

发现：用户明确同一 burn 命令共运行三次。只读物理查询确认两条预留区完全空白：Track 4 start=107440/size=784，Track 5 start=108240/size=784，均 reserved=1、blank=1、damage=0、LRA invalid、free=size、NWA valid 且等于起点。Track 6 为未预留尾区，NWA=109040/free=2188848；Session status=1，first=4/last=6。证据：`run-Iylomk`、`run-zcCHKa`。不能因没有写入 payload 而断言三次命令没有改变盘布局。

修复：从已校验的官方 cdrtools 3.02a09 源码编译隔离、标明 unofficial 的后端，保留 Homebrew 工具。待写 Track 改由 READ DISC/TRACK INFORMATION 的物理编号决定，强制 NWA valid 和 expected LBA 校验。普通 burn 在 unfinished Session 时于预留新 Track 之前拒绝；真实拒绝记录为 `run-h1csi3`。源码构建脚本和实际策略分别位于 `scripts/build-cdrecord.py`、`backend/appendcd_backend.h`。

构建/访问实验：官方源码 SHA256 与 Homebrew 配方一致。旧 argv/frame scanner 在 AppleClang 21 下失效，使用上游 NO_SCANSTACK 编译选项；使用实际 MacOSX26.5 SDK。IOKit local state 的 calloc 初始化修复没有解决设备访问失败；IOServiceAuthorize 实验返回 kIOReturnBadMessageID 0xe00002c6，已撤销。自编译后端在 Documents 下通过原生管理员执行器打开 IOKit plugin 遭拒绝；同一二进制在 /private/tmp、随后独立用户 ~/.local/libexec/appendcd 下访问成功。此结果是路径/执行环境差异的实测，不把具体 TCC 根因当作已经证明。

恢复准备：Desktop/Burn 当前包含 xlsx（362975 bytes）及 .DS_Store（6148 bytes），暂存至临时输入目录。`run-EreGj8` 只读准备通过：从完整第三 Session UDF 树合并文件，起点 107440、精确 784 块，7 个旧文件 hash 和 2 个新文件 hash 通过；生成期间旧 payload 读取为 0。没有跳过预留区去写尾部 NWA 109040。

实际写入：`run-relptw`，后端 dvd2 预选物理 Track 4，实际 WRITE 起点正确为 107440，1605632 bytes/784 sectors 全部写完，耗时 13.665 秒，没有再次 RESERVE TRACK。CLOSE TRACK 5 成功，但检查错误要求 LRA valid，后端 exit=1，Session 尚未关闭。只读 `run-hftY9s` 确认 Track 4 已完整记录（LRA=108223），Track 5 已关闭/填零（reserved=0、blank=0、free=0、NWA invalid、LRA invalid/0）。这是关闭后的状态解释错误，不能再次 recover 写同一批文件。

受控补关闭：增加 recover-finish，从光盘验证全镜像 SHA256 和全部文件 hash，再逐块验证 Track 5 全零。`run-NdXFEH` 两项通过；dvd3 因上游 -fix 同时设置 F_WRITE，误入空白 Track 选择器，关闭前退出，没有写入 payload。dvd4 专用关闭分支绕开该选择器；`run-ByDguj` cdrecord-finalize exit=0，fixate 14.928 秒。关闭后为 4 Sessions/5 Tracks、appendable，msinfo=107440,115184；物理下一 Track 6 位于 115184，Session status=0，未预留、blank、NWA valid，free=2182704。此前预留 Track 已计入 Tracks，所以完成恢复不再增加 Track 数。

补验流程曾复用 after-read 缓存目录，exclusive-create 拒绝。现将关闭前/关闭后的 cache 分开；没有因此重复刻录。实际光盘的后续独立 readback 结果见下文。

验证：Debug 四组 CTest（ISO/Joliet、UDF、实际后端策略 mock、恢复镜像/物理状态）通过。ASan/UBSan 四组通过；macOS 不支持 detect_leaks=1，该配置启动失败后改为 detect_leaks=0，未完成 LeakSanitizer 验证。真实服务 SIGINT/SIGKILL 故障验收仍未执行，不扩展此结论。

独立读回：`run-lyjB3a` 的 readback exit=0，PHYSICAL_READBACK_PASS。从实际光盘读取 784 块，与准备镜像 SHA256 `90cf25d212d918cd502c522bd1483ed6700bbca1075374bab9ffdb8dd645ca2b` 完全一致；7 个旧文件和 2 个新文件 hash 全部匹配。xlsx hash 为 `6403bf7362a608cb2bb89ad1d8885bccf3ec8dc8a665301a35cee33ae72981e0`。状态为 4 Sessions/5 Tracks、appendable、无 unfinished Session、msinfo=107440,115184。正常和错误清理路径结束后确认 diskarbitrationd 为 Ss；只实际写过一次恢复 payload，随后仅补关闭和读取，没有再累加新的预留 Track。

Windows 最终验收：用户把恢复后的盘插回 Windows，明确反馈“Windows 能看到并打开新增 Excel，旧文件也在”。第四 Session 的人工可见性/文件打开验收通过，说明本张包含填零 Track 的恢复 Session 被 Windows 正确选择。记录位于 `artifacts/recovery-after-windows/acceptance.json`。没有执行 Windows IOCTL 或自动 hash 脚本；这些项目不能标成已验证。

结论：本张 DVD-R 的 Windows 首次刻录 → Mac 追加 → Windows 再追加 → Mac 三次失败重试 → 修补后端恢复 → Mac 全镜像/文件读回 → Windows 新 Excel 可见且可打开闭环完成。已经消耗的预留区与 Session 开销不会退回；用户同一批文件已写入，后续 burn 应使用新增文件目录。正常下一 Track 的物理标志和后端策略已验证；恢复后的再次正常 payload burn 尚未另消耗 Session 测试。原始 CD-R、其他光驱/介质和真实服务故障注入仍不在这次验收结论内。

下一步：用户继续测试当前限定光驱与 DVD-R；扩大设备/介质白名单前分别验收。没有 GUI 开发。

## 10. 2026-10-10：自动排除新增 Finder 缓存

发现：用户 `run-JHHNNH` 在 UDF 文件合并阶段遇到 `.DS_Store` 同名冲突，尚未调用物理写入；Disk Arbitration 已 CONT。Finder 缓存不应成为待追加文件。

修复：ISO/Joliet 和 UDF 的递归目录合并都在名称冲突检查之前跳过名为 `.DS_Store` 的新增普通文件；旧 Session 中已导入的同名文件及其 extent 保持。其他文件、同名覆盖和 symlink 的限制不变。

证据：重新编译 `build/appendcd`，四组 CTest 全部通过。两条文件系统的回归 fixture 都包含光盘上的旧 `.DS_Store`、新增根目录同名缓存和新增子目录缓存；生成 continuation 后确认旧缓存的名称/大小/LBA 不变，子目录的新缓存未进入镜像，普通新增文件及旧内容验证仍通过。此阶段没有访问光驱或刻录。

下一步：使用只包含本次新增用户文件的目录继续测试普通 burn；原来已刻录的 Excel 等同名文件仍会按追加策略拒绝覆盖。

## 11. 2026-10-10：按用户要求开放 CD-R 实验入口

发现：用户要求去掉仅 DVD-R 的限制，尝试 CD-R。当前同一 Lenovo Ultraslim DVD XL6M 中确实为 appendable CD-R，1 Session/1 Track。Windows 第一 Session 的 TOC 为 mode=2，msinfo 为 `0,12160`，invisible Track 2 的 start/NWA=12160、free=347683、blank=1、reserved=0、NWA valid。

实现：保留型号/固件与介质身份核对，允许 CD-R 和 DVD-R；CD-R 走官方 MMC invisible Track 0xFF 查询、独立 expected NWA 校验，最终参数为 `-tao -multi -xa -nopad speed=10`。修补后端升级为 `3.02a09-appendcd-media5`。DVD-R 预留 Track 恢复和容量绕过继续只用于 DVD-R。命令形式不变，不需要 CD 专用开关。

证据：`run-8vhFCt` 为真实只读 inspect，`run-zxN2NQ` 为 list，`run-yAD9TI` 为 build。当前首 Session 同时含 ISO9660/Joliet（LBA 16–18）与 UDF recognition（19–21），CLI 按现有优先级导入 UDF 1.02。两个旧文件分别为 JDBC 文本（49926 bytes / LBA 300）、README.md（21569 bytes / LBA 325）。`artifacts/current-cd-next.iso` 从真实 NWA 12160 开始，592 blocks，加入两个小测试文件；旧 extent 不变，旧 source generation reads=0，`.DS_Store` 没有加入。独立 `appendcd verify` 通过，镜像 SHA256 为 `ad6ff0d434d4bcdac3efbe2c0e3cae3a18cf596a97d63031de8d717cb228fafe`。这次 build 没有建立旧 payload hash 基线；burn 会在写前额外建立并在光盘读回时核对。

测试：Debug 和 ASan/UBSan 各四组 CTest 全部通过。新增后端策略测试验证 CD NWA invalid、expected LBA 不符、未完成 Session、packet 模式、截短 Track 信息、误用 DVD recovery 的拒绝；DVD 恢复原测试继续通过。操作结束 drutil 仍为 1 Session/1 Track、appendable，diskarbitrationd 为 Ss，没有写盘。

边界：CD-R 实际 burn、TAO padding 后的 UDF 尾 Anchor、Windows 选择最新 Session 尚未验证。DVD-R 的先前 Windows 验收仍成立；不能推断 CD-R 已成功。原 GP70 Pro 光驱仍未开放。

下一步：用户使用原 burn 命令测试当前 CD-R；成功返回后把实验盘插回 Windows，检查旧文件与本次新增文件是否同时可见/可打开。发生刻录后错误时先查看日志或只读 readback，不重复盲目 burn。

## 12. 2026-10-10：取消光驱型号白名单，保留目标与介质检查

发现：用户反馈“三次测试都通过”，随后要求自动识别光驱及 CD-R/DVD-R、使用各自写入方式、保留写入前检查。实际当前 CD-R 为 4 Sessions/4 Tracks，最新 Session 27628、下一写入地址 35506、free=324337 blocks、appendable。原第一 Session 的两个文件及三次追加的新文件均可读取目录记录；用户反馈没有附 Windows IOCTL/自动 hash 报告。

实现：增加 `appendcd drives`，通过 IOKit 光学服务枚举取得动态厂商/产品/固件、稳定设备 ID、registry path 和 whole optical media BSD 节点。单驱自动选择，多驱要求 `--device`，禁止使用普通 `/dev/diskN` 或未知/远程目标；型号相同的两个光驱按独立 ID 区分。Apple drutil 的状态、能力及 XML 路径必须指向同一光学设备。卸载前再核对 BSD 节点归属；后端 media6 在创建 optical user client 前核对期待的 registry ID。保留 MMC media profile、appendability、Track/Session、NWA、容量、metadata 重读、旧文件 hash 基线和写后读回。CD-R 使用 TAO/XA，DVD-R 使用 incremental data；速度从当前设备/介质报告的支持列表选择。

路径实验：直接调用 DiscRecording 的 BSD lookup 从 Documents 下返回空，原生管理员执行也失败；相同程序复制到 /private/tmp 后成功。最小实验还证明需先初始化设备表。没有为了此问题改变系统安全设置，最终路线使用可正常工作的 Apple drutil 查询能力，再核对它的 IOKit registry path。该执行路径差异已实测，具体系统权限根因未完全定位。

盘片对象实验：`run-p2Qv4m` 在导入前被新增的 IOMedia registry ID 比较拒绝，服务恢复正常，没有写入。IORegistry 显示获得/释放 SCSI 独占访问会重建 IOMedia，光驱服务 ID 仍为 4295019020。修正为绑定稳定的光驱服务 ID；IOMedia ID 仅作为诊断信息，盘片继续由实际 MMC profile/geometry、metadata、内容 hash 等校验，不把暂态 kernel 对象 ID 当作永久盘片身份。

证据：`run-Jg8sru` 是完成版自动识别的实际 inspect，`run-2syVlT` 是显式 `--device 1,0,0` 的 build。在真实 NWA 35506 生成 `artifacts/generic-drive-next.iso`，592 blocks；五个旧文件继续引用原 extent，新增一个 57-byte 测试文件位于 LBA 35810，生成期间旧 source reads=0。严格 build 验证及独立 verify 通过，镜像 SHA256 为 `dadbbe0259f9cbe0524818c69bb58d85d13086d136ab425194ac3e748335114f`。build 没有建立旧 payload hash；实际 burn 会按保留的流程建立并读回验证。

拒绝实验：真实后端设置错误的 `APPENDCD_DEVICE_REGISTRY_ID=1`、仅执行 `-inq`，exit=255，明确返回 `optical registry identity changed; refusing device access`，在创建 user client 前拒绝。`inspect --device /dev/disk0` 在停止 Disk Arbitration/卸载设备前拒绝。合成测试覆盖零/单/多光驱、相同型号的显式选择、身份/固件/地址变化、BSD 节点或能力 XML 路径不匹配、不支持目标盘型/CD-TAO、空白盘、非 Removable CD-ROM、CD/DVD 参数区别以及设备支持的速度选择。

验证：Debug 和 ASan/UBSan 各五组 CTest 全部通过，media6 `--resume` 再次构建通过，源码构建/runtime/provenance 的二进制 SHA256 相同。保持已有 DVD 恢复和 CD NWA 策略测试；正常和拒绝路径均观察到服务恢复。此次开发只扫描/读取和生成文件，没有写新的 Session。

最终目录复读：`run-Hw9JS9` 通过新增的卸载前归属核对并列出五个文件；最后 drutil 仍为 4 Sessions/4 Tracks、appendable，diskarbitrationd 为 Ss，无残留实验进程。开发验收记录与两套完整 CTest 日志保存在 `artifacts/drive-discovery/`。

边界：多台真实光驱同时连接、新型号实际刻录及 Windows 验收尚未补做，不能把型号白名单取消解释为所有设备/固件均已验证。真实服务 SIGINT/SIGKILL 故障注入仍不在本次测试范围。

下一步：单驱继续使用原 burn 命令；多驱先运行 drives，再使用实际发现的 --device 地址。新增光驱按各自日志、物理读回和 Windows 挂载结果逐一记录，不需要重新修改品牌白名单。


## 13. macOS 首刻入口与 DVD-R 反向追加实验（2026-10-10）

发现：现有 CLI 只支持已有完整 Session 的追加。为避免新功能改变已通过验收的路径，新增显式 `init` 和离线 `init-build`；原 `burn` 不自动接管空白盘。首刻是 LBA 0 的 ISO9660 + Joliet standalone image，未生成 UDF bridge。没有导入旧文件系统，也没有旧 extent。

保护：修改前五组 CTest 全部通过，源码、CLI、后端及旧测试证据备份为 `artifacts/baseline-before-init/20261010-093521/baseline.tar.gz`，配套 SHA256。可独立运行的旧 CLI 为同目录 `appendcd-append-only`。追加后端 media6 SHA256 仍为 `588515140fb7e5f74dc064f31f561a6a96be8178cda5f414e0f16ada44e63d17`。first-session initial7 单独部署到 `appendcd-initial-cdrecord`，SHA256 为 `c8c69b90718bdeb02dee95810b0a3f1be9e1df6e3334f2eeb339a0ec4c4c424e`；原 runtime/provenance 未替换。原追加生成循环和 UDF writer 未更改。

离线证据：六组 Debug 与 ASan/UBSan CTest 全部通过，日志在 `artifacts/initial-development/`。macOS 不支持 LeakSanitizer，本次禁用该项，不能声称泄漏检查通过。新增 `initial_session` 检查初始 manifest、PVD/SVD、path tables、中文/大小写/长名/多层/空文件、数据 hash、独立 isoinfo、`.DS_Store` 排除、首刻后两轮 continuation、旧 extent 保持和旧 payload 不复制；对覆盖输出、空输入、symlink、非法 initial 旧引用进行拒绝检查。本机文件系统无法同时表示 Case.txt/case.txt，输入目录内该项单独记 SKIP，已有旧导入树与新文件的大小写冲突测试仍运行。

读前试验：用户换入空白 DVD-R RITEKF1，drutil 不输出 Sessions/Tracks，blank 且 used=0。`run-umau13` 实测 MMC profile=0x11、disk_status/session_status=0、Track 1、NWA 0、无 reserved/damage。`-atip` 与 `-minfo` 通过 upstream write parameters 选择不同模式，空白 DVD 容量分别为 incremental 2297888 与 streaming 2298496；代码使用较小容量，不自行推测 NWA。

被否定的实现：`run-utoJPl` 生成镜像后在写前复查拒绝 Missing/duplicate physical Track record，未调用实际写入后端、没有预留区或新 Session。根因是复查复用 DiscInfo 时把新 Track 记录累加到内存中的旧快照。修正为清空旧 Track 列表，新增连续三次 applyInitialProbe 的合成回归；之后真实 drutil 确认仍为空白、used=0，服务 Ss。这是扫描快照错误，不是光盘布局/写入失败。

实盘证据：`run-zfStxv` 用三个小文件 `hello-from-macos.txt`（25 bytes）、`中文首刻.txt`（40 bytes）、`中文目录/MixedCase.txt`（39 bytes）生成 68-block image。initial7 使用 DVD `-multi -data speed=4`，实际起点 0；upstream 最小 Track padding 至 300 sectors，payload read/written=139264/614400，exit=0。写入 28.681s，fixate 67.867s。镜像 SHA256 为 `7a31b6ad593940a895477306b0c9e6e40050e6b36899acc97ffa857866918b19`。

写后检查：1 Session / 1 Track、latest LBA 0、NWA 93952、free 2203936、appendable。PVD LBA 16、SVD LBA 17、Volume Space Size=68，Joliet root LBA 23；三个数据 extent 为 LBA 33/34/35。使用新的 readcd cache 从实际光盘读回 68-block 镜像，整镜像 SHA256 及三个文件 SHA256 全部匹配；TOC/Session 完成、下一 Track 2 的有效 NWA 与容量检查通过。光盘物理 Track 的 padding 不计入 ISO Volume Space Size。

原流程兼容证据：保存的旧 CLI 与新 CLI 都从 `run-zfStxv/after-read/disc-cache.iso` 的真实光盘读回生成 NWA 93952 的 continuation，并通过各自 verify。三个旧数据 extent 不变，generation old-source reads=0，新镜像仅加入 `append-regression.txt`。这两份 continuation 只用于文件验证，没有写入额外 Session。对已完成首刻盘再执行 init，真实在卸载或暂停服务之前拒绝 `init requires a blank disc`；随后 drutil 仍 1/1，diskarbitrationd Ss，无残留进程。

首刻结束时的阶段结论：MAC_INITIAL_DVDR_READBACK_PASS，Windows 首次读取、资源管理器 master 模式追加及返回 Mac 读回当时待验收，后续结果见第 14 节。空白 CD-R 首刻只完成实现与离线/策略检查，尚未实盘刻录；不能将 DVD 结果扩大为 CD 验收通过。`artifacts/initial-development/result.json` 汇总最新边界与证据。

当时下一步：用户将这张盘带到 Windows，读取三个首刻文件，再按原资源管理器 master 流程添加小文件且保持可追加；返回 Mac 后检查最新 Session、目录和首刻内容 hash。若 Windows 拒绝追加，保留原报错并对照 IMAPI 导入/输出文件系统布局，不能只用 Windows 可读来宣称双向完成。

## 14. Mac 首刻 → Windows master 追加 → Mac 读回完成（2026-10-10）

发现：用户明确反馈 Windows 能看到三个首刻文件，已从 Windows 追加 `swagger.json` 并将盘插回。Windows 可见性及资源管理器 master 操作的证据来源是用户实际操作；没有采集 Windows IOCTL 或 Windows 自动 hash 报告。

物理证据：返回盘为同类 DVD-R RITEKF1、2 Sessions / 2 Tracks、仍 appendable。CLI 实际扫描/目录读取 `run-WCfm5x` 得到 msinfo=93952,100272，free=2197616，Session 已完成，下一物理 Track 3 blank、NWA valid。Windows 完成的物理 Track 2 是 93952–94111（160 blocks），ISO Volume Space Size=148；两者的 12-block padding 差异是实际布局信息，不应改写 PVD 为物理 Track 长度。

文件系统证据：最新 Session 为 ISO9660 + Joliet，未观察到 UDF VRS。PVD=93968，SVD=93969；ISO root=93973/250 bytes、path table L/M=93971/93972，Joliet root=93977/304 bytes、path table L/M=93975/93976。`swagger.json` 大小 246137、数据 LBA 93979，SHA256=`4f9bf438f233b913b77a3c071eea3a8607c11eaf0dfa253535f58b22b1a8d4f4`。三个首刻文件仍引用 LBA 33/34/35，文件名、大小和初始 hash 全部一致。

实际光盘读回：只读实验工具 `artifacts/initial-roundtrip/audit.cpp` 链接原 core，未修改生产 CLI/reader/writer。先从盘读回第一 Session 的全部 68 个 ISO blocks，其 hash 与首刻基线 `7a31b6ad593940a895477306b0c9e6e40050e6b36899acc97ffa857866918b19` 一致；再从最新 Joliet 树读取全部四个文件，比较三个旧文件的原 hash 与 extent。使用全新第二份 readcd cache 重新读取最新 148-block ISO 及旧文件区域，独立镜像 hash、PVD/SVD、双端序 L/M path tables、目录和全部文件 hash 再次通过。Swagger 没有取得 Windows 原始源文件 hash，因此新文件结论限定为实际盘上两次读取一致，不能声称与未提供的 Windows 源文件比对过。

最新 ISO hash=`15b24a2ba59e5d171892dfecb3f78544ed2eada50581dc0fdaf02450b8838732`。结果、两份 sparse optical cache、最新 ISO、manifest 与文件预期清单保存在 `artifacts/initial-roundtrip/readback`；复制后使用现有 `appendcd verify` 再次通过，验证三个 old 文件 hash 与一个 new 文件。验收汇总为 `artifacts/initial-roundtrip/acceptance.json`，result=MAC_WINDOWS_MAC_DVDR_ROUNDTRIP_PASS。源码保护再次核对：原 ISO continuation 生成函数、UDF writer/reader、服务 guard、身份与进程工具、verifier/normalizer 及已验收追加后端 hash 均保持不变。

环境边界：原生管理员读取 Documents 中旧 manifest 遭遇 TCC 拒绝，发生在暂停系统服务和光驱访问前，记录为 `documents-read-refusal/failure.txt`。将已经验证的首刻基线复制到 `/private/tmp/appendcd-initial-roundtrip-c7gqdxdr` 后只读实验成功；结束时归档到项目目录，保留 sparse 数据空洞。未修改系统安全设置。

最终状态：drutil 仍 2/2、appendable，diskarbitrationd 为 Ss，无残留实验进程。本次返回盘验收没有写入任何新的光盘 Session。当前这张 DVD-R 已完成 Mac 首刻、Windows 读取并追加、Mac 读取新旧文件的闭环；空白 CD-R 首刻及其他设备仍待各自实盘验收。下一步可按需要继续原 `burn` 追加流程，当前不需要重刻或重复 `init`。

## 15. CD-R：Mac 首刻与实际光盘读回（2026-10-10）

发现：用户已放入空白 CD-R，要求验证 Mac 首刻后 Windows 能读取并继续追加。`run-0hAyKZ` 只读检查确认 Lenovo Ultraslim DVD XL6M、当前 IOKit 光驱 ID 4295021238、CD-R、used=0。MMC disk_status/session_status=0，初始 Track 1 blank、非 reserved/damaged、NWA valid、start/NWA=0、free=359843；TAO 能力及盘速均通过。没有根据动态 `/dev/disk4` 编号盲写。

保护：本次没有修改或重新编译生产代码，也没有替换后端。刻录前后对源码、CLI 和两个后端重新核对 SHA256，一致。media6 仍为 `588515140fb7e5f74dc064f31f561a6a96be8178cda5f414e0f16ada44e63d17`，initial7 仍为 `c8c69b90718bdeb02dee95810b0a3f1be9e1df6e3334f2eeb339a0ec4c4c424e`，完整记录在 `artifacts/cd-initial/staging.json`。

证据：`run-CXtlQp` 实际调用独立 initial7，以 `-multi fs=8m -tao -xa -nopad speed=10` 在 LBA 0 写入同样的三个小文件。ISO9660 + Joliet image 为 68 blocks，SHA256=`60548b4864b21827b38956ac88fc319cf128d012ac3a55a25068aa8152126f58`。上游最小 Track 填充写入 300 sectors，payload read/written=139264/614400，exit=0；writing=14.175s、fixating=26.306s。实际 TOC Track 1 control=4、mode=2、start=0、lead-out=302；minfo 已写 Track 报 300 blocks。ISO Volume Space Size 保持 68，不按物理 Track/padding 扩大。

读回：操作后 1 Session / 1 Track、latest=0、msinfo=0,11702、free=348141、appendable。下一物理 Track 2 blank、非 reserved/damaged，start/NWA=11702、NWA valid。用全新 readcd cache 从实际光盘读回 0–67 blocks，整镜像与准备镜像逐字节/hash 一致。PVD LBA 16、SVD LBA 17、ISO root=19、Joliet root=23、L/M path tables 及目录通过既有 verifier；三个文件仍为 LBA 33/34/35，全部源 fixture SHA256 匹配。另从实际缓存用 libisofs Joliet 列表及逐文件 hash 核对通过。

旧路径复核：修改前保留的 append-only CLI 与当前 CLI 都从该实际读回、真实 NWA 11702 生成一份 61-block continuation 并各自 verify 通过；三个旧 extent 未变，generation old-source reads=0。两个结果只写入项目 artifact 文件，没有写入第二个光盘 Session。无生产代码变化，因此没有重复执行同一套先前已通过的六组 CTest。

首刻阶段清理：当时实际 drutil 为 1/1、appendable，diskarbitrationd=Ss，无残留刻录/readcd/watchdog 进程。证据汇总为 `artifacts/cd-initial/acceptance.json`，当时阶段结果 MAC_INITIAL_CDR_READBACK_PASS_WINDOWS_PENDING；后续 Windows 及返回盘结果见第 16 节。

当时下一步：用户将这张 CD-R 插入 Windows，检查并打开 `hello-from-macos.txt`、`中文首刻.txt`、`中文目录/MixedCase.txt`，按原资源管理器 master 流程追加小文件并返回 Mac。首刻阶段不能仅凭 Session 数宣布双向 CD-R 完成。

## 16. CD-R：Windows 读取并追加，返回 Mac 验收通过（2026-10-10）

发现：用户明确反馈 Windows 能看到并打开三个首刻文件，又从 Windows 追加 `swagger.json`，已插回 Mac。Windows 读取/文件打开和资源管理器 master 追加是用户实际操作证据；Windows IOCTL 和源文件 hash 报告未采集。

物理证据：`run-BrYIsl` 实际只读扫描为 Lenovo Ultraslim DVD XL6M、CD-R、2 Sessions / 2 Tracks，仍 appendable，msinfo=11702,18904，free=340939。重新连接后的光驱服务 ID 为 4295022013，由当次 IOKit 发现并核对，不沿用首刻的 ID。下一 invisible Track 3 blank、非 reserved/damaged、NWA valid、start/NWA=18904，Session 已完成。Track 1 和 2 的 TOC 均为 control=4、mode=2，最新 Track start=11702、TOC lead-out=12004；minfo 最新已写 Track 为 11702–12001（300 blocks）。不把物理 Track padding 计入 ISO Volume Space Size。

文件系统证据：Windows 第二 Session 仍为 ISO9660 + Joliet，未观察到 UDF VRS。Volume Space Size=148，PVD=11718、ISO root=11723/250 bytes、path table L/M=11721/11722（24 bytes）；SVD=11719、Joliet root=11727/304 bytes、path table L/M=11725/11726（26 bytes）。三个首刻文件沿用 LBA 33/34/35。新 `swagger.json` 为 246137 bytes、LBA 11729，SHA256=`4f9bf438f233b913b77a3c071eea3a8607c11eaf0dfa253535f58b22b1a8d4f4`；没有 Windows 原始源 hash，结论为两次独立实际光盘读取一致。

读回证据：只读实验工具 `artifacts/cd-initial-roundtrip/audit.cpp` 单独编译并链接原 core，没有修改生产 reader/writer/CLI。首刻基线暂存 `/private/tmp/appendcd-cd-initial-roundtrip-m49ne58d`，避免此前管理员 Documents 读取限制。两份全新 readcd cache 分别读取第一 Session 的完整 68 blocks，以及最新 148-block ISO 和全部文件；两次首刻完整镜像 hash 均为原值 `60548b4864b21827b38956ac88fc319cf128d012ac3a55a25068aa8152126f58`。三个旧文件的名称、大小、extent 和基线 SHA256 全部不变；新文件两次 hash 一致。既有 verifier 对 PVD/SVD、双端序 path tables、目录结构、绝对地址及全部文件 hash 两次通过。

归档证据：最新 ISO SHA256=`6d2c02194fd730709c0da73ba15f9dd8b4ded3903eb990d888a4e107ce7e4841`。两份 sparse cache、最新 ISO/manifest、TOC/msinfo/MMC probe 和文件预期清单归档到 `artifacts/cd-initial-roundtrip/readback`，保留空洞并把 manifest 的 sourcePath 指向持久缓存。现有 CLI 对归档文件 verify 再次通过；额外核对缓存有效块列表、首刻/最新镜像逐字节及四个文件 hash 均通过。结果汇总为 `artifacts/cd-initial-roundtrip/acceptance.json`，MAC_WINDOWS_MAC_CDR_ROUNDTRIP_PASS。

清理与保护：最后 drutil 仍 2/2、appendable，diskarbitrationd=Ss，无残留光盘实验进程。本次光盘写入次数为 0，生产源码、CLI、media6/initial7 两个后端的 SHA256 与首刻前记录均一致；没有重复执行未变代码的六组既有 CTest。

结论与下一步：当前 Lenovo 光驱上的 CD-R 和 DVD-R 都已完成 Mac 首刻、Windows 读取/追加、Mac 读回新旧文件的闭环。现有盘若需继续 Mac 追加可使用原 `burn`；无需重复 init。其他光驱/固件仍按其各自实盘结果验收，当前不扩展为所有设备的通用成功结论。
