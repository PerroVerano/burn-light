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
    // 归档快照可能包含用户刻录过的 .app；“.noindex” 让 Spotlight / 启动台不再收录这些副本。
    public var archiveBase: URL { directory.deletingLastPathComponent().appendingPathComponent("Archives.noindex", isDirectory: true) }
    var legacyArchiveBase: URL { directory.deletingLastPathComponent().appendingPathComponent("Archives", isDirectory: true) }
    // 幂等迁移：整体改名旧 Archives，并把索引里指向旧位置的归档路径改到新位置。中途中断后再次启动会继续完成。
    @discardableResult public func migrateArchives() throws -> Int {
        let fm = FileManager.default
        var legacy = stat()
        if lstat(legacyArchiveBase.path, &legacy) == 0 {
            guard legacy.st_mode & S_IFMT == S_IFDIR else { throw GUIError.invalid("旧归档位置不是目录，未迁移") }
            guard !fm.fileExists(atPath: archiveBase.path) else { throw GUIError.invalid("新旧归档目录同时存在，未自动合并：\(legacyArchiveBase.path)") }
            try Self.secureDirectory(legacyArchiveBase)
            guard rename(legacyArchiveBase.path, archiveBase.path) == 0 else { throw Staging.error("归档目录迁移失败") }
        }
        var moved = 0
        let old = legacyArchiveBase.path + "/"
        for var record in try load().records {
            guard let path = record.archivePath, path.hasPrefix(old) else { continue }
            let name = String(path.dropFirst(old.count))
            guard name == record.id.uuidString, fm.fileExists(atPath: archiveBase.appendingPathComponent(name).path) else { continue }
            record.archivePath = archiveBase.appendingPathComponent(name).path
            try save(record); moved += 1
        }
        return moved
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
