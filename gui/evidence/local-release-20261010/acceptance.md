# 本机 1.0 离线 / 只读版验收

2026-10-10。本记录对应开启刻录和加入冲突策略之前的只读发布。不是 GUI 写盘验收。

- 使用明确的 macOS 26 SDK，arm64 原生 SwiftUI/AppKit 实际编译、安装并启动。自包含 7 个 CLI/backend/readcd/dylib 依赖，逐项 hash 与本地 ad hoc 签名通过。安装 / ZIP / 构建文件一致；搬移到含中文、空格、引号和美元符号的路径后离线构建、verify 通过。
- 115 条 Swift/状态机/安全边界断言通过；打包 CLI 六组原回归通过；导出适配器 10 个离线场景通过。均无物理写入。
- GUI 实际发起发现、inspect/list、全盘 4 文件导出、原 Windows 第二 Session 镜像实际光盘 readback、84 bytes 固定快照 build/verify、重启发现和检查、选中中文目录导出。9 个完成任务 / 17 个 CLI 命令均有明确 exit=0 收据。光盘保持 CD-R、2 Sessions / 2 Tracks、latest=11702、NWA=18904、可追加。
- 两次导出合计 5 个本地文件，独立与历史 Windows 清单比对大小/hash 全部通过；本地文件为当前用户所有。实际目录选择、系统认证桥接、用户权限拷贝均由安装的 GUI 发起。
- 重启恢复历史、预览任务诊断导出和每项 hash 核对通过。任务中关闭窗口后可重开，退出请求被拒绝；不硬杀任何 worker。
- 最终 GUI SHA256 `d92b5a7af90a6ed7ed60ede0afe8cf844e6d8080d403bb296d3dd72f5e5bc818`。全盘导出 / readback / 预览发生在上一构建，最终改动仅为历史按钮样式；最终构建另做实际 inspect/list 和目录导出，绑定详情见 physical-build-binding.json。
- ZIP SHA256 `d6a6603f27c65416eaec1a93961381fd2f48382f98b8d0fb9b5413bbcd3e1ccb`。原 CLI/runtime/47 个受保护源文件的 hash 全部保持基线，见 baseline-final.json。

证据：validation.json、cli-regression.json/log、adapter-offline.json/log、release-check.json、actual-copy-check.json、diagnostics-check.json、physical-jobs/index.json、process-state.json。

未完成：此包禁用 init/burn，未验证 GUI 实盘写入；历史 CD-R/DVD-R CLI 闭环不作为新 GUI 的写盘或 Windows 证明。无 Developer ID / 公证 / helper，发行范围仅本机 macOS 26 arm64；其他系统、光驱、多驱、ROM/RW/BD、失败状态真实故障注入未验收。后续 1.1 新冲突策略及实盘证据放在 ../write-acceptance-20261010/，不回填到本次只读 PASS。
