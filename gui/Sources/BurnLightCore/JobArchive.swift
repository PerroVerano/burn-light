import Foundation
import Darwin

public struct ArchiveIndex: Codable, Sendable {
    public let schema: Int
    public let jobID: UUID
    public let roots: [String: String]
    public let entries: [FileEntry]
    public let omitted: [String]
}

extension JobStore {
    private static func archiveTree(_ source: Int32, _ target: Int32, prefix: String, entries: inout [FileEntry], omitted: inout [String]) throws {
        let names = try Staging.names(source)
        for name in names {
            guard !name.unicodeScalars.contains(where: { CharacterSet.controlCharacters.contains($0) }) else { throw GUIError.invalid("任务材料含不安全文件名") }
            var s = stat(); guard fstatat(source, name, &s, AT_SYMLINK_NOFOLLOW) == 0 else { throw Staging.error("任务材料已变化") }
            let directory = s.st_mode & S_IFMT == S_IFDIR
            guard directory || s.st_mode & S_IFMT == S_IFREG else { throw GUIError.invalid("任务材料含 symlink 或特殊文件：\(prefix + name)") }
            let input = openat(source, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK | (directory ? O_DIRECTORY : 0))
            if input < 0, errno == EACCES, !directory { omitted.append(prefix + name); continue }
            guard input >= 0 else { throw Staging.error("任务材料无法打开") }; defer { close(input) }
            let before = try Staging.info(input)
            guard Staging.same(s, before) else { throw GUIError.invalid("任务材料被替换") }
            let path = prefix + name
            if directory {
                guard mkdirat(target, name, 0o700) == 0 else { throw Staging.error("无法建立归档目录") }
                let output = openat(target, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)
                guard output >= 0 else { throw Staging.error("无法打开归档目录") }; defer { close(output) }
                entries.append(FileEntry(path: path, directory: true, bytes: 0))
                try archiveTree(input, output, prefix: path + "/", entries: &entries, omitted: &omitted)
            } else {
                let output = openat(target, name, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0o600)
                guard output >= 0 else { throw Staging.error("无法建立归档文件") }; defer { close(output) }
                guard fcopyfile(input, output, nil, copyfile_flags_t(COPYFILE_DATA | COPYFILE_DATA_SPARSE)) == 0 else { throw Staging.error("任务归档复制失败") }
                let original = try DiscExport.hash(input), copied = try DiscExport.hash(output)
                guard original == copied, Staging.same(before, try Staging.info(input)), fsync(output) == 0, fchmod(output, 0o400) == 0 else { throw GUIError.invalid("任务归档 hash 或源文件状态变化") }
                entries.append(FileEntry(path: path, directory: false, bytes: copied.0, sha256: copied.1))
            }
        }
        guard names == (try Staging.names(source)) else { throw GUIError.invalid("任务材料在归档期间变化") }
    }
    public func archive(_ record: JobRecord, workspace: URL) throws -> URL {
        guard ![.physical, .unknown].contains(record.phase), workspace.resolvingSymlinksInPath().path == (try JobStore.temporaryJob(record.id)).resolvingSymlinksInPath().path else {
            throw GUIError.invalid("活动/未知任务不能归档，或任务位置不一致")
        }
        let base = directory.deletingLastPathComponent().appendingPathComponent("Archives", isDirectory: true)
        try Self.secureDirectory(base)
        let final = base.appendingPathComponent(record.id.uuidString, isDirectory: true)
        if FileManager.default.fileExists(atPath: final.path) {
            var existing = record; existing.archivePath = final.path
            _ = try validatedArchive(existing); return final
        }
        let pending = base.appendingPathComponent(".\(record.id.uuidString)-\(UUID().uuidString).partial", isDirectory: true)
        try Self.secureDirectory(pending)
        var roots = ["workspace": workspace.path], entries: [FileEntry] = [], omitted: [String] = []
        if let snapshot = record.snapshotPath {
            let source = URL(fileURLWithPath: snapshot).deletingLastPathComponent()
            guard TextContract.matches(source.resolvingSymlinksInPath().path, "^/(private/)?tmp/burnlight-input-[A-Za-z0-9]{6}$") else { throw GUIError.invalid("快照归档只能使用 GUI 私有快照") }
            roots["snapshot"] = source.path
        }
        for (name, original) in roots.sorted(by: { $0.key < $1.key }) {
            let source = open(original, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)
            guard source >= 0 else { throw Staging.error("任务源目录缺失") }; defer { close(source) }
            let destination = pending.appendingPathComponent(name)
            try Self.secureDirectory(destination)
            let output = open(destination.path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)
            guard output >= 0 else { throw Staging.error("无法打开归档目标") }; defer { close(output) }
            entries.append(FileEntry(path: name, directory: true, bytes: 0))
            try Self.archiveTree(source, output, prefix: name + "/", entries: &entries, omitted: &omitted)
        }
        let index = ArchiveIndex(schema: 1, jobID: record.id, roots: roots, entries: entries.sorted { $0.path < $1.path }, omitted: omitted)
        let encoder = JSONEncoder(); encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        try encoder.encode(index).write(to: pending.appendingPathComponent("archive.json"), options: .withoutOverwriting)
        try FileManager.default.moveItem(at: pending, to: final)
        return final
    }
    public func validatedArchive(_ record: JobRecord) throws -> (URL, ArchiveIndex) {
        guard let path = record.archivePath else { throw GUIError.invalid("任务尚未归档") }
        let archive = URL(fileURLWithPath: path).resolvingSymlinksInPath()
        let base = directory.deletingLastPathComponent().appendingPathComponent("Archives").resolvingSymlinksInPath()
        guard archive.deletingLastPathComponent() == base, archive.lastPathComponent == record.id.uuidString else { throw GUIError.invalid("归档不属于本应用任务") }
        try Self.secureDirectory(archive)
        let fd = open(archive.appendingPathComponent("archive.json").path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)
        guard fd >= 0 else { throw Staging.error("归档索引不可读") }; defer { close(fd) }
        let info = try Staging.info(fd)
        guard info.st_mode & S_IFMT == S_IFREG, info.st_size <= 8 * 1024 * 1024 else { throw GUIError.invalid("归档索引大小或类型无效") }
        let data = try FileHandle(fileDescriptor: dup(fd), closeOnDealloc: true).readToEnd() ?? Data()
        let index = try JSONDecoder().decode(ArchiveIndex.self, from: data)
        guard index.schema == 1, index.jobID == record.id, Set(index.roots.keys).isSubset(of: ["workspace", "snapshot"]), index.roots["workspace"] != nil else { throw GUIError.invalid("归档版本或身份无效") }
        try DiscExport.validate(index.entries, expected: index.entries)
        guard Set(index.entries.map { $0.path.split(separator: "/").first.map(String.init) ?? "" }) == Set(index.roots.keys) else { throw GUIError.invalid("归档根目录不一致") }
        for (label, original) in index.roots {
            let url = URL(fileURLWithPath: original).resolvingSymlinksInPath()
            let expected = URL(fileURLWithPath: "/private/tmp/burnlight-job-\(record.id.uuidString)").resolvingSymlinksInPath()
            let snapshot = record.snapshotPath.map { URL(fileURLWithPath: $0).deletingLastPathComponent().resolvingSymlinksInPath() }
            guard label == "workspace" ? url.path == expected.path : (url.path == snapshot?.path && TextContract.matches(url.path, "^/(private/)?tmp/burnlight-input-[A-Za-z0-9]{6}$")) else { throw GUIError.invalid("拒绝归档中的外部还原目标") }
            let content = index.entries.filter { $0.path.hasPrefix(label + "/") }.map { FileEntry(path: String($0.path.dropFirst(label.count + 1)), directory: $0.directory, bytes: $0.bytes, sha256: $0.sha256) }
            let root = archive.appendingPathComponent(label)
            let source = open(root.path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)
            guard source >= 0 else { throw Staging.error("归档内容不可读") }; defer { close(source) }
            guard try Staging.walk(source, prefix: "", target: nil, flag: CancellationFlag(), includeFinderCache: true) == content.sorted(by: { $0.path < $1.path }) else { throw GUIError.invalid("归档内容 hash 不一致，停止还原") }
        }
        return (archive, index)
    }
    public func restore(_ record: JobRecord) throws {
        guard !FileManager.default.fileExists(atPath: PhysicalLease.activeMarker.path) else { throw GUIError.invalid("物理 worker 未确认退出") }
        let (archive, index) = try validatedArchive(record)
        for (label, original) in index.roots.sorted(by: { $0.key < $1.key }) {
            let url = URL(fileURLWithPath: original)
            try Self.secureDirectory(url)
            let content = index.entries.filter { $0.path.hasPrefix(label + "/") }.map { FileEntry(path: String($0.path.dropFirst(label.count + 1)), directory: $0.directory, bytes: $0.bytes, sha256: $0.sha256) }
            let root = archive.appendingPathComponent(label)
            let target = open(url.path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)
            guard target >= 0 else { throw Staging.error("无法打开还原目录") }; defer { close(target) }
            for entry in content.sorted(by: { $0.path < $1.path }) {
                let parts = entry.path.split(separator: "/").map(String.init)
                let parent = try DiscExport.childDirectory(target, parts: Array(parts.dropLast())); defer { close(parent) }
                let name = parts.last!
                var existing = stat()
                if fstatat(parent, name, &existing, AT_SYMLINK_NOFOLLOW) == 0 {
                    if entry.directory { guard existing.st_mode & S_IFMT == S_IFDIR else { throw GUIError.invalid("还原目录冲突") } }
                    else { let fd = openat(parent, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK); guard fd >= 0 else { throw Staging.error("原位置不可读") }; defer { close(fd) }; guard existing.st_mode & S_IFMT == S_IFREG, try DiscExport.hash(fd) == (entry.bytes, entry.sha256!) else { throw GUIError.invalid("原位置已有不同内容，不覆盖") } }
                    continue
                }
                if entry.directory { guard mkdirat(parent, name, 0o700) == 0 else { throw Staging.error("还原目录失败") } }
                else {
                    let input = open(root.appendingPathComponent(entry.path).path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC)
                    guard input >= 0 else { throw Staging.error("归档文件不可读") }; defer { close(input) }
                    let output = openat(parent, name, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0o600)
                    guard output >= 0 else { throw Staging.error("原位置已被占用，不覆盖") }; defer { close(output) }
                    guard fcopyfile(input, output, nil, copyfile_flags_t(COPYFILE_DATA | COPYFILE_DATA_SPARSE)) == 0, try DiscExport.hash(output) == (entry.bytes, entry.sha256!), fsync(output) == 0 else { throw GUIError.invalid("还原文件 hash 不一致") }
                }
            }
            if label == "snapshot" {
                // 恢复后仍固定输入文件；不能因归档恢复而放宽 staging 的只读约束。
                for entry in content.sorted(by: { $0.path > $1.path }) {
                    let parts = entry.path.split(separator: "/").map(String.init)
                    let parent = try DiscExport.childDirectory(target, parts: Array(parts.dropLast())); defer { close(parent) }
                    let fd = openat(parent, parts.last!, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | (entry.directory ? O_DIRECTORY : 0))
                    guard fd >= 0 else { throw Staging.error("无法固定恢复快照") }; defer { close(fd) }
                    guard fchmod(fd, entry.directory ? 0o500 : 0o400) == 0 else { throw Staging.error("无法固定恢复快照") }
                }
            }
        }
    }
}
