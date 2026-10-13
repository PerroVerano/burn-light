import Foundation
import CryptoKit
import Darwin

public struct ExportReceipt: Codable, Sendable {
    public let schema: Int
    public let sourceKind: String
    public let filesystem: String
    public let udfCompatibility: String?
    public let device: String
    public let registryID: String
    public let media: String
    public let entries: [FileEntry]
}

public enum DiscExport {
    static func components(_ path: String) throws -> [String] {
        let parts = path.split(separator: "/", omittingEmptySubsequences: false).map(String.init)
        guard !parts.isEmpty, parts.count <= 128, path.utf8.count <= 4096,
              parts.allSatisfy({ !$0.isEmpty && $0 != "." && $0 != ".." && !$0.contains("\\") &&
                  !$0.unicodeScalars.contains(where: { CharacterSet.controlCharacters.contains($0) }) }) else {
            throw GUIError.invalid("不安全的光盘相对路径：\(path)")
        }
        return parts
    }
    public static func selection(_ listed: [FileEntry], roots: Set<String>) throws -> [FileEntry] {
        guard !listed.isEmpty, listed.count <= 100_000, Set(listed.map(\.path)).count == listed.count,
              roots.isSubset(of: Set(listed.map(\.path))) else { throw GUIError.invalid("光盘选择为空、重复或已变化，请重新检查") }
        for entry in listed { _ = try components(entry.path) }
        let byPath = Dictionary(uniqueKeysWithValues: listed.map { ($0.path, $0) })
        var selected = Set(listed.filter { entry in roots.isEmpty || roots.contains(entry.path) || roots.contains(where: { byPath[$0]?.directory == true && entry.path.hasPrefix($0 + "/") }) }.map(\.path))
        for path in selected {
            let parts = try components(path)
            for count in 1..<parts.count {
                let parent = parts.prefix(count).joined(separator: "/")
                guard byPath[parent]?.directory == true else { throw GUIError.invalid("目录缺少父级：\(path)") }
                selected.insert(parent)
            }
        }
        return listed.filter { selected.contains($0.path) }.sorted { $0.path < $1.path }
    }
    public static func writeSelection(_ entries: [FileEntry], all: Bool, to url: URL) throws {
        let lines = try entries.map { entry -> String in
            _ = try components(entry.path)
            let quoted = entry.path.replacingOccurrences(of: "\\", with: "\\\\").replacingOccurrences(of: "\"", with: "\\\"")
            return "\"\(quoted)\" \(entry.directory ? "D" : "F") \(entry.bytes)"
        }
        try Data((["BURNLIGHT_EXPORT_SELECTION 1", all ? "ALL" : "SELECTED"] + lines).joined(separator: "\n").appending("\n").utf8).write(to: url, options: .withoutOverwriting)
        try FileManager.default.setAttributes([.posixPermissions: 0o600], ofItemAtPath: url.path)
    }
    public static func receipt(_ result: CLIResult, evidence: URL, selected: Drive, expected: [FileEntry]) throws -> ExportReceipt {
        guard result.succeeded, result.stdout.components(separatedBy: .newlines).filter({ $0 == "OPTICAL EXPORT VERIFIED" }).count == 1,
              try String(contentsOf: evidence.appendingPathComponent("result.txt"), encoding: .utf8) == "OPTICAL_EXPORT_VERIFIED\n" else {
            throw GUIError.invalid("光盘导出未返回完整成功证据；保留日志与部分缓存，请重新检查介质")
        }
        let receipt = try JSONDecoder().decode(ExportReceipt.self, from: Data(contentsOf: evidence.appendingPathComponent("export.json")))
        guard receipt.schema == 1, receipt.sourceKind == "optical", ["iso9660+joliet", "udf1.02", "udf2.01+vat"].contains(receipt.filesystem),
              receipt.device == selected.device, receipt.registryID == selected.registryID, receipt.media == selected.media else {
            throw GUIError.invalid("光盘导出版本或目标身份不一致")
        }
        guard receipt.udfCompatibility == nil || receipt.udfCompatibility == "" ||
              (receipt.filesystem == "udf2.01+vat" && receipt.udfCompatibility == "windows-fid-location-v1") else {
            throw GUIError.invalid("光盘导出包含未知 UDF 兼容分支")
        }
        try validate(receipt.entries, expected: expected)
        return receipt
    }
    static func validate(_ entries: [FileEntry], expected: [FileEntry]) throws {
        guard !entries.isEmpty, entries.count <= 100_000, Set(entries.map(\.path)).count == entries.count,
              entries.count == expected.count else { throw GUIError.invalid("导出清单为空、重复或数量不一致") }
        let sorted = entries.sorted { $0.path < $1.path }, wanted = expected.sorted { $0.path < $1.path }
        for (entry, original) in zip(sorted, wanted) {
            _ = try components(entry.path)
            guard entry.path == original.path, entry.directory == original.directory, entry.bytes == original.bytes,
                  entry.directory ? (entry.bytes == 0 && entry.sha256 == nil) : TextContract.matches(entry.sha256 ?? "", "^[a-f0-9]{64}$") else {
                throw GUIError.invalid("导出路径、类型、大小或 hash 不一致：\(entry.path)")
            }
        }
        _ = try selection(entries, roots: [])
    }
    static func childDirectory(_ base: Int32, parts: [String]) throws -> Int32 {
        var fd = dup(base)
        guard fd >= 0 else { throw Staging.error("无法打开导出目录") }
        for part in parts {
            let next = openat(fd, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)
            close(fd)
            guard next >= 0 else { throw Staging.error("导出目录被替换或不可读") }; fd = next
        }
        return fd
    }
    static func hash(_ fd: Int32) throws -> (UInt64, String) {
        guard lseek(fd, 0, SEEK_SET) == 0 else { throw Staging.error("无法复核导出文件") }
        var sha = SHA256(), size: UInt64 = 0, buffer = [UInt8](repeating: 0, count: 1024 * 1024)
        while true {
            let n = read(fd, &buffer, buffer.count)
            if n < 0 && errno == EINTR { continue }
            guard n >= 0 else { throw Staging.error("导出复读失败") }; if n == 0 { break }
            size += UInt64(n); sha.update(data: Data(buffer.prefix(n)))
        }
        return (size, sha.finalize().map { String(format: "%02x", $0) }.joined())
    }
    // root 只产生 /private/tmp 缓存。Desktop/Documents 等目标只由当前用户打开并写入。
    public static func copyToLocal(receipt: ExportReceipt, evidence: URL, destination: URL, jobID: UUID) throws -> URL {
        try validate(receipt.entries, expected: receipt.entries)
        let source = open(evidence.appendingPathComponent("files").path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)
        guard source >= 0 else { throw Staging.error("光盘导出缓存不可读") }; defer { close(source) }
        guard receipt.entries.sorted(by: { $0.path < $1.path }) == (try Staging.walk(source, prefix: "", target: nil, flag: CancellationFlag(), includeFinderCache: true)) else { throw GUIError.invalid("导出缓存已变化或含未列出的文件") }
        let parent = open(destination.path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)
        guard parent >= 0 else { throw Staging.error("无法以当前用户权限打开本地目标") }; defer { close(parent) }
        var free = statfs(); guard fstatfs(parent, &free) == 0 else { throw Staging.error("无法读取本地可用空间") }
        var total: UInt64 = 0
        for entry in receipt.entries { let sum = total.addingReportingOverflow(entry.bytes); guard !sum.overflow else { throw GUIError.invalid("导出大小溢出") }; total = sum.partialValue }
        guard free.f_bsize > 0, total / UInt64(free.f_bsize) + (total % UInt64(free.f_bsize) == 0 ? 0 : 1) <= free.f_bavail else { throw GUIError.invalid("本地目标空间不足") }
        let name = "BurnLight-export-\(jobID.uuidString)", partial = ".\(name).partial"
        guard mkdirat(parent, partial, 0o700) == 0 else { throw Staging.error("导出临时目录已存在；不会覆盖") }
        // 失败目录保留 .partial 名称与任务日志；只有所有文件复读 PASS 才提交最终目录。
        let target = openat(parent, partial, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)
        guard target >= 0 else { throw Staging.error("无法打开导出临时目录") }; defer { close(target) }
        for entry in receipt.entries.sorted(by: { $0.path < $1.path }) {
            let parts = try components(entry.path), name = parts.last!
            let from = try childDirectory(source, parts: Array(parts.dropLast())); defer { close(from) }
            let to = try childDirectory(target, parts: Array(parts.dropLast())); defer { close(to) }
            if entry.directory { guard mkdirat(to, name, 0o700) == 0 else { throw Staging.error("导出目录冲突") }; continue }
            let input = openat(from, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)
            guard input >= 0 else { throw Staging.error("无法读取缓存文件") }; defer { close(input) }
            let before = try Staging.info(input)
            guard before.st_mode & S_IFMT == S_IFREG else { throw GUIError.invalid("导出缓存不是普通文件") }
            let output = openat(to, name, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0o600)
            guard output >= 0 else { throw Staging.error("无法建立本地文件；不会覆盖") }; defer { close(output) }
            var buffer = [UInt8](repeating: 0, count: 1024 * 1024)
            while true {
                let n = read(input, &buffer, buffer.count)
                if n < 0 && errno == EINTR { continue }; guard n >= 0 else { throw Staging.error("缓存读取失败") }; if n == 0 { break }
                try buffer.withUnsafeBytes { ptr in
                    var offset = 0
                    while offset < n { let v = write(output, ptr.baseAddress!.advanced(by: offset), n - offset); if v < 0 && errno == EINTR { continue }; guard v > 0 else { throw Staging.error("本地写入失败，保留 partial 目录") }; offset += v }
                }
            }
            let (bytes, sha) = try hash(output)
            guard fsync(output) == 0, bytes == entry.bytes, sha == entry.sha256, Staging.same(before, try Staging.info(input)) else { throw GUIError.invalid("本地导出 hash 或大小不一致：\(entry.path)") }
        }
        guard receipt.entries.sorted(by: { $0.path < $1.path }) == (try Staging.walk(target, prefix: "", target: nil, flag: CancellationFlag(), includeFinderCache: true)) else { throw GUIError.invalid("本地导出完整清单复核失败") }
        guard renameatx_np(parent, partial, parent, name, UInt32(RENAME_EXCL)) == 0 else { throw Staging.error("最终导出目录已存在或无法提交；不会覆盖") }
        return destination.appendingPathComponent(name, isDirectory: true)
    }
}
