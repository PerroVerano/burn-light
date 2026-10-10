import Foundation

public enum GUIError: LocalizedError {
    case invalid(String)
    case authorizationCancelled
    public var errorDescription: String? {
        switch self {
        case .invalid(let text): return text
        case .authorizationCancelled: return "系统管理员认证已取消，未启动 CLI"
        }
    }
}

public struct Drive: Identifiable, Equatable, Codable, Sendable {
    public var id: String { registryID }
    public let device: String
    public let name: String
    public let bsdNode: String
    public let media: String
    public let registryID: String
}

public struct Disc: Equatable, Codable, Sendable {
    public let drive: Drive
    public let media: String
    public let sessions: UInt64
    public let tracks: UInt64
    public let freeBytes: UInt64
    public let appendable: Bool
    public let blank: Bool
    public let unfinished: Bool
    // 地址只用于展示与结果核对；不作为 GUI 命令参数传回核心。
    public let lastLBA: UInt64
    public let nextLBA: UInt64
    public var writeCLIOperation: CLIOperation? {
        guard ["CD-R", "DVD-R"].contains(media), appendable, !unfinished, freeBytes > 0 else { return nil }
        if blank { return sessions == 0 && tracks == 0 && lastLBA == 0 && nextLBA == 0 ? .initialize : nil }
        return sessions > 0 && tracks > 0 && nextLBA > lastLBA ? .burn : nil
    }
}

public struct FileEntry: Identifiable, Equatable, Codable, Sendable {
    public var id: String { path }
    public let path: String
    public let directory: Bool
    public let bytes: UInt64
    public let sha256: String?
    public init(path: String, directory: Bool, bytes: UInt64, sha256: String? = nil) {
        self.path = path; self.directory = directory; self.bytes = bytes; self.sha256 = sha256
    }
}

public enum CLIOperation: String, Codable, Sendable {
    case drives, inspect, list, initialBuild = "init-build", build, verify, initialize = "init", burn, readback, export
    public var physical: Bool { ![.drives, .initialBuild, .verify].contains(self) }
    public var writes: Bool { self == .initialize || self == .burn }
}

public enum ConflictPolicy: String, Codable, Sendable {
    case reject, skip, replace
    public var label: String { self == .skip ? "跳过同路径文件" : self == .replace ? "替换同路径文件" : "尚未选择" }
    public static func conflicts(input: [FileEntry], existing: [FileEntry]) throws -> [String] {
        var old: [String: FileEntry] = [:]
        for entry in existing {
            guard old.updateValue(entry, forKey: entry.path) == nil else { throw GUIError.invalid("光盘目录出现重复路径") }
        }
        var paths: [String] = []
        for entry in input {
            guard let match = old[entry.path] else { continue }
            guard entry.directory == match.directory else { throw GUIError.invalid("目录与文件类型冲突：\(entry.path)") }
            if !entry.directory { paths.append(entry.path) }
        }
        return paths.sorted()
    }
}

public struct CLIRequest: Sendable {
    public let operation: CLIOperation
    public let arguments: [String]
    public init(_ operation: CLIOperation, input: URL? = nil, image: URL? = nil, drive: Drive? = nil, evidence: URL? = nil, conflictPolicy: ConflictPolicy = .reject) throws {
        var args = [operation.rawValue]
        switch operation {
        case .initialBuild, .build:
            guard let input, let image else { throw GUIError.invalid("缺少快照或预览镜像位置") }
            args += [input.path, image.path]
        case .initialize, .burn:
            guard let input else { throw GUIError.invalid("缺少固定输入快照") }
            args.append(input.path)
        case .verify, .readback:
            guard let image else { throw GUIError.invalid("缺少原镜像及 manifest") }
            args.append(image.path)
        case .export:
            guard let input, let drive else { throw GUIError.invalid("缺少导出选择及光驱身份") }
            args += ["--selection", input.path, "--registry-id", drive.registryID, "--bsd-node", drive.bsdNode, "--media", drive.media]
        default: break
        }
        if conflictPolicy != .reject {
            guard [.build, .burn].contains(operation) else { throw GUIError.invalid("重名策略仅适用于追加预览与追加刻录") }
            args += ["--on-conflict", conflictPolicy.rawValue]
        }
        if operation.physical {
            guard let drive, let evidence, TextContract.matches(drive.device, "^[0-9]+,[0-9]+,[0-9]+$") else {
                throw GUIError.invalid("必须明确选择光驱及本任务记录目录")
            }
            args += ["--device", drive.device, "--work-dir", evidence.path]
        }
        self.operation = operation; self.arguments = args
    }
}

public enum JobPhase: String, Codable, Sendable {
    case idle, discovering, staging, preparing, physical, exporting, archiving, completed, failed, needsInspection, unknown
    public var label: String {
        switch self {
        case .idle: return "等待操作"
        case .discovering: return "正在发现光驱"
        case .staging: return "正在建立输入快照"
        case .preparing: return "正在构建并验证预览"
        case .physical: return "正在执行光盘任务"
        case .exporting: return "正在导出到本地"
        case .archiving: return "正在保存任务材料"
        case .completed: return "任务完成"
        case .failed: return "任务未完成"
        case .needsInspection: return "写入后需要检查"
        case .unknown: return "执行器状态未知 · 已锁定"
        }
    }
}

public struct CLIResult: Sendable {
    public let exitCode: Int32?
    public let stdout: String
    public let stderr: String
    public let logDirectory: URL
    public let authorizationCancelled: Bool
    public var unknown: Bool { exitCode == nil && !authorizationCancelled }
    public var succeeded: Bool { exitCode == 0 }
    public init(exitCode: Int32?, stdout: String, stderr: String, logDirectory: URL, authorizationCancelled: Bool = false) {
        self.exitCode = exitCode; self.stdout = stdout; self.stderr = stderr
        self.logDirectory = logDirectory; self.authorizationCancelled = authorizationCancelled
    }
}

public struct JobRecord: Codable, Identifiable, Sendable {
    public let id: UUID
    public let created: Date
    public var phase: JobPhase
    public var operation: CLIOperation?
    public var device: Drive?
    public var snapshotPath: String?
    public var imagePath: String?
    public var evidencePath: String?
    public var exportPath: String?
    public var workspacePath: String?
    public var archivePath: String?
    public var archiveWarning: String?
    public var requiresInspection: Bool?
    public var conflictPolicy: ConflictPolicy?
    public var message: String
    public init(id: UUID = UUID(), phase: JobPhase = .idle, message: String = "") {
        self.id = id; created = Date(); self.phase = phase; self.message = message
    }
}
