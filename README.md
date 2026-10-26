# BurnLight

简体中文 | [English](README.en.md)

一个在 Mac 上给 CD-R、DVD-R **继续追加文件**的小工具，带原生图形界面，也提供命令行。

主要解决这样的需求：一张在 Windows 上刻过文件、还没封盘的光盘，拿到 Mac 后还想往里加东西；之后再拿回 Windows，仍然能读取和继续追加。也可以从空白盘开始，在 Mac 上完成第一次刻录。

每次追加都会在光盘上新写一批内容（称为一个 Session）。没被替换的旧文件继续使用原来的数据，不需要重新刻一遍。

当前版本：**1.2.2 / build 6**。这是一个从个人实际需求发展出来的项目，主要在 Apple Silicon Mac 和 Lenovo XL6M 光驱上测试，欢迎有同样需求的人试用、反馈。

![BurnLight 主窗口](docs/images/burnlight.jpg)

## 能做什么

- **查看光盘**：识别光驱、盘片类型、剩余容量，以及最新一批刻录后的文件目录。
- **首次刻录和追加**：支持空白盘首次刻录，也支持在已有完整记录、仍可追加的盘上增加文件。
- **处理同名文件**：可以跳过或替换同路径文件，同名目录会合并。
- **导出文件**：把盘内所选文件、目录或全部内容保存到本地，并复读光盘核对内容。
- **检查刻录结果**：写入后实际读回，核对新旧文件；保留任务日志、镜像和诊断材料。
- **管理本地缓存**：预览并清理已经结束的任务材料。

> CD-R、DVD-R 是一次写入介质。“替换”只是让最新目录指向新版本，旧数据仍留在盘上，不能擦除，也不能回收已经用掉的容量。

## 支持范围

| 项目 | 当前情况 |
| --- | --- |
| 系统 | Apple Silicon / macOS 26、27；macOS 26.6 和 macOS 27 均已实机测试可用 |
| 盘片 | CD-R、DVD-R；追加时要求盘片未封盘、上一批刻录已完整结束 |
| 已有光盘格式 | ISO9660 / Joliet、受支持的 UDF 1.02、UDF 2.01 / VAT 布局 |
| 空白盘首次刻录 | 生成 ISO9660 + Joliet 格式，完成后保留继续追加的能力 |
| 光驱选择 | 单光驱自动选择；多个光驱时需要明确指定 |

程序不按品牌或型号设白名单，但会检查光驱的写入能力和盘片状态。**没有白名单不代表所有设备都已经测试过。**

目前不支持 CD-RW、DVD-RW、蓝光盘，也没有验证 Intel Mac、旧版 macOS。Windows 是光盘往返使用的一端，BurnLight 本身运行在 Mac 上。

UDF 支持有具体的格式限制，不是所有 UDF 光盘都能处理；其中 UDF 分支新增的单个文件须小于 1 GiB。符号链接、特殊文件，以及部分文件名或格式冲突会被拒绝。具体实现见[技术说明](docs/implementation.md)。

## 构建并打开图形界面

### 如果你是 AI

请根据[构建提示词](docs/build-prompt.md)进行工作。

### 如果你是人类

构建方式请继续向下阅读。

仓库不附带编译好的应用，需要在本机从源码构建。目前的构建脚本按以下环境编写：

- Apple Silicon Mac，macOS 26 或 27。
- Homebrew、Python 3 和 Xcode Command Line Tools。
- macOS 26 SDK，路径为 `/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk`。当前后端构建脚本使用这个固定路径。

取得源码后，在项目根目录依次运行：

```sh
# 安装依赖
brew install cdrtools libisofs libudfread cmake pkgconf smake

# 校验并补齐打包所需的基准文件，只恢复缺失内容
python3 gui/scripts/restore-reference.py

# 构建 UDF 2.01 / VAT 所需的独立刻录后端
python3 scripts/build-cdrecord.py --vat --output-dir build-vat-backend-v9

# 构建应用，默认不开放实际刻录
./gui/scripts/build.sh
open gui/output/BurnLight.app
```

VAT 后端构建会下载固定版本的 cdrtools 源码并校验 SHA-256。若已构建过、提示源码目录存在，重新构建时在同一条命令末尾加 `--resume`。

**默认生成的是只读版**，可以检查光盘、导出文件和预览镜像。需要实际刻录时，关闭应用，再重新构建：

```sh
BURNLIGHT_ENABLE_WRITES=1 ./gui/scripts/build.sh
open gui/output/BurnLight.app
```

生成的 `.app` 自带运行所需的工具和库，可以放到应用程序目录。它使用本机临时签名，没有 Developer ID 签名或公证，目前的打包流程主要用于本机使用。

更完整的构建、基准文件和后端说明见[仓库与构建说明](docs/repository.md)。

## 怎么用

1. **连接光驱，放入光盘。** 只有一个光驱时会自动选择，多个时先选目标。
2. **点击“检查介质与目录”。** 先确认盘片状态和已有文件。访问光驱时会弹出 macOS 管理员认证，密码由系统处理，应用不保存密码。
3. **选择要刻录的文件夹。** 应用会准备一份固定副本，保留目录结构，忽略新增文件夹中的 `.DS_Store`。
4. **预览本次刻录。** 同路径文件可以选择本次全部跳过或替换。预览成功只代表镜像检查通过，还没有写盘。
5. **确认首次刻录或追加刻录。** 程序会重新检查盘片，写入后实际读回校验。过程中请保持光驱连接，等待任务结束。

只想取出文件时，检查目录后选中文件或文件夹，再点导出；不选择时可以导出全部。导出会新建独立目录，不覆盖本地已有文件。它保存文件名、目录和内容，不还原原权限、时间戳或扩展属性。

任务历史和归档默认保存在 `~/Library/Application Support/BurnLight/`。读盘缓存和镜像可能占用较多空间，可通过“清理本地存储…”先预览再清理。诊断材料可能包含原始输入和本地路径，分享前请自行检查；应用不会自动上传。

### 如果刻录报错

**先检查盘片和日志，不要直接再刻一次。** 报错可能发生在数据已经写入之后，重复刻录会继续消耗光盘容量。

保留失败任务的原始镜像、同名 `.manifest` 和任务材料。界面的“原镜像只读补验”可以检查已有写入结果，不会再写入一批文件。遇到未完成的刻录，普通追加会停止；恢复功能只覆盖少数已验证的 DVD-R 布局，不是通用的坏盘修复工具。

底层访问光驱时会暂时暂停 macOS 的磁盘仲裁服务，并在结束时恢复。异常退出后的恢复机制尚未完成真实系统服务的全部故障测试。如果任务已经结束，但系统仍无法识别磁盘，可在终端尝试恢复该服务：

```sh
sudo kill -CONT "$(pgrep -x diskarbitrationd)"
```

## 命令行使用

命令行程序保留原名 `appendcd`。先完成上面的依赖安装、基准恢复和 VAT 后端构建，再编译当前源码：

```sh
cmake -S . -B build-cli -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.sdk \
  -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build-cli -j 4
./build-cli/appendcd help
```

请使用 `build-cli/appendcd`；`build/appendcd` 是图形界面打包时校验用的历史基准文件。

查看光驱、盘片和文件：

```sh
./build-cli/appendcd drives
sudo ./build-cli/appendcd inspect --work-dir artifacts/physical
sudo ./build-cli/appendcd list --work-dir artifacts/physical
```

空白盘第一次刻录：

```sh
sudo ./build-cli/appendcd init ~/Desktop/Burn --work-dir artifacts/physical
```

给已有完整记录、仍可追加的盘增加文件：

```sh
sudo ./build-cli/appendcd burn ~/Desktop/Burn --work-dir artifacts/physical
```

`~/Desktop/Burn` 换成你的文件夹。CLI 默认拒绝同路径文件；确实需要时，为 `burn` 加 `--on-conflict skip` 或 `--on-conflict replace`。

有多个光驱时，先运行 `drives`，再把输出中的地址通过 `--device` 传给命令，例如 `--device 1,0,0`。这里不能填 `/dev/diskN`。GUI 和 CLI 请勿同时操作光驱。

`help` 还列出了离线镜像生成（`init-build` / `build`）、镜像校验（`verify`）、只读补验（`readback`）等命令。校验镜像时需保留同名 `.manifest` 及其依赖的缓存；恢复流程见[技术说明](docs/implementation.md)。

## 测试到什么程度了

在本机 Lenovo XL6M 光驱上，已经做过 CD-R、DVD-R 的真实刻录，以及“Mac 追加 → Windows 读取并再追加 → Mac 读回”的往返测试。不同格式和操作的验证范围有所区别，不能据此保证其他光驱、固件或盘片都兼容。

1.2.2 主要修复 Windows 多次更新文件后，UDF 2.01 / VAT 目录无法读取的问题。该版本对一张已有 12 批刻录的 DVD-R 完成了全部 9 个文件的两次独立读取和本地 SHA-256 比对；这次修复验证没有新增写盘。详情见[1.2.2 验证记录](docs/udf201-windows-updates-20261019.md)。

开发时可以运行以下检查，它们不会执行实际刻录：

```sh
ctest --test-dir build-cli --output-on-failure
./gui/scripts/test.sh
```

`test.sh` 会重新生成只读应用，测试后如需刻录，要重新按上面的方式构建可写版。部分历史导出验证依赖未纳入 Git 的实盘缓存，不能仅凭源码重现；离线测试通过也不代表完成了新的实盘或 Windows 测试。

## 更多资料

- [仓库与构建说明](docs/repository.md)：源码范围、基准恢复和独立构建。
- [图形界面详细记录](gui/README.md)：功能、权限和各阶段验证情况，含历史版本说明。
- [技术说明](docs/implementation.md)：文件系统处理、追加原理和限定恢复流程。
- [DVD-R / UDF 2.01 往返测试](docs/udf201-dvd-r-roundtrip-20261013.md)：Mac 与 Windows 之间的实际使用记录。
- [早期实验记录](docs/experiments-2026-10-09.md)：开发过程、失败原因和修复细节。

历史文档中的本机绝对路径、任务编号和盘片地址是当时的记录，对应的大型镜像、用户文件和完整日志不随仓库提供。

BurnLight 使用了 cdrtools、libisofs 和 libudfread。第三方许可会随构建后的应用保存在 `Contents/Resources/ThirdPartyNotices/`，仓库另保留 [CDDL 声明](backend/CDDL.Schily.txt)。
