import Foundation
import Darwin

public final class JobStore: Sendable {
    public let directory: URL
    public init(directory: URL) throws { self.directory = directory; try Self.secureDirectory(directory) }
    public static func secureDirectory(_ url: URL) throws {
        if !FileManager.default.fileExists(atPath: url.path) {
            try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true, attributes: [.posixPermissions: 0o700])
        }
        var s = stat()
        guard lstat(url.path, &s) == 0, s.st_mode & S_IFMT == S_IFDIR, s.st_uid == getuid(), s.st_mode & 0o077 == 0 else {
            throw GUIError.invalid("任务目录须为当前用户私有目录：\(url.path)")
        }
    }
    public static func temporaryJob(_ id: UUID) throws -> URL {
        let path = URL(fileURLWithPath: "/private/tmp/burnlight-job-\(id.uuidString)", isDirectory: true)
        try secureDirectory(path); return path
    }
    public func save(_ record: JobRecord) throws {
        let encoder = JSONEncoder(); encoder.outputFormatting = [.prettyPrinted, .sortedKeys]; encoder.dateEncodingStrategy = .iso8601
        try encoder.encode(record).write(to: directory.appendingPathComponent(record.id.uuidString + ".json"), options: .atomic)
    }
    public func records() throws -> [JobRecord] {
        try load().records
    }
    public func load() throws -> (records: [JobRecord], warnings: [String]) {
        let decoder = JSONDecoder(); decoder.dateDecodingStrategy = .iso8601
        let files = try FileManager.default.contentsOfDirectory(at: directory, includingPropertiesForKeys: nil).filter { $0.pathExtension == "json" }
        guard files.count <= 10_000 else { throw GUIError.invalid("历史记录数量超限，请保存诊断后整理") }
        var records: [JobRecord] = [], warnings: [String] = []
        for file in files {
            do {
                let fd = open(file.path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)
                guard fd >= 0 else { throw GUIError.invalid("索引不能读取") }; defer { close(fd) }
                let s = try Staging.info(fd)
                guard s.st_mode & S_IFMT == S_IFREG, s.st_size <= 8 * 1024 * 1024 else { throw GUIError.invalid("索引不是普通文件或超过大小限制") }
                let data = try FileHandle(fileDescriptor: dup(fd), closeOnDealloc: true).readToEnd() ?? Data()
                let record = try decoder.decode(JobRecord.self, from: data)
                guard file.deletingPathExtension().lastPathComponent == record.id.uuidString else { throw GUIError.invalid("记录身份与文件名不一致") }
                records.append(record)
            } catch { warnings.append(file.lastPathComponent + ": " + error.localizedDescription) }
        }
        return (records.sorted { $0.created > $1.created }, warnings)
    }
}
