# 本次 GUI 刻录的 Windows 实机补验

2026-10-10，本机 GUI 在当前 Lenovo 光驱的 CD-R 上做了两次成功追加及实际读回。当前 4 Sessions / 4 Tracks，最新 Session 起点 26262，由核心实测；此数字不是下一次刻录地址。Windows 结果尚未采集。

把本目录的 windows-verify.ps1 和 GUI 导出的 windows-expected.json 拷到 Windows 本地目录。光盘换到真实 Windows 光驱后，在管理员 PowerShell 中进入此目录，并把 D: 换成实际光驱盘符：

```powershell
.\windows-verify.ps1 -Drive 'D:' -ExpectedJson '.\windows-expected.json' -Output '.\windows-result.json'
```

脚本只读光盘，报告写到本地；不用 Windows VM，不修改永久系统安全设置。脚本采集真实光驱、TOC、GET_LAST_SESSION、descriptor 和完整目录/文件 SHA-256。报告必须来自本次盘片，不能复用历史 CLI 结果。

人工核对：共七个文件；方向范围约束航迹抽稀技术说明.docx 可打开；DirectionRangeSimplifier.java 最新版本为 7358 bytes，末尾有 BurnLight GUI replacement acceptance 的注释；GUI-追加验收/receipt.txt 为 54 bytes；原四个文件还在。

请保留 windows-result.json，并记录 Windows 版本、光驱型号、盘符、人工打开结果。若要继续验证 Windows 再追加，请先完成只读报告，然后添加新的小文件并记录名称、大小、hash，返回 Mac 后再实际 inspect/readback；该部分尚未授权具体新输入，不自动进行。先不要盲目重复写入同一组文件。
