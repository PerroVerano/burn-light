import Foundation
import CryptoKit
import Darwin

public struct InputSnapshot: Codable, Sendable {
    public let directory: URL
    public let entries: [FileEntry]
    public var fileCount: Int { entries.filter { !$0.directory }.count }
    public var totalBytes: UInt64 { entries.reduce(0) { $0 + $1.bytes } }
}

public final class CancellationFlag: @unchecked Sendable {
    private let lock = NSLock()
    private var cancelled = false
    public init() {}
    public func cancel() { lock.lock(); cancelled = true; lock.unlock() }
    public func check() throws {
        lock.lock(); let value = cancelled; lock.unlock()
        if value { throw GUIError.invalid("输入快照已取消；未提交物理任务") }
    }
}

public enum Staging {
    static func error(_ message: String) -> GUIError { .invalid("\(message)：\(String(cString: strerror(errno)))") }
    static func info(_ fd: Int32) throws -> stat {
        var s = stat(); guard fstat(fd, &s) == 0 else { throw error("无法读取文件状态") }; return s
    }
    static func same(_ a: stat, _ b: stat) -> Bool {
        a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_size == b.st_size &&
            a.st_mtimespec.tv_sec == b.st_mtimespec.tv_sec && a.st_mtimespec.tv_nsec == b.st_mtimespec.tv_nsec &&
            a.st_ctimespec.tv_sec == b.st_ctimespec.tv_sec && a.st_ctimespec.tv_nsec == b.st_ctimespec.tv_nsec
    }
    static func names(_ fd: Int32) throws -> [String] {
        let copied = dup(fd)
        guard copied >= 0, let dir = fdopendir(copied) else { if copied >= 0 { close(copied) }; throw error("无法枚举目录") }
        defer { closedir(dir) }
        rewinddir(dir); var result: [String] = []; errno = 0
        while let entry = readdir(dir) {
            let name: String = withUnsafePointer(to: &entry.pointee.d_name) { ptr in
                ptr.withMemoryRebound(to: CChar.self, capacity: Int(NAME_MAX) + 1) { String(cString: $0) }
            }
            if name != "." && name != ".." { result.append(name) }
            errno = 0
        }
        guard errno == 0 else { throw error("目录枚举失败") }
        return result.sorted()
    }
    static func walk(_ fd: Int32, prefix: String, target: URL?, flag: CancellationFlag, includeFinderCache: Bool = false) throws -> [FileEntry] {
        let before = try info(fd); let children = try names(fd); var entries: [FileEntry] = []
        for name in children {
            try flag.check()
            guard !name.unicodeScalars.contains(where: { CharacterSet.controlCharacters.contains($0) }) else { throw GUIError.invalid("文件名含控制字符：\(prefix)") }
            var s = stat()
            guard fstatat(fd, name, &s, AT_SYMLINK_NOFOLLOW) == 0 else { throw error("输入在复制期间变化") }
            let type = s.st_mode & S_IFMT
            guard type == S_IFREG || type == S_IFDIR else { throw GUIError.invalid("拒绝 symlink 或特殊文件：\(prefix + name)") }
            if !includeFinderCache && name == ".DS_Store" && type == S_IFREG { continue }
            let path = prefix + name
            let child = openat(fd, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK | (type == S_IFDIR ? O_DIRECTORY : 0))
            guard child >= 0 else { throw error("无法打开输入：\(path)") }
            defer { close(child) }
            let opened = try info(child)
            guard same(s, opened), opened.st_mode & S_IFMT == type else { throw GUIError.invalid("输入被替换：\(path)") }
            let destination = target?.appendingPathComponent(name)
            if type == S_IFDIR {
                if let destination { try FileManager.default.createDirectory(at: destination, withIntermediateDirectories: false, attributes: [.posixPermissions: 0o700]) }
                entries.append(FileEntry(path: path, directory: true, bytes: 0))
                entries += try walk(child, prefix: path + "/", target: destination, flag: flag, includeFinderCache: includeFinderCache)
                if let destination { try FileManager.default.setAttributes([.posixPermissions: 0o500], ofItemAtPath: destination.path) }
            } else {
                let output: Int32
                if let destination {
                    output = open(destination.path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0o600)
                    guard output >= 0 else { throw error("无法建立快照文件") }
                } else { output = -1 }
                defer { if output >= 0 { close(output) } }
                var hash = SHA256(), count: UInt64 = 0, buffer = [UInt8](repeating: 0, count: 1024 * 1024)
                while true {
                    try flag.check()
                    let n = read(child, &buffer, buffer.count)
                    if n < 0 && errno == EINTR { continue }
                    guard n >= 0 else { throw error("读取输入失败：\(path)") }
                    if n == 0 { break }
                    count += UInt64(n); hash.update(data: Data(buffer.prefix(n)))
                    if output >= 0 {
                        try buffer.withUnsafeBytes { ptr in
                            var offset = 0
                            while offset < n {
                                let written = write(output, ptr.baseAddress!.advanced(by: offset), n - offset)
                                if written < 0 && errno == EINTR { continue }
                                guard written > 0 else { throw error("写入快照失败") }; offset += written
                            }
                        }
                    }
                }
                guard opened.st_size >= 0, count == UInt64(opened.st_size), same(opened, try info(child)) else { throw GUIError.invalid("源文件在复制期间变化：\(path)") }
                if output >= 0 { guard fsync(output) == 0, fchmod(output, 0o400) == 0 else { throw error("无法固定快照") } }
                let digest = hash.finalize().map { String(format: "%02x", $0) }.joined()
                entries.append(FileEntry(path: path, directory: false, bytes: count, sha256: digest))
            }
        }
        guard children == (try names(fd)), same(before, try info(fd)) else { throw GUIError.invalid("源目录在复制期间变化：\(prefix)") }
        return entries.sorted { $0.path < $1.path }
    }
    public static func create(source: URL, flag: CancellationFlag = CancellationFlag()) throws -> InputSnapshot {
        let fd = open(source.path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)
        guard fd >= 0 else { throw error("无法以用户权限打开目录") }; defer { close(fd) }
        var template = Array("/private/tmp/burnlight-input-XXXXXX".utf8CString)
        guard let pointer = mkdtemp(&template) else { throw error("无法建立私有快照") }
        let root = URL(fileURLWithPath: String(cString: pointer), isDirectory: true)
        let content = root.appendingPathComponent("files", isDirectory: true)
        try FileManager.default.createDirectory(at: content, withIntermediateDirectories: false, attributes: [.posixPermissions: 0o700])
        let entries = try walk(fd, prefix: "", target: content, flag: flag)
        // 第二遍复读源目录，识别复制中已处理文件的后续变化与替换。
        guard entries == (try walk(fd, prefix: "", target: nil, flag: flag)), entries.contains(where: { !$0.directory }) else {
            throw GUIError.invalid("输入已变化或没有可刻录文件；快照保留在 \(root.path)")
        }
        try FileManager.default.setAttributes([.posixPermissions: 0o500], ofItemAtPath: content.path)
        let snapshot = InputSnapshot(directory: content, entries: entries)
        try JSONEncoder().encode(snapshot).write(to: root.appendingPathComponent("snapshot.json"), options: .atomic)
        return snapshot
    }
    public static func validate(_ snapshot: InputSnapshot) throws {
        let fd = open(snapshot.directory.path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)
        guard fd >= 0 else { throw error("快照不再可读") }; defer { close(fd) }
        guard snapshot.entries == (try walk(fd, prefix: "", target: nil, flag: CancellationFlag())) else { throw GUIError.invalid("固定输入快照被修改；请重新选择目录") }
    }
    public static func readbackMaterials(image: URL, directory: URL) throws -> URL {
        try JobStore.secureDirectory(directory)
        let parent = open(image.deletingLastPathComponent().path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)
        guard parent >= 0 else { throw error("无法按用户权限读取原镜像目录") }; defer { close(parent) }
        var inputs: [(Int32, stat, String)] = []
        defer { for (fd, _, _) in inputs { close(fd) } }
        for name in [image.lastPathComponent, image.lastPathComponent + ".manifest"] {
            let fd = openat(parent, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)
            guard fd >= 0 else { throw error("无法读取原镜像或 manifest") }
            let s = try info(fd)
            guard s.st_mode & S_IFMT == S_IFREG else { close(fd); throw GUIError.invalid("原镜像与 manifest 必须是普通文件") }
            inputs.append((fd, s, name))
        }
        for (fd, before, name) in inputs {
            let output = open(directory.appendingPathComponent(name).path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0o600)
            guard output >= 0 else { throw error("无法建立只读补验快照") }; defer { close(output) }
            var hash = SHA256(), bytes: Int64 = 0, buffer = [UInt8](repeating: 0, count: 1024 * 1024)
            while true {
                let n = read(fd, &buffer, buffer.count)
                if n < 0 && errno == EINTR { continue }
                guard n >= 0 else { throw error("原镜像读取失败") }; if n == 0 { break }
                bytes += Int64(n); hash.update(data: Data(buffer.prefix(n)))
                try buffer.withUnsafeBytes { ptr in
                    var offset = 0
                    while offset < n {
                        let written = write(output, ptr.baseAddress!.advanced(by: offset), n - offset)
                        if written < 0 && errno == EINTR { continue }
                        guard written > 0 else { throw error("补验快照写入失败") }; offset += written
                    }
                }
            }
            guard bytes == before.st_size, same(before, try info(fd)), fsync(output) == 0, fchmod(output, 0o400) == 0 else {
                throw GUIError.invalid("只读补验材料在复制中变化")
            }
            guard lseek(fd, 0, SEEK_SET) == 0 else { throw error("无法复核补验材料") }
            var second = SHA256()
            while true {
                let n = read(fd, &buffer, buffer.count)
                if n < 0 && errno == EINTR { continue }
                guard n >= 0 else { throw error("补验材料复读失败") }; if n == 0 { break }
                second.update(data: Data(buffer.prefix(n)))
            }
            guard hash.finalize() == second.finalize(), same(before, try info(fd)) else { throw GUIError.invalid("原镜像或 manifest 已变化") }
        }
        for (fd, before, _) in inputs { guard same(before, try info(fd)) else { throw GUIError.invalid("补验材料在复制后变化") } }
        // readback 从实际盘取旧数据，不读取 manifest.sourcePath；保留原清单原文及其依赖位置。
        return directory.appendingPathComponent(image.lastPathComponent)
    }
}
