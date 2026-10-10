# appendcd CLI PoC

原生 macOS / Apple Silicon 的 CD-R / DVD-R multisession 实验。**自动识别光学光驱，无品牌、型号或固件白名单**。`burn` 追加到已有完整 Session 的盘，独立的 `init` 入口用于空白盘首次刻录。单光驱自动选择；多个光驱必须通过 `--device` 指定目标。保留设备身份、写入能力、介质和可写地址检查。CLI 项目名保留 `appendcd`。

独立的 [原生 GUI 本机使用版 1.1.1](gui/README.md) 提供自包含 `.app`、用户输入快照、光盘文件导出、任务归档与系统认证。当前指定 CD-R 已完成 GUI 两次追加/实际读回、Windows 七文件正常打开及再次追加、返回 Mac GUI 八文件实际读取/导出；同路径文件可明确选择跳过或替换。返回盘发现的 Session 地址矛盾和 31-block 短卷导入已修复，六组 CLI 回归、136 条 GUI 断言与专项离线测试通过。原 `build/appendcd` 和两个 runtime 保持基线，未替换文件仍重用旧 extent。最新产物、证据及未完成项见 [验收报告](gui/evidence/write-acceptance-20261010/acceptance.md)，[GUI 制作指南](docs/gui-development-guide.md) 保留历史契约与开发约束。

新 Git checkout 请先阅读 [仓库范围与重新构建](docs/repository.md)，恢复必要基准输入，并把当前源码 CLI 构建到独立 `build-cli/`。大缓存、完整实盘任务及用户输入不在 Git 中；验收摘要中的历史路径不保证随 clone 存在。

本项目同时实现两条文件系统路径：

| 首次刻录的实际格式 | 追加路径 | 旧文件数据 |
| --- | --- | --- |
| ISO9660 + Joliet | libisofs 优先导入 Joliet，生成 ISO9660 + Joliet continuation | 原有绝对 LBA 不变 |
| Windows 资源管理器 master 模式的当前 UDF 1.02 DVD-R | libudfread 导入 UDF 名称/extent，C++ 生成 UDF 1.02 continuation | 原有绝对 LBA 不变 |

**反向 DVD-R 实验也已通过：Mac `init` 创建第一 Session，用户确认 Windows 看到了三个文件并用资源管理器 master 模式追加 `swagger.json`，返回 Mac 后实际光盘读回验证通过。** 当前为 2 Sessions / 2 Tracks、latest=93952、NWA=100272、appendable。首刻完整镜像保持原 hash，三个旧文件的数据 LBA 33/34/35 及 SHA256 均未变；`swagger.json` 为 246137 bytes，两次独立光盘读取 hash 一致。证据为 `artifacts/initial-roundtrip/acceptance.json`，详见实验记录第 14 节。Windows IOCTL/自动 hash 报告未采集。

**历史 CLI 的 CD-R 反向实验也已完整通过：Mac 首刻 → Windows 读取并打开三个文件 → Windows master 追加 `swagger.json` → Mac 实盘读回。** 当时实验盘为 2 Sessions / 2 Tracks、latest=11702、NWA=18904、appendable。两次独立 readcd 读取确认首刻完整镜像未变，三个旧文件的 LBA 33/34/35 和 SHA256 均保持；新增 Swagger 为 246137 bytes、数据 LBA 11729，两次哈希一致。最新 Session 为 ISO9660 + Joliet，metadata/path tables 与全部文件验证通过。返回盘验收没有再写入 Session，也没有修改生产源码或两个后端。证据为 `artifacts/cd-initial-roundtrip/acceptance.json`，详见实验记录第 15–16 节。

**当前 DVD-R 实验的关键验收已通过：用户确认 Windows 能看到 macOS 追加的三个文件，随后又从 Windows 追加 BAT，Mac 能读到。** 返回 Mac 后实测 3 Sessions / 3 Tracks，仍 appendable；六个新旧文件 SHA256 全部与第二 Session 基线一致。当前盘的 UDF 格式属于本项目要处理的输入，不要求用户改变 Windows 资源管理器的首次刻录操作。

真实第二 Session 读回证据位于 `artifacts/physical/run-LyStUD`；Windows 再追加后的 TOC/extent 位于 `artifacts/physical/run-taqeFU`，挂载文件 hash 与人工验收记录位于 `artifacts/windows-roundtrip`。这是本张 DVD-R 的实测结果。2026-10-10 用户另反馈 CD-R 连续三次测试通过；当前只读确认该 CD-R 为 4 Sessions / 4 Tracks、appendable。该反馈未附 Windows IOCTL 或哈希报告，其他型号的实测结果仍需分别记录。

**后续三次失败重试留下的预留区也已恢复，并通过 Windows 验收。** 修补后的后端填满已有 Track 4、关闭空白 Track 5 和第四 Session；Mac 独立读回全部 9 个文件通过，仍 appendable。用户随后确认 Windows 能看到并打开 `816测试_66_10000航迹点.xlsx`，旧文件也在。当前完成状态为 4 Sessions / 5 Tracks，最新 Session LBA 107440、下一写入 LBA 115184。这批文件已写入，不要再对同一批文件运行 burn。证据见 `artifacts/recovery-after-windows/acceptance.json` 和实验记录第 9 节。

## 构建

依赖均来自 Homebrew：

```sh
brew install cdrtools libisofs libudfread cmake pkgconf smake
python3 scripts/build-cdrecord.py --output-dir build/backend \
  --runtime-dir "$HOME/.local/libexec/appendcd"
python3 scripts/build-cdrecord.py --initial --output-dir build/initial-backend \
  --runtime-dir "$HOME/.local/libexec/appendcd"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DCDRECORD_PATH="$HOME/.local/libexec/appendcd/appendcd-cdrecord"
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

本机默认 macOS 27 SDK 与当前 linker 不匹配，实际使用以下配置编译通过：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk \
  -DCDRECORD_PATH="$HOME/.local/libexec/appendcd/appendcd-cdrecord"
cmake --build build -j 4
```

测试机：arm64，macOS 26.6；AppleClang 21；libisofs 1.5.8.pl02；libudfread 1.2.0；cdrtools 3.02a09。当前使用隔离的 `3.02a09-appendcd-media6` 修补版，保留 DVD-R 修复和 CD-R NWA 校验，并在打开写入设备之前核对 IOKit registry ID；CMake 拒绝原始或旧版 cdrecord 作为写入后端。源码下载来自官方发布，SHA256 与 Homebrew 配方一致，生成的 `provenance.json` 记录源码和二进制 hash。未替换 Homebrew 工具。已有源码目录再次构建时使用 `--resume`。

本机原生管理员执行路径直接从 Documents 下运行自编译后端曾遭遇 IOKit plugin 拒绝；相同二进制移到独立用户 libexec 目录后已能访问光驱，因此实际运行使用上面的路径，不依赖临时目录中的二进制。此目录和修补源码需与 CLI 一起保留。

首次刻录使用独立的 `appendcd-initial-cdrecord`（initial7）。原 `appendcd-cdrecord`（media6）及其 runtime/provenance 未替换，`burn/recover/recover-finish` 继续使用原后端。开发前的源码、CLI 和旧测试证据备份位于 `artifacts/baseline-before-init/20261010-093521/baseline.tar.gz`，并配套 `sha256.json`。本次增加第六组首刻/后续追加回归测试；首刻测试不代表 Windows 已验收。

## CLI

以下命令在项目根目录运行，物理操作通过用户终端的 `sudo` 取得权限：

```sh
./build/appendcd drives
# 空白 CD-R/DVD-R：首刻前先生成离线镜像检查，也可直接 init。
./build/appendcd init-build ./fixtures/macos-first ./session-first.iso
./build/appendcd verify ./session-first.iso
sudo ./build/appendcd init ./fixtures/macos-first --work-dir artifacts/physical
sudo ./build/appendcd inspect --work-dir artifacts/physical
sudo ./build/appendcd list --work-dir artifacts/physical
sudo ./build/appendcd build ./fixtures/macos-session2 ./session-next.iso \
  --work-dir artifacts/physical
./build/appendcd verify ./session-next.iso
# 已写入但即时就绪检查失败时，只读补验；不会写入更多 Session：
sudo ./build/appendcd readback ./session-next.iso --work-dir artifacts/physical
sudo ./build/appendcd burn ./fixtures/macos-session2 --work-dir artifacts/physical
# 同义的快捷命令：
sudo ./build/appendcd ./fixtures/macos-session2 --work-dir artifacts/physical
# 仅针对失败后留下的 1–2 条完全空白预留 Track，先准备并验证：
sudo ./build/appendcd recover ./new-files --prepare-only --work-dir artifacts/physical
# 实际填满第一条预留 Track，并处理空白 Track/关闭 Session：
sudo ./build/appendcd recover ./new-files --work-dir artifacts/physical
# 仅在 recovery 数据已写完且空白 Track 已关闭时，使用原镜像补做 Session 关闭：
sudo ./build/appendcd recover-finish artifacts/physical/run-XXXXXX/session-next.iso \
  --work-dir artifacts/physical
```

`build` 只生成文件；`burn` 在本次操作中重新扫描、生成、验证和刻录，随后从光盘读回。`verify` 自动读取同名 `.manifest`，不能删除它后仅凭 ISO 文件验证 extent 来源。普通 `burn/build` 遇到未完成 Session 会在预留新 Track 之前拒绝，不能通过重复运行来恢复。

同一条 `burn` 命令支持当前光驱里的可追加 CD-R 和 DVD-R，无需额外开关。`burn` 必须已有一个完整 Session；空白盘使用 `init`，已刻录盘上的 `init` 会在卸载或暂停系统服务前拒绝。`recover` 和 `recover-finish` 仍仅适用于限定的 DVD-R 预留 Track 布局，不能用于 CD-R。

`init` 生成从 LBA 0 开始的 ISO9660 + Joliet 独立镜像，不导入旧文件系统。首次刻录也使用对应介质的 `-multi`，完成当前 Session 并保留整盘可追加。它不生成 UDF bridge；当前 Lenovo 光驱的 CD-R 和 DVD-R 均已完成 Windows 读取、资源管理器 master 追加和返回 Mac 的实际读回验收，其他设备仍须分别测试。首刻文件为 `hello-from-macos.txt`、`中文首刻.txt` 和 `中文目录/MixedCase.txt`。现有 `windows-verify.ps1` 要求旧/新文件同时存在，适用于追加验收，不直接用于仅有首刻 Session 的清单；`artifacts/initial-roundtrip/readback/windows-session2-expected.json`（DVD-R）和 `artifacts/cd-initial-roundtrip/readback/windows-session2-expected.json`（CD-R）可用于对应盘第二 Session 的进一步 Windows IOCTL/hash 采集。

只有一个光驱时，使用原命令即可：

```sh
sudo ./build/appendcd burn ~/Desktop/Burn --work-dir /tmp
```

存在多个光驱时，先运行 `./build/appendcd drives`，再使用其中的实际地址，例如：

```sh
sudo ./build/appendcd burn ~/Desktop/Burn --device 1,0,0 --work-dir /tmp
```

`--device` 接受本次发现的 `bus,target,lun` 地址；不接受 `/dev/diskN`、远程 SCSI 或未发现的目标。即使两个光驱型号相同，也按独立 IOKit ID 区分。运行过程中地址重分配、设备断开/重新插入或身份变化会被拒绝。

两条文件系统路径均自动排除新增目录及其子目录里的 `.DS_Store` 普通文件，避免 Finder 缓存触发同名冲突或占用新 Session。旧 Session 中已经存在的 `.DS_Store` 仍保留原引用；其他同名文件仍按追加策略拒绝覆盖。

`recover` 的目标 LBA/大小只从实际物理 Track 信息读取，不使用尾部 NWA 跳过预留区；新镜像必须精确填满第一条预留 Track，文件过大就拒绝。`recover-finish` 没有文件写入参数，只在完整镜像、新旧文件 hash 及全部 padding 块都从光盘读回验证后关闭 Session。无法通过这些检查的中断写入不属于自动恢复范围。恢复不会取回已经消耗的 DVD-R 空间。

macOS 原生管理员执行器在本次实测中可以运行二进制、写实验输出，但受到 Documents 目录读取限制。真实测试的小文件已暂存到 `/private/tmp/appendcd-poc-files`。这不改变文件系统算法；不要把 TCC 拒绝误判为刻录故障。

离线接口接受显式的真实或合成 LBA，不根据镜像长度猜测下一写入地址：

```sh
./build/appendcd list --source ./disc.iso --last 0
./build/appendcd build ./new-files ./continuation.iso \
  --source ./disc.iso --last 0 --next 12128
./build/appendcd verify ./continuation.iso
```

若输入仅包含一个 Session，从 `readcd` 提取的文件需加 `--source-base 原始绝对LBA`。UDF 读取仅对 VRS 和第一个 Anchor 查找入口做 Session 重定向，分区和文件地址仍使用绝对 LBA。

## 物理操作与证据

程序只枚举 IOKit 光学服务，通过其子级 whole optical media 取得 BSD 节点，再用 `drutil -drive N status/info` 核对该节点、registry path、当前盘型、写入能力和 appendable 状态。卸载前再次检查节点仍属于选定光驱。cdrecord 每次打开设备时核对同一 registry ID，随后 INQUIRY 的厂商/产品/固件必须与本次发现的身份相符；不要求具体型号。写入前再次核对介质 profile、Session、NWA、容量、Track 标志以及重新读回的 metadata/content。代码不会固定使用 `/dev/disk4`，也不会把普通磁盘加入候选集。

获得/释放 SCSI 独占访问会重建 IOMedia，其 registry ID 仅记录用于诊断，不能作为盘片不变的判据；稳定绑定的是光驱服务 ID。底层地址使用本机 cdrtools 3.02a09 IOKit transport 的实际枚举编号，不根据 drutil 的设备顺序或型号猜测。

只暂停已经运行的 `diskarbitrationd`。清理时父进程直接发送 `CONT`，独立 watchdog 负责异常退出恢复，并通过共享 release 状态避免只依赖 pipe EOF；失联 90 秒后会终止登记的 backend 再恢复服务。真实 SIGINT 故障测试的旧实现曾两次卡住并由用户恢复。因此 **最新修复尚未通过真实服务的 SIGINT/SIGKILL 故障验收**，不能承诺绝对恢复。真实服务故障注入已禁用；四项虚拟进程故障测试已通过。

紧急恢复命令：

```sh
sudo kill -CONT "$(pgrep -x diskarbitrationd)"
```

每次物理运行保存 `artifacts/physical/run-*`：

- before/after 的 drutil、INQUIRY、minfo、TOC、msinfo、ATIP、fulltoc 结果；
- 文件系统识别块、readcd 缓存及 `.blocks` 有效块清单；
- continuation、manifest、刻录日志、Windows 预期文件名/大小/SHA256；
- `result.txt` 或具体 `failure.txt`。

DVD-R 刻录使用 `cdrecord -multi -data fs=8m`，不使用 CD-ROM XA。CD-R 使用 `-tao -multi -xa -nopad fs=8m`，将 2048 字节镜像块写为 CD-ROM XA Mode 2 Form 1；这里的 `-xa` 不是要求 2056 字节输入的 `-xa1`，见 [官方 cdrecord man page](https://cdrtools.sourceforge.net/private/man/cdrecord/cdrecord.1.html)。CD-R 查询 invisible Track（0xFF）的有效 NWA，与 `-msinfo` 及 expected LBA 比较；DVD-R 仍查询实际 RZone 编号，不混用地址路径。速度从选定光驱和当前介质报告的列表中选择：支持时保留已实测的 CD 10x / DVD 4x，否则选报告的最低整数速度。

对本机已证实的 DVD-R cdrtools “zero media size → ADIP 736 blocks” 缺陷，只有错误容量小于 NWA、drutil/minfo 剩余容量一致且镜像在实际边界内时才加入 `-ignsize`，CD-R 不启用此绕过。这是对容量误判的限定调试绕过，不授权超出实际剩余空间；完整警告保留在日志。写入前重新读取已导入的 metadata，检查介质几何和型号，再读取旧文件内容建立 SHA256 基线。旧文件只用于校验，**不写入 continuation**。

写后就绪检测会重读状态最多 30 秒，避免 fixate 刚返回时的临时 No Media。普通追加必须满足 Session/Track 各增加 1；恢复填满已计数的预留 Track 时只要求完成 Session 增加 1、Track 数保持。两种路径都要求 appendable、无 unfinished Session、新起点等于镜像起点；全新 readcd 缓存验证 continuation 字节及新旧文件 SHA256。缓存缺块会报错，不会把 sparse hole 当作读回成功。

## Windows 验收

本次人工验收已通过：Windows 显示 `hello-from-macos.txt`、`中文测试.txt` 和 `中文目录/MixedCase.txt`；Windows 又追加 BAT，返回 Mac 后实际读出所有七个文件。第三 Session 的 msinfo 为 `100704,107440`，六个已有文件仍引用第一、第二 Session 的原数据 LBA，BAT 位于 LBA 101005。证据来源分别为用户反馈、Mac 挂载后的 SHA256 和 CLI 的独立光盘目录读取。

若后续需要采集 Windows IOCTL 与文件 hash，可把同一张盘插回 Windows，复制该 Session 对应的 `windows-expected.json` 和 `scripts/windows-verify.ps1` 到 Windows 本地目录。管理员 PowerShell 中运行（D: 替换为实际光驱盘符）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\windows-verify.ps1 `
  -Drive D: -ExpectedJson .\windows-expected.json -Output .\windows-result.json
```

该脚本只读光盘、将结果写到本地文件，不永久修改 execution policy。它检查 Windows 挂载得到的原始名称、大小和 SHA256，采集 `IOCTL_CDROM_GET_LAST_SESSION`、TOC/Full TOC 以及新 Session 的描述符。所有新旧文件匹配且 Windows 返回的 last-session LBA 正确才报告 `WINDOWS_ACCEPTANCE_PASS`；仅文件匹配、IOCTL 不可用时返回待验收状态。

Windows 脚本尚未在 Windows 执行，未取得 Windows 端 IOCTL/哈希报告；本次 Windows 可见性结论来自用户实际操作确认。`run-LyStUD/windows-expected.json` 对应第二 Session，期望 LBA 93952；当前盘已有第四 Session，不能再用该旧清单验收最新 Session。`windows-joliet-fixture.vbs` 是可选的 ISO/Joliet 对照实验工具，**不是当前资源管理器 master 模式的必需前置条件**。

第四 Session 恢复的 Windows 人工验收同样已通过，最新预期清单为 `artifacts/physical/run-lyjB3a/windows-expected.json`，期望 LBA 107440。用户已确认新 Excel 可见且可打开、旧文件仍在；Windows 端 IOCTL/自动 hash 脚本仍未执行。Mac 全镜像和 9 个文件 hash 为实际光盘读回证据。

## 限制与交付状态

- 当前 UDF writer 支持实测的 UDF 1.02、一个只读物理分区、type-1 partition map、常规文件。拒绝 UDF 1.50 VAT、2.x metadata/sparing map、内嵌/稀疏文件等尚未实现的布局。新增单文件须小于 1 GiB。
- `init` 的首刻镜像独立实现，当前只输出 ISO9660 + Joliet。当前 Lenovo 光驱的 DVD-R 和 CD-R 均已完成 Mac 首刻、Windows 读取并追加、返回 Mac 再读回；最新状态见实验记录第 14、16 节。其他光驱仍需分别实盘验收。
- UDF 输入生成 UDF continuation；ISO/Joliet 输入生成 ISO9660 + Joliet continuation。当前 UDF 分支未同时生成 ISO/Joliet bridge。
- 名称使用原文件系统树作为权威；Windows 大小写/Unicode 冲突、同名旧文件替换、symlink 和特殊文件会失败。当前单个名称最长 103 UTF-16 units；尚未验证所有 Windows 长路径边界。
- `libisofs` 实测生成的 Joliet `..` extent 有缺陷，本项目只修正新镜像的这些记录和空文件占位地址，再独立验证。不会修改 Homebrew 库或旧盘。
- 当前 Lenovo XL6M 的 DVD-R 已完成前述 Windows 人工验收；CD-R 连续三次测试通过由用户反馈。放开型号后的版本在这台光驱上通过实际扫描和镜像 build/verify，证据见 `artifacts/physical/run-Jg8sru`、`run-2syVlT`。本次开发没有消耗新的 Session；其他型号、多台真实光驱同时连接仍未完成物理刻录验收，选择和身份拒绝逻辑已通过合成测试。
- 当前 bridge 输入同时有 UDF 和 ISO/Joliet 时优先走 UDF 树，生成 UDF continuation。不能把一次设备上的结果推断为所有固件或盘片布局都支持；不支持写入能力、TAO、有效 NWA、已实现文件系统布局的情况会拒绝。
- Windows 第三 Session 后的原后端按模拟 TOC 编号选错物理 Track，误向 LBA 0 写入并留下两个空白预留 Track。修补后端改用 READ DISC/TRACK INFORMATION 的物理编号，检查 NWA valid 和预期 LBA；增加限定恢复与关闭流程。修复的真实读回和 Windows 验收状态见实验记录第 9 节；原失败记录保留在第 8 节。
- Windows 最终结果、原理及各次失败证据见 [实验记录](docs/experiments-2026-10-09.md) 和 [实现说明](docs/implementation.md)。

完整源码位于 `src/`，CMake 配置位于根目录；离线验证位于 `tests/`，小测试文件位于 `fixtures/`。本次没有建立 Git 仓库或发布远端代码。
