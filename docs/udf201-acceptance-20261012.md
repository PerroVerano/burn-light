# UDF 2.01/VAT 交付与验收（2026-10-12）

当前源码已实现受限 CD-R / UDF 2.01 / type-1 + Virtual Partition Map 的读取、追加生成、独立验证与 CLI/GUI 接入。八 Session 实盘的最新入口、跨 Session VDS、VAT、当前目录及 extent 解析成功；**全文件读取未通过，真实追加与 Windows 往返未执行，完整功能尚未验收完成**。

本次没有写盘、init、erase、封盘或 recovery，没有覆盖安装应用、原 runtime 或历史验收，也没有 push。用户重新连接光驱后重新识别了当前盘。后续真实写入需要完整可读的旧内容基线，以及对实时目标盘和新增文件的明确授权。

## 输入与构建绑定

基线 commit 为 `b4899944d19d32ff82b89d9185e3dcce1227a38a`，开始时工作区干净。初始源码和保护对象 hash 在 `artifacts/udf201-acceptance/20261012/baseline.json`。最终源码、CLI、GUI、sanitizer、后端、fixture、只读输入和结果 hash 见 [机器可读摘要](evidence/udf201-20261012.json)。当前工作区修改尚未提交。

专用 CLI 为 `build-cli/appendcd`；GUI 为 `build-udf201-gui/output/BurnLight.app`（只读包，未安装）；sanitizer 为 `build-udf201-asan/appendcd`。后端 `build-vat-backend/appendcd-vat-cdrecord` 为独立 vat8，原 media6/initial7 不变。上游 cdrtools 3.02a09 tar SHA256 为 `c7e4f732fb299e9b5d836629dadf5512aa5e6a5624ff438ceb1d056f4dcb07c2`，CDDL notice 在 `backend/CDDL.Schily.txt`；构建命令、版本、生成 provenance 在专用目录和 `vat-backend-build.log`。

原始证据总目录为 `/Users/luzove/Documents/Projects/BurnLight/artifacts/udf201-acceptance/20261012/`，不提交用户内容或大缓存。实盘 CLI 运行的二进制 hash 单独记录在 `physical-cli-sha256.txt`；之后最终构建又对同一有效块缓存重新解析。cache 的容器 hash、`.blocks` 有效块索引和 `.tracks` 区间 hash 一起绑定输入，**容器中的零洞不是已读取数据，也不是全盘镜像 hash**。旧的 2026-10-10 物理验收保持独立，不能回填本次 VAT PASS。

## 当前八 Session 实盘只读结果

重新识别为 Lenovo Ultraslim DVD XL6M，CD-R，8 个完成 Session / 8 个完成 Track，appendable，无 unfinished Session；本次地址为 `1,0,0`、`/dev/disk4`、registry `4295064341`。最新起点 95414，已记录 end-exclusive 132640，NWA 139542，剩余 220301 blocks。minfo 的第九 Session/Track 是下一空白项，不计入已完成数。TOC 的 leadout 报告含额外尾部地址，读取边界采用完成 Track 的 minfo 数据区间，不把间隙或 run-out 扩成已记录用户数据。

`final-physical-directory.log` 为最终 CLI 的实际只读 list；原始扫描、VRS、诊断和有效缓存位于 `final-physical-directory/run-2glZOH/`。权威视图 identity 为 `3422b872f64742c2bd8206fe70b348dbacb9c8eab95f8d10136500b2cbb179a3`。

| 引用 | 绝对 LBA | 所属 Track / Session |
| --- | ---: | --- |
| 最新入口 Anchor | 95670 | 8 / 8 |
| 主 / 备用 VDS | 40704 / 40736 | 6 / 6 |
| 物理分区起点 | 40928 | 6 / 6 |
| 最新 VAT ICB | 132639 | 8 / 8 |
| previous VAT 链 | 88511 → 41119 → 40959 | 7 / 7 → 6 / 6 → 6 / 6 |
| 当前 FSD / root ICB | 132618 / 132619 | 8 / 8 |
| swagger / aisea / center ICB | 41097 / 88490 / 132620 | 6 / 6、7 / 7、8 / 8 |

revision 0x0201、Tag Version 3、Partition Number 8192、Access Type 2、Partition Length 318912；Map Reference 0 为物理映射，1 为虚拟映射，FSD Reference 1。Anchor/VDS 使用绝对地址；VAT 和虚拟 ICB 使用各自空间的 Tag Location。最新 inline VAT header 为 184 bytes（implementation use 32）、422 entries；previous 相对位置 47583 对应绝对 88511。Session 6 内存在两个 VAT 事务，不能把 VAT 链长度当作物理 Session 数。

| 当前文件 | bytes | 原实际数据地址（LBA + bytes） | 实际 SHA256 |
| --- | ---: | --- | --- |
| aisea.jar | 82318010 | 48286+81985536；88325+332474 | 未完成：LBA 62772 不可纠正 |
| centerEddy_80.bln | 75638750 | 95678+75300864；132453+337886 | `4f310aff3c56f460d8a3facfb34da6b08002328c6c570783e1ef8d5d2fff21ba` |
| swagger.json | 246137 | 40967+65536；41006+180601 | `4f9bf438f233b913b77a3c071eea3a8607c11eaf0dfa253535f58b22b1a8d4f4` |

名称、层级（均在根目录）与大小与 macOS 当前挂载视图一致。后两文件经实际 readcd 读取，hash 与挂载文件一致，证据在 `selected-hashes/`、`mounted-selected-sha256.txt`。`aisea.jar` 的批量读取与单扇区复查均报 Sense Key 3 / Sense 11/05、L-EC uncorrectable，retries=4；见 `read-hashes-failed/failure.txt` 和 `burnlight-udf201-sector-recheck-20261012.log`。此前挂载读取曾进入内核 I/O 等待；按用户重新连接后恢复，没有硬杀或服务故障注入。

本次最新 FSD/目录和 center 文件均在第八 Session，不能通过读到第六 Session 的旧目录得到此视图。独立 fixture 另以 `latest.txt` 与历史视图区别验证错误选择历史 VAT 必然失败。主样本全文件 hash 尚不完整，不能把上述目录成功记作 CURRENT_8SESSION_READ_PASS。guard 在正常和读取失败路径均恢复；实盘复查结束后服务运行，无遗留光盘 worker。

## 离线追加、边界与 GUI 回归

独立 Python 编码器 `tests/vat_fixture.py` 构造八 Session 跨引用、单 Session、中文目录、空文件、FE/EFE、内嵌数据、物理 allocation 与多事务 VAT。`tests/vat_oracle.py` 不使用 manifest 或 C++ writer/reader，独立核对生成复合视图的 tag/CRC、地址、内容、previous VAT、LVID 和尾部布局。

VAT 专项覆盖连续两次追加、external VAT、skip/replace、原 inline ICB 和 payload extent/hash 保持、旧 payload 不写入新镜像、UID rollover；也覆盖错误 rebasing、Track gap/未来/溢出、缓存缺块/坏索引、非法映射、截断/损坏最新 VAT、checksum/CRC/Tag Location、目录/allocation/VAT 链循环，以及主 VDS 恢复到当前入口的备用 VDS、两份均无效失败。精确断言列表及计数见摘要中的 `vat` 结果。

最终七组 CTest 为原 `offline_multisession`、`udf_multisession`、`backend_target`、`recovery_image`、`drive_selection`、`initial_session` 加 `vat_multisession`，全部通过。fixture 输出隔离到构建目录；GUI 搬移包另跑同七组。GUI 有 142 条受控断言，VAT 导出覆盖全目录、原 inline 文件、中文空文件与过期视图拒绝；冲突、短卷及既有导出结果另附摘要。ASan/UBSan 跑同七组，禁用当前 macOS 不支持的 leak detection，**未宣称 leak detection PASS**。

新分支只用于 CD-R / UDF 2.01 / type-1 + virtual map，未实现的版本、映射、文件类型、稀疏 extent 等明确拒绝。没有扩大到 sparing、metadata partition、RW/BD、UDF 首刻或 DVD recover。缺少本机历史大缓存的物理场景如实标记未复现。

## 可审核镜像与真实往返待验收

以本次实时扫描的 latest/NWA 生成本地 `physical-preview/session-next.iso`，576 blocks / 1179648 bytes；默认冲突拒绝。唯一新增文件 `burnlight-vat-mac-20261012.txt` 为 56 bytes，SHA256 `faaabcbc28a26670d4b26b2a47b350d98d8a03c7778e22274931e636c6778b37`。镜像 SHA256 为 `b7d057be184ec2a238f77aa0b9c977dcf918e1900b4b5bc7e8e7548904a8529b`。本地生成及重新解析通过，新内容 hash 正确，三个旧文件 extent 保持，生成阶段没有读取或复制旧 payload；verify 日志明确显示旧 hash verified=0。本镜像是预览，未获写入授权，不可当作完整旧内容基线。

取得完整旧内容基线后须重新扫描设备、介质、最新事务/NWA/容量，重新生成绑定镜像和冲突策略；旧预览不能直接沿用。授权目标盘及明确文件后，Mac 可变包追加并用新的实际扇区缓存核对所有旧/新 hash、地址与最终 VAT。packet link、padding、尾扇区及新 NWA 必须按实测记录，不能用 576-block 镜像预设物理边界。

Windows 人工正常打开、PowerShell 自动报告和 Mac 实际读回分别记录。`scripts/windows-verify.ps1` 已增加 2.01 标识/VDS 取证及 OS 版本字段，但本次没有 Windows 执行结果。用户继续采用原刻录方式追加一个已记录名称/大小/源 hash 的小文件，记录 Windows 版本、光驱与刻录方式；返回 Mac 后重新探测最新入口/VAT并读取全部文件。不能预设 Windows 追加必然新增一个物理 Session。

| 完成判据 | 本次状态 |
| --- | --- |
| CURRENT_8SESSION_READ_PASS | 未通过：目录成功，2/3 文件 hash 匹配；一个文件介质错误 |
| CROSS_SESSION_REFERENCE_PASS | PASS：实盘入口/跨引用/VAT/目录 + 独立边界测试 |
| SINGLE_SESSION_REGRESSION_PASS | 离线 PASS；2026-10-10 辅助实盘未复验 |
| VAT_OFFLINE_APPEND_PASS | PASS：匿名连续追加、独立解析及本地预览；无物理写入 |
| EXISTING_FEATURE_REGRESSION_PASS | 离线 PASS；不代表重跑历史 CD/DVD 物理验收 |
| GUI_INTEGRATION_PASS | 受控 PASS；完整实盘导出受介质错误阻断 |
| MAC_PHYSICAL_APPEND_PASS | 待验收：旧内容基线不完整，未授权/未写入 |
| WINDOWS_READ_AND_REAPPEND_PASS | 待验收：未执行 |
| MAC_RETURN_READBACK_PASS | 待验收：未执行 |

每项的输入、构建 hash、结果和证据路径见机器可读摘要。完整八 Session 主样本的 Mac ↔ Windows 闭环仍待完成，不能以离线成功或历史 UDF 1.02 实验替代。
