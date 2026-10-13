import Foundation
import AppKit
import BurnLightCore

@MainActor
final class AppModel: ObservableObject {
    @Published var drives: [Drive] = []
    @Published var selectedID = ""
    @Published var disc: Disc?
    @Published var oldFiles: [FileEntry] = []
    @Published var selectedDiscPaths: Set<String> = []
    @Published var exportedDirectory: URL?
    @Published var snapshot: InputSnapshot?
    @Published var sourceName = "尚未选择目录"
    @Published var phase: JobPhase = .idle
    @Published var detail = "连接光驱，检查介质，再选择本次新增目录。"
    @Published var busy = false
    @Published var physicalActive = false
    @Published var logs = ""
    @Published var history: [JobRecord] = []
    @Published var lastDirectory: URL?
    @Published var needsInspection = false
    @Published var workerUnknown = false
    @Published var previewImage: URL?
    @Published var showWriteConfirmation = false
    @Published var demo = false
    @Published var physicalWritesEnabled = false
    @Published var historyWarnings: [String] = []
    @Published var conflictPolicy: ConflictPolicy = .reject
    @Published var cleanupMode: CleanupMode = .temporary
    @Published var cleanupRetentionDays = 0
    @Published var cleanupPlan: CleanupPlan?
    @Published var cleanupSelectedIDs: Set<UUID> = []
    @Published var cleanupWorking = false
    @Published var cleanupMessage = "扫描后选择要清理的任务材料。"
    var selectedCleanupBytes: UInt64 { cleanupPlan?.items.filter { cleanupSelectedIDs.contains($0.id) }.reduce(0) { $0 + $1.allocatedBytes } ?? 0 }
    var conflictingPaths: [String] { (try? ConflictPolicy.conflicts(input: snapshot?.entries ?? [], existing: oldFiles)) ?? [] }
    private var previewBinding: String?
    private var flag: CancellationFlag?
    private var runner: CLIRunning?
    private var store: JobStore?
    private var current: JobRecord?
    private var currentDirectory: URL?
    private var inspectionAfterFailure = false
    var selected: Drive? { drives.first { $0.id == selectedID } }
    var writeCLIOperation: CLIOperation? { disc?.writeCLIOperation }
    var canExportDisc: Bool {
        !demo && !busy && !workerUnknown && !oldFiles.isEmpty && disc?.blank == false && disc?.unfinished == false &&
            disc?.appendable == true && ["CD-R", "DVD-R"].contains(disc?.media ?? "")
    }
    var canExportWindows: Bool {
        guard !busy, let path = current?.evidencePath else { return false }
        return FileManager.default.fileExists(atPath: URL(fileURLWithPath: path).appendingPathComponent("windows-expected.json").path)
    }
    // 禁用原因与准入使用同一判断，预览成功也不能掩盖历史写入或未知执行器锁定。
    var writeDisabledReason: String? {
        if !physicalWritesEnabled { return "此应用版本未启用物理刻录。" }
        if demo { return "模拟模式不能执行物理刻录。" }
        if busy { return "请等待当前任务结束。" }
        if workerUnknown { return "执行器退出状态未知，请先核对退出收据。" }
        if needsInspection { return "历史刻录任务尚未核对。请在“历史记录”查看失败任务；构建预览不会解除写入锁定。" }
        if !historyWarnings.isEmpty { return "任务索引有读取错误，请先查看历史记录诊断。" }
        if snapshot == nil { return "请先选择本次新增目录。" }
        if writeCLIOperation == nil { return "请先检查可写介质与目录。" }
        if previewImage == nil { return "请先构建并验证追加或首刻预览。" }
        if previewBinding != binding { return "目标或冲突策略已变化，请重新构建预览。" }
        return nil
    }
    var canWrite: Bool { writeDisabledReason == nil }
    var canManageStorage: Bool { !busy && !workerUnknown && !demo && store != nil }
    private var binding: String { "\(selectedID)|\(writeCLIOperation?.rawValue ?? "offline")|\(conflictPolicy.rawValue)|\(disc?.lastLBA ?? 0)|\(disc?.nextLBA ?? 0)|\(disc?.filesystemView ?? "")" }

    init() {
        demo = CommandLine.arguments.contains("--demo")
        do {
            let config = try Configuration.bundled(contents: Bundle.main.bundleURL.appendingPathComponent("Contents"))
            runner = CLIRunner(configuration: config)
            physicalWritesEnabled = config.allowPhysicalWrites
            store = try JobStore(directory: URL(fileURLWithPath: config.recordDirectory))
            let loaded = try store!.load(); history = loaded.records; historyWarnings = loaded.warnings
            needsInspection = history.contains { ($0.operation?.writes == true && [.needsInspection, .physical, .unknown].contains($0.phase)) || $0.requiresInspection == true }
            workerUnknown = FileManager.default.fileExists(atPath: PhysicalLease.activeMarker.path)
            if workerUnknown { phase = .unknown; detail = "存在未确认退出的物理 worker。仅有明确退出收据且系统服务正常时，才允许核对恢复。" }
            else if needsInspection { phase = .needsInspection; detail = "历史写入任务未完成。先检查介质，并使用该任务原镜像只读补验。" }
            if demo { loadDemo("appendable") }
            else { Task { await discover() } }
        } catch { phase = .failed; detail = error.localizedDescription }
    }
    init(runner: CLIRunning, store: JobStore) {
        self.runner = runner; self.store = store
        physicalWritesEnabled = true // 仅供独立 mock 验证，真实 runner 仍执行配置限制。
    }
    func changeDrive() {
        conflictPolicy = .reject
        disc = nil; oldFiles = []; selectedDiscPaths = []; previewImage = nil; previewBinding = nil
        detail = "光驱选择已变化，请重新检查介质。"
    }
    private func begin(_ operation: CLIOperation?, phase: JobPhase, message: String) throws {
        guard !busy, !workerUnknown else { throw GUIError.invalid("任务仍在执行或状态未知") }
        let record = JobRecord(phase: phase, message: message)
        let directory = try JobStore.temporaryJob(record.id)
        current = record; current?.operation = operation; current?.device = selected
        current?.workspacePath = directory.path
        current?.snapshotPath = snapshot?.directory.path
        current?.conflictPolicy = conflictPolicy
        currentDirectory = directory; lastDirectory = directory
        busy = true; self.phase = phase; detail = message; logs = ""
        try persist()
    }
    private func persist() throws {
        if let current {
            try store?.save(current)
            if let loaded = try store?.load() { history = loaded.records; historyWarnings = loaded.warnings }
        }
    }
    private func end(_ phase: JobPhase, _ message: String) async {
        self.phase = phase; detail = message; physicalActive = false
        current?.phase = phase; current?.message = message
        do {
            try persist()
            if !workerUnknown, let store, let record = current, let directory = currentDirectory {
                self.phase = .archiving; detail = "任务已结束，正在保存日志、镜像、缓存与快照并复核 hash…"
                do {
                    let archive = try await Task.detached { try store.archive(record, workspace: directory) }.value
                    current?.archivePath = archive.path
                } catch {
                    current?.archiveWarning = error.localizedDescription
                    logs += "\n材料持久化未完成：\(error.localizedDescription)。原临时材料仍保留。"
                }
                try persist()
            }
        } catch { logs += "\n任务索引保存失败：\(error.localizedDescription)" }
        self.phase = phase; detail = message
        if let warning = current?.archiveWarning { detail += "\n材料保存未完成：\(warning)；请及时导出诊断。" }
        busy = false
    }
    private func log(_ result: CLIResult, _ operation: CLIOperation) {
        logs += "\n[\(operation.rawValue)] exit=\(result.exitCode.map(String.init) ?? "unknown")\n\(result.stdout)\n\(result.stderr)"
        if logs.count > 120_000 { logs = "界面显示最近日志；完整日志见任务目录。\n" + String(logs.suffix(100_000)) }
    }
    private func run(_ request: CLIRequest) async throws -> CLIResult {
        guard let runner, let directory = currentDirectory else { throw GUIError.invalid("执行器未就绪") }
        let runDirectory = directory.appendingPathComponent("command-\(UUID().uuidString)")
        physicalActive = request.operation.physical
        if physicalActive { current?.phase = .physical; try persist() }
        let result: CLIResult
        do { result = try await runner.run(request, directory: runDirectory) }
        catch {
            if request.operation.physical && FileManager.default.fileExists(atPath: PhysicalLease.activeMarker.path) {
                workerUnknown = true
            }
            throw error
        }
        log(result, request.operation)
        if result.unknown {
            workerUnknown = true
            throw GUIError.invalid("管理员执行器未返回 CLI 退出收据；保留任务占用，请人工检查 worker 状态。")
        }
        physicalActive = false
        if result.authorizationCancelled { throw GUIError.authorizationCancelled }
        return result
    }
    private func currentDrive() async throws -> Drive {
        guard let selected else { throw GUIError.invalid("请选择光驱") }
        let found = try TextContract.drives(await run(CLIRequest(.drives)))
        guard let current = found.first(where: { $0 == selected }) else {
            disc = nil; oldFiles = []; previewBinding = nil
            throw GUIError.invalid("光驱身份或介质枚举已变化，请重新发现并检查")
        }
        return current
    }
    private func evidenceParent() throws -> URL {
        guard let directory = currentDirectory else { throw GUIError.invalid("任务目录未建立") }
        let parent = directory.appendingPathComponent("optical", isDirectory: true)
        try JobStore.secureDirectory(parent); return parent
    }
    func discover() async {
        guard !busy, !workerUnknown, !demo else { return }
        do {
            try begin(.drives, phase: .discovering, message: "正在发现原生光学设备…")
            disc = nil; oldFiles = []; selectedDiscPaths = []; previewImage = nil; previewBinding = nil; conflictPolicy = .reject
            drives = try TextContract.drives(await run(CLIRequest(.drives)))
            if drives.count == 1 { selectedID = drives[0].id }
            else if !drives.contains(where: { $0.id == selectedID }) { selectedID = "" }
            await end(.idle, drives.isEmpty ? "未发现光驱。连接后点击刷新。" : "已发现 \(drives.count) 台光驱；检查介质后可读取目录。")
        } catch { await end(workerUnknown ? .unknown : .failed, error.localizedDescription) }
    }
    func inspect() async {
        guard !busy, !workerUnknown, !demo else { return }
        do {
            try begin(.inspect, phase: .physical, message: "正在检查介质；此只读任务也需要系统管理员认证。")
            disc = nil; oldFiles = []; selectedDiscPaths = []; previewBinding = nil; previewImage = nil; conflictPolicy = .reject
            let drive = try await currentDrive(), parent = try evidenceParent()
            let result = try await run(CLIRequest(.inspect, drive: drive, evidence: parent))
            disc = try TextContract.disc(result, selected: drive)
            current?.evidencePath = try TextContract.evidence(result, under: parent).path
            if let disc, !disc.blank {
                let listing = try await run(CLIRequest(.list, drive: drive, evidence: parent))
                oldFiles = try TextContract.tree(listing, selected: drive, matching: disc)
            }
            inspectionAfterFailure = needsInspection
            await end(needsInspection ? .needsInspection : .idle, needsInspection ? "只读检查完成。仍保留写入锁定，请选择失败任务的原镜像进行补验。" : "介质检查完成。\(disc?.blank == true ? "空白盘尚无目录。" : "已读取最新 Session 目录。")")
        } catch { disc = nil; oldFiles = []; await end(workerUnknown ? .unknown : .failed, error.localizedDescription) }
    }
    func chooseInput() {
        guard !busy else { return }
        let panel = NSOpenPanel(); panel.canChooseFiles = false; panel.canChooseDirectories = true
        panel.allowsMultipleSelection = false; panel.prompt = "建立输入快照"
        panel.message = "按用户权限固定本次输入；同路径文件在预览前选择跳过或替换。"
        guard panel.runModal() == .OK, let source = panel.url else { return }
        Task { await stage(source) }
    }
    func stage(_ source: URL) async {
        do {
            try begin(nil, phase: .staging, message: "以当前用户权限读取目录，复制并复核内容…")
            snapshot = nil; previewImage = nil; previewBinding = nil; conflictPolicy = .reject; sourceName = source.lastPathComponent
            let token = CancellationFlag(); flag = token
            let securityScoped = source.startAccessingSecurityScopedResource()
            defer { if securityScoped { source.stopAccessingSecurityScopedResource() }; flag = nil }
            snapshot = try await Task.detached { try Staging.create(source: source, flag: token) }.value
            current?.snapshotPath = snapshot?.directory.path
            await end(.idle, "固定快照就绪：\(snapshot!.fileCount) 个文件，\(formatBytes(snapshot!.totalBytes))。")
        } catch { await end(.failed, error.localizedDescription) }
    }
    func cancelStaging() { flag?.cancel() }
    func chooseDiscExport() {
        guard canExportDisc else { return }
        let roots = selectedDiscPaths
        let panel = NSOpenPanel(); panel.canChooseFiles = false; panel.canChooseDirectories = true
        panel.allowsMultipleSelection = false; panel.canCreateDirectories = true; panel.prompt = "导出到此目录"
        panel.message = roots.isEmpty ? "导出光盘全部文件。将新建一个独立目录，保留相对路径，不覆盖本地文件。" : "导出选中的文件/目录（目录含子项）。将新建一个独立目录，不覆盖本地文件。"
        guard panel.runModal() == .OK, let destination = panel.url else { return }
        Task { await exportDisc(to: destination, roots: roots) }
    }
    func exportDisc(to destination: URL, roots: Set<String>) async {
        guard canExportDisc else { return }
        do {
            let entries = try DiscExport.selection(oldFiles, roots: roots)
            try begin(.export, phase: .exporting, message: "正在只读导出光盘文件；系统认证后复读光盘内容，再以用户权限拷贝到本地…")
            exportedDirectory = nil
            let accessed = destination.startAccessingSecurityScopedResource()
            defer { if accessed { destination.stopAccessingSecurityScopedResource() } }
            let drive = try await currentDrive(), parent = try evidenceParent()
            guard let directory = currentDirectory, let id = current?.id else { throw GUIError.invalid("导出任务未建立") }
            let selection = directory.appendingPathComponent("export-selection.txt")
            try DiscExport.writeSelection(entries, all: roots.isEmpty, to: selection)
            let result = try await run(CLIRequest(.export, input: selection, drive: drive, evidence: parent, expectedView: disc?.filesystemView))
            let evidence = try TextContract.evidence(result, under: parent)
            current?.evidencePath = evidence.path; try persist()
            let receipt = try DiscExport.receipt(result, evidence: evidence, selected: drive, expected: entries)
            let checkedDisc = try TextContract.disc(result, selected: drive)
            guard !checkedDisc.blank, !checkedDisc.unfinished else { throw GUIError.invalid("导出介质状态不完整，请检查") }
            disc = checkedDisc
            detail = "光盘复读与 hash 已通过；正在以当前用户权限拷贝并复核本地结果…"
            let output = try await Task.detached { try DiscExport.copyToLocal(receipt: receipt, evidence: evidence, destination: destination, jobID: id) }.value
            current?.exportPath = output.path; exportedDirectory = output
            let report: [String: Any] = ["result": "LOCAL_EXPORT_VERIFIED", "destination": output.path, "files": receipt.entries.filter { !$0.directory }.count, "bytes": receipt.entries.reduce(UInt64(0)) { $0 + $1.bytes }, "physicalWrites": 0]
            try JSONSerialization.data(withJSONObject: report, options: [.prettyPrinted, .sortedKeys]).write(to: directory.appendingPathComponent("local-export.json"), options: .withoutOverwriting)
            await end(.completed, "已导出 \(receipt.entries.filter { !$0.directory }.count) 个文件，大小与 SHA-256 全部通过。\n\(output.path)")
        } catch {
            // 导出失败不清除既有写后检查锁，也不提交任何写入或重试。
            if workerUnknown { await end(.unknown, error.localizedDescription) }
            else { await end(.failed, "导出未完成：\(error.localizedDescription)。部分缓存/本地 .partial 目录已保留；查看日志后手动重试。") }
        }
    }
    func showExportedFiles() { if let exportedDirectory { NSWorkspace.shared.selectFile(nil, inFileViewerRootedAtPath: exportedDirectory.path) } }
    func chooseConflictPolicyAndPreview() {
        guard !busy, !workerUnknown, let snapshot else { return }
        do {
            let paths = try ConflictPolicy.conflicts(input: snapshot.entries, existing: oldFiles)
            if !paths.isEmpty {
                let alert = NSAlert(); alert.messageText = "光盘已有 \(paths.count) 个同路径文件"
                alert.informativeText = paths.prefix(12).joined(separator: "\n") + (paths.count > 12 ? "\n…" : "") + "\n\n跳过：保留这些旧文件，只加入其他文件。\n替换：最新 Session 使用快照版本；消耗新空间，旧 Session 数据仍留在盘上。此选择适用于本次全部同路径文件。"
                alert.addButton(withTitle: "跳过已有文件"); alert.addButton(withTitle: "替换已有文件"); alert.addButton(withTitle: "取消")
                let answer = alert.runModal()
                guard answer == .alertFirstButtonReturn || answer == .alertSecondButtonReturn else { return }
                conflictPolicy = answer == .alertFirstButtonReturn ? .skip : .replace
                previewImage = nil; previewBinding = nil
            } else { conflictPolicy = .reject }
            Task { await preview() }
        } catch { phase = .failed; detail = error.localizedDescription }
    }
    func preview() async {
        guard !busy, !workerUnknown, !demo, let snapshot else { return }
        do {
            try begin(nil, phase: .preparing, message: "正在构建预览并验证名称、metadata、extent 与 hash…")
            previewImage = nil; previewBinding = nil
            try await Task.detached { try Staging.validate(snapshot) }.value
            let conflicts = try ConflictPolicy.conflicts(input: snapshot.entries, existing: oldFiles)
            guard conflicts.isEmpty || conflictPolicy != .reject else { throw GUIError.invalid("存在同路径文件，请先选择跳过或替换") }
            let image = currentDirectory!.appendingPathComponent("preview.iso")
            let operation: CLIOperation
            let result: CLIResult
            if let disc, !disc.blank {
                guard disc.writeCLIOperation == .burn else { throw GUIError.invalid("当前介质不可追加，预览已停止") }
                let drive = try await currentDrive(); operation = .build
                result = try await run(CLIRequest(.build, input: snapshot.directory, image: image, drive: drive, evidence: evidenceParent(), conflictPolicy: conflictPolicy, expectedView: disc.filesystemView))
            } else {
                operation = .initialBuild
                result = try await run(CLIRequest(.initialBuild, input: snapshot.directory, image: image))
            }
            current?.operation = operation
            guard result.succeeded, result.stdout.components(separatedBy: .newlines).contains(operation == .initialBuild ? "INITIAL IMAGE VERIFIED; Windows acceptance PENDING" : "BUILD VERIFIED") else { throw GUIError.invalid("预览镜像未明确通过 CLI 验证") }
            let verified = try await run(CLIRequest(.verify, image: image))
            guard verified.succeeded, verified.stdout.components(separatedBy: .newlines).contains("IMAGE VERIFIED; this does not prove Windows mounting") else { throw GUIError.invalid("独立镜像验证未通过") }
            let skipped = conflictPolicy == .skip ? Set(conflicts) : []
            let bytes = snapshot.entries.filter { !$0.directory && !skipped.contains($0.path) }.reduce(UInt64(0)) { $0 + $1.bytes }
            if let disc, bytes > disc.freeBytes { throw GUIError.invalid("新增内容超过剩余容量；核心仍负责含 metadata 的最终容量检查") }
            previewImage = image; previewBinding = binding; current?.imagePath = image.path
            await end(.completed, "预览镜像验证通过。此结果只证明镜像，实际刻录将由核心重新扫描、写入并读回同一份快照。")
        } catch { await end(workerUnknown ? .unknown : .failed, error.localizedDescription) }
    }
    func write() async {
        guard canWrite, let snapshot, let operation = writeCLIOperation else { return }
        do {
            try begin(operation, phase: .preparing, message: "正在复核快照与目标；提交后不提供强制取消。")
            try await Task.detached { try Staging.validate(snapshot) }.value
            let drive = try await currentDrive(), parent = try evidenceParent()
            // 在提交前持久化保守状态；崩溃后不能丢失可能写盘的事实。
            current?.phase = .needsInspection; current?.message = "物理写入请求将提交，等待实际读回结果"; try persist()
            needsInspection = true; phase = .physical
            detail = "核心正在准备 / 写入 / 关闭 Session / 实际读回。系统认证桥接不提供阶段流，进度不确定；请等待。"
            let result = try await run(CLIRequest(operation, input: snapshot.directory, drive: drive, evidence: parent, conflictPolicy: operation == .burn ? conflictPolicy : .reject, expectedView: operation == .burn ? disc?.filesystemView : nil))
            let evidence = try TextContract.evidence(result, under: parent)
            current?.evidencePath = evidence.path
            current?.imagePath = evidence.appendingPathComponent(operation == .initialize ? "session-first.iso" : "session-next.iso").path
            disc = try TextContract.physicalSuccess(result, request: operation, selected: drive, evidence: evidence)
            needsInspection = false; previewImage = nil; previewBinding = nil
            var message = "刻录与实际光盘读回验证完成，介质仍可追加。Windows 实机验收待完成。"
            do {
                let listing = try await run(CLIRequest(.list, drive: drive, evidence: parent))
                guard let disc else { throw GUIError.invalid("缺少刻录后介质状态") }
                oldFiles = try TextContract.tree(listing, selected: drive, matching: disc)
                selectedDiscPaths = []
            } catch { oldFiles = []; message += "\n目录刷新未完成：\(error.localizedDescription)" }
            await end(workerUnknown ? .unknown : .completed, message)
        } catch {
            if case GUIError.authorizationCancelled = error { needsInspection = false }
            if let parent = try? evidenceParent(), let runs = try? FileManager.default.contentsOfDirectory(at: parent, includingPropertiesForKeys: nil), runs.count == 1 {
                current?.evidencePath = runs[0].path
                let name = operation == .initialize ? "session-first.iso" : "session-next.iso"
                if FileManager.default.fileExists(atPath: runs[0].appendingPathComponent(name).path) { current?.imagePath = runs[0].appendingPathComponent(name).path }
            }
            disc = nil; previewImage = nil; previewBinding = nil
            await end(workerUnknown ? .unknown : (needsInspection ? .needsInspection : .failed), error.localizedDescription + (needsInspection ? "。不要重新刻录，请先检查介质。" : ""))
        }
    }
    func chooseReadback() {
        guard !busy, !workerUnknown, selected != nil, !demo else { return }
        let panel = NSOpenPanel(); panel.canChooseDirectories = false; panel.allowedContentTypes = [.data]
        panel.prompt = "只读补验"; panel.message = "选择原任务的 ISO；同目录 manifest 与旧 cache 依赖须保留。不会刻录。"
        if let recent = history.first(where: { $0.phase == .needsInspection }), let path = recent.imagePath { panel.directoryURL = URL(fileURLWithPath: path).deletingLastPathComponent() }
        guard panel.runModal() == .OK, let image = panel.url else { return }
        Task { await readback(image) }
    }
    func readback(_ image: URL) async {
        do {
            try begin(.readback, phase: .preparing, message: "以用户权限固定原镜像与 manifest，然后从实际光盘只读补验…")
            guard !needsInspection || inspectionAfterFailure else { throw GUIError.invalid("写入失败后先点击检查介质，再选择原镜像补验") }
            guard image.pathExtension == "iso", FileManager.default.fileExists(atPath: image.path + ".manifest") else { throw GUIError.invalid("原 ISO 或同名 manifest 缺失") }
            let scoped = image.startAccessingSecurityScopedResource()
            defer { if scoped { image.stopAccessingSecurityScopedResource() } }
            let materialDirectory = currentDirectory!.appendingPathComponent("readback-input")
            let captured = try await Task.detached { try Staging.readbackMaterials(image: image, directory: materialDirectory) }.value
            let drive = try await currentDrive(), parent = try evidenceParent()
            phase = .physical
            let result = try await run(CLIRequest(.readback, image: captured, drive: drive, evidence: parent))
            let evidence = try TextContract.evidence(result, under: parent)
            disc = try TextContract.physicalSuccess(result, request: .readback, selected: drive, evidence: evidence)
            current?.imagePath = image.path; current?.evidencePath = evidence.path
            // 只解除原镜像对应且同光驱的失败记录，不能用另一张盘的 PASS 覆盖它。
            for var record in history where record.operation?.writes == true && [.needsInspection, .physical].contains(record.phase) &&
                record.imagePath.map({ URL(fileURLWithPath: $0).resolvingSymlinksInPath().path }) == image.resolvingSymlinksInPath().path && record.device?.id == drive.id {
                record.phase = .completed; record.requiresInspection = false; record.message = "原镜像实际光盘只读补验通过；Windows 待验收"; try store?.save(record)
            }
            history = try store?.records() ?? []
            needsInspection = history.contains { $0.requiresInspection == true || ($0.operation?.writes == true && [.needsInspection, .physical, .unknown].contains($0.phase)) }
            previewImage = nil; previewBinding = nil
            await end(.completed, "实际光盘只读补验通过，仍可追加。Windows 实机验收待完成。")
        } catch { await end(workerUnknown ? .unknown : (needsInspection ? .needsInspection : .failed), error.localizedDescription) }
    }
    func openLogs(_ path: String? = nil) {
        if let url = path.map({ URL(fileURLWithPath: $0) }) ?? current?.archivePath.map({ URL(fileURLWithPath: $0) }) ?? lastDirectory { NSWorkspace.shared.open(url) }
    }
    private var cleanupProtection: (Set<UUID>, [String]) {
        (Set(current.map { [$0.id] } ?? []), [snapshot?.directory.path, previewImage?.path, currentDirectory?.path].compactMap { $0 })
    }
    func invalidateCleanupPlan() { cleanupPlan = nil; cleanupSelectedIDs = []; cleanupMessage = "选项已变化，请重新扫描。" }
    func scanCleanup() async {
        guard canManageStorage, let store else { return }
        busy = true; cleanupWorking = true; cleanupPlan = nil; cleanupSelectedIDs = []
        cleanupMessage = "正在统计本地占用，不读取光盘…"
        defer { busy = false; cleanupWorking = false }
        let policy = CleanupPolicy(mode: cleanupMode, retentionDays: cleanupRetentionDays)
        let (ids, paths) = cleanupProtection
        do {
            let plan = try await Task.detached { try CleanupManager(store: store).scan(policy: policy, protectedIDs: ids, protectedPaths: paths) }.value
            cleanupPlan = plan; cleanupSelectedIDs = Set(plan.items.map { $0.id })
            cleanupMessage = plan.items.isEmpty ? "没有符合条件的可清理材料。" : "已扫描，选择的材料将在确认后永久删除。"
        } catch { cleanupMessage = error.localizedDescription }
    }
    func cleanStorage() async {
        guard canManageStorage, let store, let plan = cleanupPlan, !cleanupSelectedIDs.isEmpty else { return }
        busy = true; cleanupWorking = true
        cleanupMessage = "正在复核任务状态与归档内容，然后清理所选材料…"
        defer { busy = false; cleanupWorking = false }
        let (ids, paths) = cleanupProtection, selected = cleanupSelectedIDs
        do {
            let result = try await Task.detached { try CleanupManager(store: store).clean(plan, selectedIDs: selected, protectedIDs: ids, protectedPaths: paths) }.value
            let loaded = try store.load(); history = loaded.records; historyWarnings = loaded.warnings
            cleanupMessage = "已清理 \(result.jobs) 个任务的材料，预计释放 \(formatBytes(result.estimatedFreedBytes))。" + (result.errors.isEmpty ? "" : "\n" + result.errors.joined(separator: "\n"))
            detail = cleanupMessage
        } catch { cleanupMessage = error.localizedDescription }
        cleanupPlan = nil; cleanupSelectedIDs = []
    }
    func readbackFailedJob(_ record: JobRecord) async {
        guard !busy, !workerUnknown, let imagePath = record.imagePath else { return }
        do {
            if !FileManager.default.fileExists(atPath: imagePath), let store {
                busy = true; phase = .archiving; detail = "正在核对归档并还原本任务材料；不会覆盖不同内容…"
                try await Task.detached { try store.restore(record) }.value
                busy = false
            }
            await readback(URL(fileURLWithPath: imagePath))
        } catch { busy = false; phase = .needsInspection; detail = "原材料无法安全恢复：\(error.localizedDescription)" }
    }
    func exportDiagnostics(_ record: JobRecord) {
        guard !busy, !workerUnknown, record.archivePath != nil, let store else { return }
        let panel = NSOpenPanel(); panel.canChooseFiles = false; panel.canChooseDirectories = true; panel.canCreateDirectories = true
        panel.prompt = "保存诊断材料"; panel.message = "保存此任务的原始日志、镜像、缓存及输入文件。可能包含文件内容与本地路径；请选择自己控制的本地目录。不会自动上传。"
        guard panel.runModal() == .OK, let target = panel.url else { return }
        Task {
            busy = true; phase = .archiving
            let scoped = target.startAccessingSecurityScopedResource(); defer { if scoped { target.stopAccessingSecurityScopedResource() } }
            do {
                let destination = target.appendingPathComponent("BurnLight-diagnostics-\(record.id.uuidString)")
                try await Task.detached {
                    let (archive, _) = try store.validatedArchive(record)
                    guard !FileManager.default.fileExists(atPath: destination.path) else { throw GUIError.invalid("诊断目标已存在，不覆盖") }
                    try FileManager.default.copyItem(at: archive, to: destination)
                    let encoder = JSONEncoder(); encoder.dateEncodingStrategy = .iso8601; encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
                    try encoder.encode(record).write(to: destination.appendingPathComponent("job.json"), options: .withoutOverwriting)
                }.value
                phase = .completed; detail = "诊断材料已保存：\(destination.path)；原 manifest 的绝对路径保留。"
            } catch { phase = .failed; detail = error.localizedDescription }
            busy = false
        }
    }
    func checkWorkerExit() async {
        guard workerUnknown, !busy else { return }
        var recovered: RecoveryReceipt?
        busy = true; detail = "正在核对明确退出收据与光盘 worker / Disk Arbitration 状态；不会结束任何进程…"
        do {
            let proof = try await Task.detached { try PhysicalLease.recoverKnownExit() }.value
            recovered = proof
            let record = try proof.record(existing: history.first { $0.id == proof.jobID })
            current = record; currentDirectory = proof.directory.deletingLastPathComponent(); lastDirectory = currentDirectory
            try persist()
            workerUnknown = false
            needsInspection = history.contains { $0.requiresInspection == true || ($0.operation?.writes == true && [.needsInspection, .physical, .unknown].contains($0.phase)) }
            await end(needsInspection ? .needsInspection : .failed, record.message)
            disc = nil; oldFiles = []; previewImage = nil; previewBinding = nil
            if drives.isEmpty { await discover() }
        } catch {
            if let proof = recovered { _ = try? PhysicalLease(jobDirectory: proof.directory) }
            busy = false; phase = .unknown; detail = error.localizedDescription
        }
    }
    func exportWindows() {
        guard !busy, let path = current?.evidencePath else { return }
        let source = URL(fileURLWithPath: path).appendingPathComponent("windows-expected.json")
        guard FileManager.default.fileExists(atPath: source.path) else { detail = "本任务尚无 Windows 预期清单"; return }
        let panel = NSSavePanel(); panel.nameFieldStringValue = "windows-expected.json"
        guard panel.runModal() == .OK, let destination = panel.url else { return }
        do { try Data(contentsOf: source).write(to: destination, options: .atomic); detail = "Windows 预期清单已导出；原 ISO/manifest/cache 保留在任务目录。" }
        catch { detail = error.localizedDescription }
    }
    func loadDemo(_ scenario: String) {
        guard demo else { return }
        let text = "Device: 1,0,0\nDrive: Lenovo Ultraslim DVD XL6M\nOptical BSD node: /dev/disk4\nMedia: CD-R\nRegistry ID: 4295022013\n"
        let drive = try! TextContract.drives(CLIResult(exitCode: 0, stdout: text, stderr: "", logDirectory: URL(fileURLWithPath: "/private/tmp")))[0]
        drives = scenario == "none" ? [] : [drive]; selectedID = drives.first?.id ?? ""; oldFiles = []; snapshot = nil
        let blank = scenario == "blank", unfinished = scenario == "unfinished", closed = scenario == "closed"
        let output = "Drive: \(drive.name)\nDevice: \(drive.device)\nMedia: CD-R\nSessions: \(blank ? 0 : 2)\nTracks: \(blank ? 0 : 2)\nOptical BSD node: /dev/disk4\nRegistry ID: \(drive.registryID)\nWrite speed: 10x\nLast session LBA: \(blank ? 0 : 11702)\nNext writable LBA: \(blank ? 0 : 18904)\nFree space: 340939 blocks / 698243072 bytes\nAppendable: \(closed ? "no" : "yes")\nBlank: \(blank ? "yes" : "no")\nPhysical next Track: 3\nUnfinished Session: \(unfinished ? "yes" : "no")\n"
        disc = scenario == "none" || scenario == "empty" ? nil : try? TextContract.disc(CLIResult(exitCode: 0, stdout: output, stderr: "", logDirectory: URL(fileURLWithPath: "/private/tmp")), selected: drive)
        if !blank && disc != nil {
            oldFiles = [FileEntry(path: "hello-from-macos.txt", directory: false, bytes: 25), FileEntry(path: "swagger.json", directory: false, bytes: 246137), FileEntry(path: "中文目录", directory: true, bytes: 0), FileEntry(path: "中文目录/MixedCase.txt", directory: false, bytes: 39), FileEntry(path: "中文首刻.txt", directory: false, bytes: 40)]
        }
        phase = .idle; detail = "模拟场景：\(scenario)。仅用于界面检查，物理按钮已禁用。"; logs = output
    }
}

func formatBytes(_ bytes: UInt64) -> String { ByteCountFormatter.string(fromByteCount: Int64(clamping: bytes), countStyle: .file) }
