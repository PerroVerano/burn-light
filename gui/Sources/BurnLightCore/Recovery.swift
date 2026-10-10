import Foundation
import Darwin

public struct RecoveryReceipt: Sendable {
    public let jobID: UUID
    public let directory: URL
    public let exitCode: Int32
    public func record(existing: JobRecord? = nil) throws -> JobRecord {
        var record = existing ?? JobRecord(id: jobID)
        guard record.id == jobID else { throw GUIError.invalid("退出收据与任务索引不匹配") }
        let workspace = directory.deletingLastPathComponent()
        if record.operation == nil,
           let data = try? Data(contentsOf: directory.appendingPathComponent("request.json")),
           let request = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
           let operation = request["operation"] as? String { record.operation = CLIOperation(rawValue: operation) }
        record.requiresInspection = record.operation?.writes != false
        record.phase = record.requiresInspection == true ? .needsInspection : .failed
        record.workspacePath = workspace.path
        record.message = "已确认 CLI 退出（\(exitCode)）；未推断光盘任务成功。请检查介质，写入任务须使用原镜像只读补验。"
        // 仅在本任务唯一的 run 中寻找原镜像；多份或符号链接材料不猜测。
        if record.operation?.writes == true {
            let parent = workspace.appendingPathComponent("optical")
            let runs = (try? FileManager.default.contentsOfDirectory(at: parent, includingPropertiesForKeys: nil)) ?? []
            if runs.count == 1, let run = runs.first,
               TextContract.matches(run.lastPathComponent, "^run-[A-Za-z0-9]{6}$"),
               run.standardizedFileURL.path == run.resolvingSymlinksInPath().path {
                var info = stat()
                if lstat(run.path, &info) == 0, info.st_mode & S_IFMT == S_IFDIR {
                    record.evidencePath = run.path
                    let image = run.appendingPathComponent(record.operation == .initialize ? "session-first.iso" : "session-next.iso")
                    if lstat(image.path, &info) == 0, info.st_mode & S_IFMT == S_IFREG { record.imagePath = image.path }
                }
            }
        }
        return record
    }
}

extension PhysicalLease {
    public static func systemIsQuiescent() throws -> Bool {
        let process = Process(), pipe = Pipe()
        process.executableURL = URL(fileURLWithPath: "/bin/ps")
        process.arguments = ["-axo", "stat=,comm="]; process.standardOutput = pipe
        process.standardError = FileHandle.nullDevice
        try process.run()
        let data = try pipe.fileHandleForReading.readToEnd() ?? Data(); process.waitUntilExit()
        guard process.terminationStatus == 0, let text = String(data: data, encoding: .utf8) else { throw GUIError.invalid("无法核对 worker 与系统服务状态") }
        let workers: Set<String> = ["appendcd", "appendcd-cdrecord", "appendcd-initial-cdrecord", "burnlight-export", "readcd", "osascript"]
        var daemonReady = false
        for line in text.components(separatedBy: .newlines) {
            let fields = line.split(maxSplits: 1, whereSeparator: { $0 == " " || $0 == "\t" })
            guard fields.count == 2 else { continue }
            let state = String(fields[0]), name = URL(fileURLWithPath: String(fields[1]).trimmingCharacters(in: .whitespaces)).lastPathComponent
            if workers.contains(name) { return false }
            if name == "diskarbitrationd" { daemonReady = !state.contains("T") && !state.contains("Z") }
        }
        return daemonReady
    }
    // 仅接受明确退出收据和正常系统状态；不杀进程、不发服务信号、不提供强制解锁。
    public static func recoverKnownExit(root: URL? = nil, quiescent: () throws -> Bool = systemIsQuiescent) throws -> RecoveryReceipt {
        let root = root ?? activeMarker.deletingLastPathComponent()
        try JobStore.secureDirectory(root)
        let lock = open(root.appendingPathComponent("physical.lock").path, O_RDWR | O_NOFOLLOW | O_CLOEXEC)
        guard lock >= 0 else { throw GUIError.invalid("任务锁不存在，无法恢复") }; defer { close(lock) }
        guard flock(lock, LOCK_EX | LOCK_NB) == 0 else { throw GUIError.invalid("另一个 GUI 仍持有物理任务锁") }
        defer { flock(lock, LOCK_UN) }
        let marker = root.appendingPathComponent("active.json")
        let input = open(marker.path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)
        guard input >= 0 else { throw GUIError.invalid("没有可核对的活动任务标记") }; defer { close(input) }
        let original = try Staging.info(input)
        guard original.st_mode & S_IFMT == S_IFREG, original.st_uid == getuid(), original.st_size <= 16 * 1024 else { throw GUIError.invalid("活动任务标记不可信") }
        let data = try FileHandle(fileDescriptor: dup(input), closeOnDealloc: true).readToEnd() ?? Data()
        guard let metadata = try JSONSerialization.jsonObject(with: data) as? [String: String], let path = metadata["jobDirectory"] else { throw GUIError.invalid("活动任务标记不完整") }
        let directory = URL(fileURLWithPath: path).resolvingSymlinksInPath()
        guard TextContract.matches(directory.path, "^/(private/)?tmp/burnlight-job-[A-Fa-f0-9-]{36}/command-[A-Fa-f0-9-]{36}$"),
              let id = UUID(uuidString: String(directory.deletingLastPathComponent().lastPathComponent.dropFirst("burnlight-job-".count))) else {
            throw GUIError.invalid("活动任务不属于 GUI 私有任务")
        }
        try JobStore.secureDirectory(directory)
        let fd = open(directory.appendingPathComponent("exit.code").path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC)
        guard fd >= 0 else { throw GUIError.invalid("CLI 尚无明确退出收据，继续保持锁定") }; defer { close(fd) }
        let info = try Staging.info(fd)
        guard info.st_mode & S_IFMT == S_IFREG, info.st_size <= 4 else { throw GUIError.invalid("退出收据格式无效") }
        let receipt = try FileHandle(fileDescriptor: dup(fd), closeOnDealloc: true).readToEnd() ?? Data()
        guard let text = String(data: receipt, encoding: .utf8), TextContract.matches(text, "^[0-9]{1,3}\n$"),
              let code = Int32(text.trimmingCharacters(in: .newlines)), code <= 255 else { throw GUIError.invalid("退出收据格式无效") }
        guard try quiescent() else { throw GUIError.invalid("仍有光盘/认证 worker，或 Disk Arbitration 状态未正常；继续保持锁定") }
        var current = stat()
        guard lstat(marker.path, &current) == 0, Staging.same(original, current) else { throw GUIError.invalid("活动标记在核对期间变化") }
        try FileManager.default.removeItem(at: marker)
        return RecoveryReceipt(jobID: id, directory: directory, exitCode: code)
    }
}
