# 实现与地址模型

## 空白盘首次刻录

`init-build` 通过新文件 `IsoInitial.cpp` 创建空的 libisofs image，使用与追加相同的文件名/冲突/普通文件检查，生成 LBA 0 的 ISO9660 + Joliet 镜像。`iso_write_opts_set_appendable(0)` 表示没有旧 extent 重用的 standalone image，`iso_write_opts_set_ms_block(0)` 决定首次地址；光盘后续可追加由物理后端的 `-multi` 保证。没有修改原 `buildSession` 的选项或数据生成循环。初始 manifest 仍兼容 v2，只有 start/importedStart/sourceBase 均为 0、sourcePath 为空、ISO/Joliet、所有条目均为 new 才接受初始地址，不能用它表达旧 extent。

`init` 在 native 光驱/盘型/能力检查之后，必须由 drutil 确认 blank、已用块 0、没有已完成 Session/Track，再由独立 initial7 后端核对 MMC profile（CD-R 0x09 / DVD-R 0x11）、disk_status=0、session_status=0、初始 Track 1、blank、非 reserved/damaged、有效 NWA 0、起点 0、剩余容量。空白 DVD-R 的 drutil 不输出 Sessions/Tracks 字段；只有上述 blank 与使用块条件成立才把缺失计数视为 0。容量取 drutil 与 DVD incremental Track 信息的保守边界。写前重新探测全部边界，写后复用已有完整光盘读回及 TOC/Session 验证。

已通过验收的 media6 二进制保持原 hash，原 `burn/recover/recover-finish` 路径继续使用它。新 initial7 单独命名、构建和部署，仅在 `init`/空白 `inspect` 打开。即使给 initial7 传入首刻标志，它也拒绝 disk_status=1 或非零初始 NWA；未传入标志的原追加策略继续拒绝 blank/NWA 0。两条入口互不自动转换。

本机实测 `-atip` 与 `-minfo` 会通过上游 write parameters 选择 incremental/streaming 模式，空白 DVD-R 报告容量分别为 2297888/2298496 blocks。这些查询未写光盘数据。首刻以较小容量限制镜像，不能把模式切换后的容量变化误认为新 Track 或自行推测 NWA。重复写前复核必须清空内存中的旧 Track 列表再解析新记录。

Windows IMAPI 对 [ImportFileSystem](https://learn.microsoft.com/en-us/windows/win32/api/imapi2fs/nf-imapi2fs-ifilesystemimage-importfilesystem) 按 UDF/Joliet/ISO9660 顺序导入，并检查 [FileSystemsToCreate 的布局兼容性](https://learn.microsoft.com/en-us/windows/win32/api/imapi2fs/nf-imapi2fs-ifilesystemimage-put_filesystemstocreate)。当前 DVD-R 已取得用户实际 Windows 操作结果：三个首刻文件可见，资源管理器 master 模式成功追加 `swagger.json`。第二 Session 仍为 ISO9660 + Joliet，三个旧文件引用原 LBA 33/34/35，Swagger 数据从 LBA 93979 开始。返回 Mac 后，首刻完整镜像 hash、三个旧文件 hash/extent、最新 metadata/path tables 和两次独立光盘读回均通过。CD-R 在同一 Lenovo 光驱也已完成相同闭环：Windows 能看到并打开三个首刻文件，再用 master 模式追加 Swagger；返回 Mac 实测 2 Sessions / 2 Tracks、latest=11702、NWA=18904、appendable，两个 Track 的 TOC 均为 control=4/mode=2。两次独立读取确认首刻 68-block 完整镜像和旧文件 LBA/hash 不变；最新 ISO 为 148 blocks，新文件从 LBA 11729 开始，完整 hash 与 metadata/path tables 均通过。CD-R 证据为 `artifacts/cd-initial-roundtrip/acceptance.json`。两种介质的 Windows 源文件 hash/IOCTL 报告未采集；新增 Swagger 验证限于实际盘上的两次读取一致。新型号仍需分别验收。

## 两种权威目录

ISO9660/Joliet 输入调用本机 `/opt/homebrew/include/libisofs/libisofs.h` 的实际 API：

```cpp
int iso_read_opts_set_preferjoliet(IsoReadOpts *, int);
int iso_write_opts_set_appendable(IsoWriteOpts *, int);
int iso_write_opts_set_ms_block(IsoWriteOpts *, uint32_t);
int iso_file_get_old_image_sections(IsoFile *, int *, iso_file_section **, int);
```

read start block 为 `cdrecord -msinfo` 的第一个值。启用 preferjoliet，并拒绝没有 Joliet 的 ISO 输入；实际测试中刻意让 Rock Ridge 名称与 Joliet 名称不同，导入结果仍为 Joliet 名称。旧 sections 数组按头文件要求用 `free()` 释放。新目录和兼容 ISO 短名由 libisofs 生成，不建立旧 ISO 短名与 Joliet 名称的猜测映射。

UDF 输入使用本机 `libudfread 1.2.0` 的 `udfread_open_input/readdir/file_size/file_lba`，读取实际 UDF 名称与数据的绝对块地址。当前 Windows 资源管理器 master DVD-R 为 UDF 1.02，只读物理分区从 LBA 288 开始。UDF writer 克隆该卷的格式描述信息，生成新的目录和 FE，旧 FE 中的数据 allocation descriptors 转换成同一分区下的相同物理 extent。当前没有内嵌文件；有内嵌文件时明确拒绝，避免偷偷复制旧 payload。

## 地址

| 名称 | 作用 |
| --- | --- |
| last session start | 读取上一完整 Session 的描述符和文件树 |
| next writable address / ms_block | 新 continuation 的物理首块 |
| ISO 文件偏移 0 | 对应物理 NWA，不对应光盘 LBA 0 |
| 旧数据 extent | 原绝对 LBA，保持不变 |
| 新目录/数据 extent | 必须落在新 continuation 的物理范围 |

ISO 路径将 NWA 传给 `iso_write_opts_set_ms_block`，并启用 appendable。PVD 位于 NWA+16，Joliet SVD 位于后续 descriptor block。生成镜像不包含旧 payload。libisofs 默认 continuation 的 Volume Space Size 为本次 Session 的 block count；绝对 extent 则包含 ms_block。实测与官方 `ecma119.c` 一致，不擅自改成总盘大小，也不使用给可覆写介质准备的 overwrite buffer。

UDF 路径保留原只读物理分区起点，把最新 PD 的分区长度扩展到新 Session 已分配内容的末尾；新目录的 short_ad 使用 `绝对LBA - 分区起点`。历史 Session 之间的不可读间隙没有文件 extent 引用。新版 Anchor 分别位于 NWA+256、本镜像 N-256 和 N；主/备用 VDS 指向新 Session 的描述符；新 LVD 指向新 FSD 和关闭状态的 LVID。该布局依据 [Microsoft IMAPI multisession layout](https://learn.microsoft.com/en-us/windows/win32/imapi/imapi-multisession-layout)，字段、tag checksum 和 CRC 依据 [ECMA-167](https://ecma-international.org/publications-and-standards/standards/ecma-167/)。本次 Windows 挂载由用户实际确认，并再次用 Windows 追加；返回 Mac 的第三 Session 保留六个已有文件的原数据地址与 hash。详见 [实验记录](experiments-2026-10-09.md)。此结果限定为本次实验盘，解析器通过本身不能替代其他介质的 Windows 验收。

## libisofs 的实测缺陷

本机 1.5.8 的 Joliet writer 在生成 `..` 时，先计算当前目录的 block/length，再把 node 指向 parent，导致 `..` 仍引用自身。官方 master 的 `joliet.c` 同一段逻辑可解释本机观察结果，ECMA-119 writer 的顺序则正确。`GeneratedMetadata.cpp` 根据新目录树修正 parent 的 extent/size；只有值为自身或正确 parent 时才接受，不覆盖未知损坏。空文件无需旧数据块，非法占位地址统一指向经检查全零的尾块。

验证器独立检查 PVD/SVD、双端序字段、目录记录、`.`/`..`、L/M path tables、extent、名称和 SHA256；再让 libisofs 重新导入新镜像。UDF 验证器检查 Anchor、主/备用 VDS、LVID、FSD、每个 FE/FID 的 tag/CRC/location、父引用、link count、allocation descriptor、partition span，再由 libudfread 独立重读目录和 extent。

## 读回与恢复

不使用 `isoinfo` 的直接 libscg 光驱访问。所有物理读取由 `readcd sectors=begin-end` 完成，端点按实测的 end-exclusive 语义处理，并校验输出字节数。metadata cache 使用绝对 LBA 的 sparse 文件，配套有效块清单，禁止将未读的洞当作零数据。

`burn` 先用全新的 cache 比较准备期间的旧 metadata，再读取旧内容的 hash 基线；刻录之后又建立新的 cache，对新 Session 全字节和所有文件做验证。这些读取是验证，不是把旧内容重新写入新 Session。

## Windows 再追加后的 DVD 后端修补

Windows 完成第三 Session 后，本机模拟 CD TOC 的 last track 为 2，实际完成的 DVD Track 有 3 条。cdrtools 原代码根据 `read_tochdr` 推导 trackno=3，再读取已完成 Track 的无效 NWA，导致实际 WRITE(10) 地址为 0。`-msinfo` 使用另一条物理 Track 查询路径，所以预检正确并不能证明最终写地址正确。

隔离修补版保留 cdrecord 的 IOKit/libscg、write、mode page、OPC、flush、close 实现，只改目标选择与状态校验。物理 Track 号来自 READ DISC INFORMATION 的最后 Track 字段；READ TRACK INFORMATION 返回值、编号、NWA valid、blank/reserved/damage、容量和 expected LBA 都需符合限定条件。最终 writer 使用 `appendcd-nwa=...`，写入前和 open_track 后均检查实际起点，拒绝从模拟 TOC 推断 DVD 待写 Track。编译时用官方源码 hash、`NO_SCANSTACK` 兼容当前 Clang；保留上游版权/许可证。DVD-R 实测恢复使用 unofficial `3.02a09-appendcd-dvd4`，media5 增加下述 CD-R 分支，当前 media6 再加入光驱身份绑定。

当前盘的两条失败预留 Track 分别为 107440/784 和 108240/784，尾部 NWA 为 109040。它们属于同一个未完成 Session；普通 `burn` 必须拒绝这个布局。`recover` 从上一完整 UDF 树生成起点 **107440**、精确 **784** 块的镜像，不使用尾部 109040 作为恢复镜像起点，不再次 RESERVE TRACK。UDF 的尾部 Anchor 也随 784 块真实边界定位。填满第一条预留 Track 后，cdrecord 现有 CLOSE TRACK 命令负责填零并关闭其他已证实为空的预留 Track，然后用 multi 参数关闭 Session。

本机对完全空白 Track 的 CLOSE 返回：reserved=0、blank=0、free=0、NWA invalid，但 LRA 仍 invalid/0；不能把它误当作填零没有完成。若数据写入和 Track 关闭已完成，而 Session 关闭被中断，`recover-finish` 必须用原镜像/manifest 读回比较全镜像 SHA256、新旧文件 hash，并逐块验证后续 padding Track 全零，才允许 cdrecord `-fix -multi` 关闭 Session。`-fix` 在上游也设置 F_WRITE，因此该专用分支必须绕过“空白待写 Track”选择器。它没有 image 参数，不会重复写文件或预留新 Track。

恢复的物理验证要求 completed Sessions 增加 1、预留 Track 已计入的 Tracks 数保持、最新起点匹配镜像、下一物理 Track 为正常空白可写区。最新 Session 尚未关闭时，普通 `readback` 不报告完成。本次第四 Session 已完成 Mac 全镜像/所有文件光盘读回，用户也确认 Windows 能看到并打开新 Excel、旧文件仍在；这次包含 padding Track 的 Session 人工验收通过。其他布局的 Windows 选择行为仍需实测。

恢复逻辑包含 signal flag、RAII、直接 CONT、共享 release、独立 watchdog、backend 进程组登记和 heartbeat。真实故障测试曾失败，根因未完全定位；pipe 继承只是覆盖的失效场景，不是已证实的根因。现禁用真实服务故障注入，最新修复只通过虚拟进程故障测试及正常/错误路径的真实服务恢复观察。

## CD-R 实验分支

介质 profile 必须为 0x09（CD-R）。按照本机官方 `drv_mmc.c::next_wr_addr_mmc` 的查询方式，从 READ TRACK INFORMATION 的 invisible Track 0xFF 取得 NWA；不套用 DVD 的 RZone 选取和恢复逻辑。要求可追加、Session 已完整或下一 Session 为空、Track blank、NWA valid、非 reserved/damaged/packet/fixed-packet，容量取 drutil 与 raw Track 信息的较小值。实际 CD TOC 的最后 Track 与 drutil 已完成 Track 数须一致，输出的待写编号为其下一条。

最终使用 `-tao -multi -xa -nopad`，输入块大小为 2048 字节，光盘 Track 为 XA Mode 2 Form 1。写前校验 expected NWA，open_track 后再次校验实际 NWA；不启用 DVD 容量绕过或预留区恢复。参数语义依据 [官方 cdrecord man page](https://cdrtools.sourceforge.net/private/man/cdrecord/cdrecord.1.html) 及校验过 hash 的本机源码。保留 cdrecord 的 CD lead-in/lead-out 和 Session 关闭实现。

当前 CD-R 首 Session 是 Windows 创建的 ISO9660/Joliet + UDF bridge，现有入口优先导入 UDF 树。真实只读导入和起点 12160 的 UDF continuation 生成/验证通过，旧文件 LBA 300/325 不变，generator 没有读取旧 payload。之后用户报告三次 CD-R 测试通过，本机只读确认为 4 Sessions/4 Tracks、latest=27628、NWA=35506、appendable；没有取得 Windows IOCTL/自动 hash 报告。

## 自动光驱发现与写前绑定

`OpticalDrive.cpp` 只枚举 `IOCompactDiscServices`、`IODVDServices`、`IOBDServices`，取得厂商/产品/固件、稳定 registry ID/path 及其子级 whole optical media 的 BSD 节点。普通存储设备不进入候选集。单个光驱自动选择；多个光驱时必须指定 `--device bus,target,lun`，即使厂商/产品相同也按 ID 区分。上游 IOKit `iokit_open` 枚举循环内外都递增 idx，因此本版保留其实际 target=2*index 编号；不根据 drutil 顺序推断。

Apple `drutil -drive N status/info/info -xml` 的 BSD 节点和 registry path 必须与 native 枚举一致，同时核对写入 CD-R/DVD-R 的能力、CD-TAO、盘型、完整 Session、appendable 和当前写速度。新后端 `media6` 在创建 optical user client、获得独占访问之前，将 CLI 在子进程环境传入的 `APPENDCD_DEVICE_REGISTRY_ID` 与实际服务 ID 比较；不匹配就拒绝打开。环境值只由本次发现结果设置，普通子进程会清除用户环境中的同名变量。写前 INQUIRY 再核对本次动态身份，MMC 与新 metadata/content 读取的原检查继续执行。

本机发现直接使用 DiscRecording 的 BSD lookup 存在执行路径差异：相同测试程序在 Documents 下返回空、在 /private/tmp 下可见；原生管理员执行也未解决。当前实现改用可工作的 Apple drutil 查询能力，并与 IOKit 路径相互校验，没有要求移动 CLI 或永久修改系统权限。取得/释放 SCSI 独占访问还会移除/重建 IOMedia，其 registry ID 不等于盘片永久身份；保留它作为诊断数据，设备绑定使用光驱服务 ID，盘片通过 profile、Session/TOC/NWA、容量、metadata 和 payload hash 检查。

真实扫描、build/verify 已在当前 Lenovo/CD-R 上通过；新型号和多台实际光驱的物理刻录未额外测试。扩大型号可选择范围不代表所有固件及文件系统布局已经验收。

## 官方资料

- [libisofs 官方源码](https://dev.lovelyhq.com/libburnia/libisofs)，本机头文件为调用依据。
- [libudfread 官方源码](https://code.videolan.org/videolan/libudfread)，1.2.0 的输入 API 为调用依据。
- [Windows IOCTL_CDROM_GET_LAST_SESSION](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddcdrm/ni-ntddcdrm-ioctl_cdrom_get_last_session)，用于最终 Session 选择证据。

## 2026-10-10：显式同路径文件策略

在 GUI 实盘验收阶段，用户解除源码限制并要求跳过/替换同路径文件。`build` 和 `burn` 新增可选 `--on-conflict skip|replace`，其他命令拒绝此参数；不传时继续拒绝已有普通文件。目录同名仍合并，类型与 Windows 大小写/Unicode 名称碰撞仍失败。

Joliet 路径仅在显式 replace 时移除导入文件节点并加入新 stream，expected manifest 把该文件标记为 new；skip 保留导入节点及原 extent。UDF 路径在显式 replace 时替换该 manifest entry 与新输入 source，skip 保留旧 entry。所有未替换旧文件仍引用旧 extent，不复制旧 payload；镜像 verifier、写前 fresh metadata/hash、物理写入参数、Disk Arbitration guard 和实际全文件 readback 保持原流程。替换是最新 Session 的逻辑引用更新，不能擦除或回收旧 Session 数据。

共享源码只改 IsoCore.hpp/cpp、UdfSession.hpp/cpp、main.cpp，GUI 独立构建产物不覆盖原 build/appendcd 或 runtime。默认六组原回归、独立冲突测试、GUI 选择绑定与实盘结果见 gui/evidence/write-acceptance-20261010/acceptance.md；跨 Windows 和其他 GUI 介质边界分别记录。

## 2026-10-10：GUI Windows 返回盘与短卷导入

用户在真实 Windows 确认七文件可见、正常打开，DOCX/Java 大小一致，并追加 AlgoLogFileWatcher.java。返回 CD-R 实测第五 Session：start=33464、NWA=40666；首次旧 GUI 的 inspect 与 list 地址矛盾，旧 list 错读了首 Session 的三个文件。该结果保留为失败诊断，不计为返回盘通过。

DiscScanner 现在先把 msinfo 的两个地址与同一次扫描的 minfo 最新起点/NWA/leadout 交叉核对，原子接受，矛盾时最多重新查询三次只读 msinfo；仍不一致即拒绝。GUI 的 inspect/list 还要求身份、Session/Track、地址、容量和介质状态一致。该复查不调用 writer，不自动重试 burn 或互换 init。

Windows 最新 PVD/SVD 的 Volume Space Size 均为 31；旧 32-block 导入下限拒绝了这个有效的小卷。导入改为要求描述符在已报告卷范围内，生成镜像仍保持 >=32 与精确块数等旧校验。UDF VRS 识别只读到调用方的已知 end，短 ISO/Joliet 离线镜像不再因固定 48-block 扫描越界。生成格式、旧 extent、参数、写前 metadata/hash 与 guard 不变。字段依据 [ECMA-119](https://ecma-international.org/wp-content/uploads/ECMA-119_6th_edition_december_2025.pdf) 的 7.3 与 8.4.8；31-block 兼容性来自此次实际盘与独立 mkisofs -no-pad 回归。

新增改动涉及 DiscScanner.hpp/cpp、IsoVerifier.cpp、UdfReader.hpp/cpp、main.cpp，以及 drive_selection.cpp 的矛盾地址回归；GUI 导出适配器向识别器传递实际已知 end。加上原冲突改动，基线差异共十个共享源码文件和一个原测试文件；原 build/appendcd、两个 runtime 及 SCSI/MMC 后端未替换。最新材料位于 gui/evidence/write-acceptance-20261010/windows-return/。
