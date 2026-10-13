import SwiftUI
import BurnLightCore

struct ContentView: View {
    @ObservedObject var model: AppModel
    @State private var selectedTab = "files"
    @State private var demoScenario = "appendable"
    @State private var showCleanup = false
    private let accent = Color(red: 0.86, green: 0.36, blue: 0.16)
    var body: some View {
        HStack(spacing: 0) {
            sidebar.frame(width: 245)
            Divider()
            VStack(alignment: .leading, spacing: 18) {
                header
                discSummary
                status
                Picker("内容", selection: $selectedTab) {
                    Text("文件与快照").tag("files")
                    Text("任务日志").tag("logs")
                    Text("历史记录").tag("history")
                }.pickerStyle(.segmented).frame(width: 360)
                Group {
                    switch selectedTab {
                    case "logs": logView
                    case "history": historyView
                    default: filesView
                    }
                }.frame(maxWidth: .infinity, maxHeight: .infinity)
                footer
            }.padding(26)
        }
        .tint(accent)
        .background(Color(nsColor: .windowBackgroundColor))
        .sheet(isPresented: $showCleanup) { CleanupView(model: model) }
        .alert(model.writeCLIOperation == .initialize ? "首次刻录到空白盘" : "追加到已有光盘", isPresented: $model.showWriteConfirmation) {
            Button("取消", role: .cancel) {}
            Button("确认并进行系统认证") { Task { await model.write() } }
        } message: {
            Text("目标：\(model.selected?.name ?? "")\n介质：\(model.disc?.media ?? "未知") · 快照 \(model.snapshot?.fileCount ?? 0) 个文件\n同路径文件：\(model.conflictingPaths.count) 个 · \(model.conflictPolicy.label)\n使用同一份固定快照，核心将复查目标、容量与地址，刻录后实际读回。写入不可撤销，提交后不能强制取消。")
        }
    }
    private var sidebar: some View {
        VStack(alignment: .leading, spacing: 22) {
            HStack(spacing: 10) {
                Image(systemName: "opticaldisc.fill").font(.system(size: 30)).foregroundStyle(accent)
                VStack(alignment: .leading, spacing: 2) {
                    Text("BurnLight").font(.title2.bold())
                    Text("原生光盘工作台").font(.caption).foregroundStyle(.secondary)
                }
            }
            Divider()
            HStack {
                Text("光学设备").font(.subheadline.bold())
                Spacer()
                Button { Task { await model.discover() } } label: { Image(systemName: "arrow.clockwise") }
                    .help("重新发现光驱；使旧介质检查失效").disabled(model.busy || model.workerUnknown || model.demo)
            }
            if model.drives.isEmpty {
                Label("未发现光驱", systemImage: "externaldrive.badge.questionmark").foregroundStyle(.secondary)
                Text("连接 USB 光驱后刷新。多台设备需要明确选择。")
                    .font(.caption).foregroundStyle(.secondary)
            } else {
                Picker("选择光驱", selection: $model.selectedID) {
                    Text("请选择").tag("")
                    ForEach(model.drives) { drive in Text(drive.name).tag(drive.id) }
                }.labelsHidden().disabled(model.busy || model.workerUnknown || model.demo)
                    .onChange(of: model.selectedID) { _, _ in if !model.demo { model.changeDrive() } }
                if let drive = model.selected {
                    Label(drive.name, systemImage: "externaldrive").font(.subheadline).fixedSize(horizontal: false, vertical: true)
                    Text(drive.media == "none" ? "未检测到光盘" : "枚举介质：\(drive.media)")
                        .font(.caption).foregroundStyle(.secondary)
                }
            }
            Button { Task { await model.inspect() } } label: { Label("检查介质与目录", systemImage: "magnifyingglass") }
                .buttonStyle(.borderedProminent).disabled(model.busy || model.selected == nil || model.workerUnknown || model.demo)
            Text("只读检查也需要系统管理员认证，期间会暂时卸载光学卷。")
                .font(.caption).foregroundStyle(.secondary)
            if model.workerUnknown {
                Button("核对执行器退出收据") { Task { await model.checkWorkerExit() } }.disabled(model.busy)
                Text("只有明确退出收据、worker 已结束、系统服务正常才可恢复。不会强制解锁或终止进程。")
                    .font(.caption).foregroundStyle(.secondary)
            }
            Divider()
            VStack(alignment: .leading, spacing: 9) {
                Label("一次一个物理任务", systemImage: "lock.shield")
                Label("固定新增文件快照", systemImage: "doc.on.doc")
                Label("刻录后实际读回", systemImage: "checkmark.shield")
            }.font(.caption).foregroundStyle(.secondary)
            if model.demo {
                Divider()
                Text("模拟模式 · 无物理命令").font(.caption.bold()).foregroundStyle(.orange)
                Picker("模拟介质", selection: $demoScenario) {
                    Text("可追加").tag("appendable"); Text("空白盘").tag("blank")
                    Text("封盘").tag("closed"); Text("未完成 Session").tag("unfinished")
                    Text("未插盘").tag("empty"); Text("无光驱").tag("none")
                }.onChange(of: demoScenario) { _, value in model.loadDemo(value) }
            }
            Spacer()
            Button { showCleanup = true } label: { Label("清理本地存储…", systemImage: "externaldrive.badge.minus") }
                .disabled(!model.canManageStorage)
            Text("本机使用版 · macOS 26 / Apple Silicon\n使用现有 appendcd 核心")
                .font(.caption2).foregroundStyle(.tertiary)
        }.padding(20).frame(maxHeight: .infinity).background(Color(nsColor: .controlBackgroundColor))
    }
    private var header: some View {
        HStack {
            VStack(alignment: .leading, spacing: 5) {
                Text("让光盘继续生长").font(.system(size: 27, weight: .semibold))
                Text("追加文件、实际读回、导出到本地。")
                    .font(.subheadline).foregroundStyle(.secondary)
            }
            Spacer()
            Label(model.demo ? "模拟数据" : (model.physicalWritesEnabled ? "本机 CLI" : "离线 / 只读版"), systemImage: model.demo ? "testtube.2" : "desktopcomputer")
                .font(.caption).padding(.horizontal, 12).padding(.vertical, 7)
                .background(.quaternary, in: Capsule())
        }
    }
    private var discSummary: some View {
        HStack(spacing: 22) {
            Image(systemName: "opticaldisc").font(.system(size: 48, weight: .thin)).foregroundStyle(accent)
            metric("当前介质", model.disc?.media ?? "尚未检查")
            metric("文件系统", model.disc?.filesystem == "udf2.01+vat" ? "UDF 2.01 / VAT" : (model.disc?.filesystem ?? (model.disc == nil ? "尚未检查" : "ISO9660 + Joliet")))
            if model.disc?.udfCompatibility != nil {
                Text("此盘使用 Windows 兼容视图，已校验文件引用；格式偏差记录在日志中。")
                    .font(.caption).foregroundStyle(.secondary)
            }
            Divider().frame(height: 38)
            metric("介质状态", discLabel)
            Divider().frame(height: 38)
            metric("剩余容量", model.disc.map { formatBytes($0.freeBytes) } ?? "—")
            Spacer()
            if let disc = model.disc {
                VStack(alignment: .trailing, spacing: 4) {
                    Text("\(disc.sessions) Sessions · \(disc.tracks) Tracks").font(.caption)
                    Text(disc.appendable ? "仍可追加" : "不可追加").font(.caption).foregroundStyle(disc.appendable ? .green : .orange)
                }
            }
        }.padding(20).background(Color(nsColor: .controlBackgroundColor), in: RoundedRectangle(cornerRadius: 12))
    }
    private var discLabel: String {
        guard let disc = model.disc else { return "等待检查" }
        if disc.unfinished { return "Session 未完成" }
        if disc.blank { return "空白" }
        return disc.writeCLIOperation == .burn ? "可追加" : "不可写入"
    }
    private func metric(_ label: String, _ value: String) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(label).font(.caption).foregroundStyle(.secondary)
            Text(value).font(.headline)
        }
    }
    private var status: some View {
        HStack(alignment: .top, spacing: 12) {
            if model.busy { ProgressView().controlSize(.small).padding(.top, 3) }
            else { Image(systemName: statusIcon).foregroundStyle(statusColor).padding(.top, 3) }
            VStack(alignment: .leading, spacing: 4) {
                Text(model.phase.label).font(.subheadline.bold())
                Text(model.detail).font(.caption).foregroundStyle(.secondary).textSelection(.enabled)
            }
            Spacer(minLength: 0)
            if model.phase == .staging && model.busy { Button("取消快照") { model.cancelStaging() } }
        }.padding(13).frame(maxWidth: .infinity, alignment: .leading)
            .background(statusColor.opacity(0.07), in: RoundedRectangle(cornerRadius: 9))
    }
    private var statusIcon: String {
        if model.workerUnknown || model.needsInspection { return "exclamationmark.shield" }
        if model.phase == .completed { return "checkmark.circle.fill" }
        return model.phase == .failed ? "exclamationmark.triangle" : "info.circle"
    }
    private var statusColor: Color {
        if model.workerUnknown || model.needsInspection || model.phase == .failed { return .orange }
        return model.phase == .completed ? .green : .secondary
    }
    private var filesView: some View {
        HStack(alignment: .top, spacing: 20) {
            VStack(alignment: .leading, spacing: 12) {
                HStack { Text("光盘已有文件").font(.headline); Spacer(); Text("\(model.oldFiles.filter { !$0.directory }.count) 个文件").font(.caption).foregroundStyle(.secondary) }
                if model.oldFiles.isEmpty {
                    emptyState("光盘目录", model.disc?.blank == true ? "空白盘尚无文件" : "检查介质后读取最新完整 Session", icon: "opticaldisc")
                } else {
                    Table(model.oldFiles, selection: $model.selectedDiscPaths) {
                        TableColumn("相对路径") { entry in Label(entry.path, systemImage: entry.directory ? "folder" : "doc").lineLimit(1).help(entry.path) }
                        TableColumn("大小") { entry in Text(entry.directory ? "—" : formatBytes(entry.bytes)).foregroundStyle(.secondary) }.width(75)
                    }.tableStyle(.inset).border(Color.secondary.opacity(0.15)).disabled(model.busy)
                    HStack {
                        Button(model.selectedDiscPaths.isEmpty ? "导出全部到本地…" : "导出所选到本地…") { model.chooseDiscExport() }.disabled(!model.canExportDisc)
                        if !model.selectedDiscPaths.isEmpty { Button("取消选择") { model.selectedDiscPaths = [] }.disabled(model.busy) }
                        Spacer()
                        if model.exportedDirectory != nil { Button("显示导出文件") { model.showExportedFiles() }.disabled(model.busy) }
                    }
                    Text("可多选文件或目录；未选择时导出全部。保留相对路径，在目标下新建独立目录，并复核本地 hash。")
                        .font(.caption).foregroundStyle(.secondary)
                }
            }
            Divider()
            VStack(alignment: .leading, spacing: 12) {
                HStack { Text("本次新增").font(.headline); Spacer(); Button("选择目录…") { model.chooseInput() }.disabled(model.busy || model.workerUnknown) }
                if let snapshot = model.snapshot {
                    HStack {
                        Label(model.sourceName, systemImage: "folder.fill").lineLimit(1)
                        Spacer()
                        Text("\(snapshot.fileCount) 个文件 · \(formatBytes(snapshot.totalBytes))").font(.caption).foregroundStyle(.secondary)
                    }
                    fileTable(snapshot.entries)
                    Text("已建立只读快照；同路径文件 \(model.conflictingPaths.count) 个 · \(model.conflictPolicy.label)。预览前可选择跳过或替换；名称和类型冲突由核心复核。")
                        .font(.caption).foregroundStyle(.secondary)
                } else { emptyState("新增目录", "选择一个目录，按用户权限复制并校验内容", icon: "folder.badge.plus") }
            }
        }
    }
    private func fileTable(_ files: [FileEntry]) -> some View {
        Table(files) {
            TableColumn("相对路径") { entry in Label(entry.path, systemImage: entry.directory ? "folder" : "doc").lineLimit(1).help(entry.path) }
            TableColumn("大小") { entry in Text(entry.directory ? "—" : formatBytes(entry.bytes)).foregroundStyle(.secondary) }.width(75)
        }.tableStyle(.inset).border(Color.secondary.opacity(0.15))
    }
    private func emptyState(_ title: String, _ description: String, icon: String) -> some View {
        VStack(spacing: 12) {
            Image(systemName: icon).font(.system(size: 32, weight: .light)).foregroundStyle(.tertiary)
            Text(title).font(.subheadline.bold())
            Text(description).font(.caption).foregroundStyle(.secondary).multilineTextAlignment(.center)
        }.frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(.quaternary.opacity(0.35), in: RoundedRectangle(cornerRadius: 9))
    }
    private var logView: some View {
        ScrollView {
            Text(model.logs.isEmpty ? "任务执行结束后显示 stdout/stderr。管理员桥接不提供实时阶段或百分比。" : model.logs)
                .font(.system(size: 11, design: .monospaced)).textSelection(.enabled)
                .frame(maxWidth: .infinity, alignment: .leading).padding(14)
        }.background(Color(nsColor: .textBackgroundColor), in: RoundedRectangle(cornerRadius: 9))
    }
    private var historyView: some View {
        VStack(alignment: .leading, spacing: 8) {
            if !model.historyWarnings.isEmpty {
                Text("\(model.historyWarnings.count) 条历史索引无法读取，原文件保留；物理写入已阻止，请保存诊断并检查记录。")
                    .font(.caption).foregroundStyle(.orange).textSelection(.enabled)
            }
            List(model.history) { record in
            HStack(alignment: .top) {
                Image(systemName: record.phase == .completed ? "checkmark.circle" : "doc.text")
                VStack(alignment: .leading, spacing: 4) {
                    Text("\(record.operation?.rawValue ?? "staging") · \(record.phase.label)").font(.subheadline.bold())
                    Text(record.message).font(.caption).foregroundStyle(.secondary)
                    Text(record.created.formatted(date: .numeric, time: .standard)).font(.caption2).foregroundStyle(.tertiary)
                    Text(record.materialsRemovedAt != nil ? "材料已清理，保留任务摘要" : (record.archivePath != nil ? "可读材料已持久保存" : (record.archiveWarning ?? "原临时材料尚未归档")))
                        .font(.caption2).foregroundStyle(record.materialsRemovedAt != nil || record.archivePath != nil ? Color.secondary : Color.orange)
                    if let output = record.exportPath { Text(output).font(.caption2).foregroundStyle(.secondary).textSelection(.enabled) }
                }
                Spacer()
                VStack(alignment: .trailing, spacing: 7) {
                    Button("材料") { model.openLogs(record.archivePath ?? record.workspacePath ?? "/private/tmp/burnlight-job-\(record.id.uuidString)") }
                        .disabled(record.materialsRemovedAt != nil || model.busy)
                    Button("导出诊断…") { model.exportDiagnostics(record) }.disabled(model.busy || model.workerUnknown || record.archivePath == nil)
                    if record.operation?.writes == true, [.needsInspection, .physical, .unknown].contains(record.phase) {
                        Button("原镜像只读补验…") { Task { await model.readbackFailedJob(record) } }
                            .disabled(model.busy || model.workerUnknown || record.imagePath == nil || model.selected == nil)
                    }
                }
            }.padding(.vertical, 6).buttonStyle(.borderless).accessibilityElement(children: .contain)
            }
        }
    }
    private var footer: some View {
        VStack(alignment: .leading, spacing: 12) {
            Divider()
            HStack {
                Button("打开任务日志") { model.openLogs() }.disabled(model.lastDirectory == nil)
                Button("导出 Windows 清单") { model.exportWindows() }.disabled(!model.canExportWindows || model.demo)
                Spacer()
                Button("原镜像只读补验…") { model.chooseReadback() }.disabled(model.busy || model.selected == nil || model.workerUnknown || model.demo)
                Button(model.disc?.blank == false ? "构建追加预览" : "离线首刻预览") { model.chooseConflictPolicyAndPreview() }
                    .disabled(model.busy || model.snapshot == nil || model.workerUnknown || model.demo)
                Button(model.writeCLIOperation == .initialize ? "首次刻录…" : "追加刻录…") { model.showWriteConfirmation = true }
                    .buttonStyle(.borderedProminent).disabled(!model.canWrite)
            }
            if let reason = model.writeDisabledReason {
                Text("刻录暂不可用：\(reason)")
                    .font(.caption).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            }
            DisclosureGroup("技术详情与验收边界") {
                Text("设备：\(model.selected?.device ?? "—") · Registry ID：\(model.selected?.registryID ?? "—")\n最新 Session LBA：\(model.disc.map { String($0.lastLBA) } ?? "—") · NWA：\(model.disc.map { String($0.nextLBA) } ?? "—")（仅展示，核心现场探测）\n本机使用版；每次任务以实际读回结果为准，Windows 实机验收另行记录。系统认证非流式传输，日志在命令结束后读取。任务材料保存在 Application Support，归档中的原绝对路径保留；失败材料不自动清理。")
                    .font(.caption2).foregroundStyle(.secondary).textSelection(.enabled).frame(maxWidth: .infinity, alignment: .leading).padding(.top, 5)
            }.font(.caption)
            if !model.demo && !model.physicalWritesEnabled {
                Text("本次交付已禁用物理 init/burn；预览、光盘文件导出与实际光盘只读补验可用。")
                    .font(.caption2).foregroundStyle(.secondary)
            }
        }
    }
}
