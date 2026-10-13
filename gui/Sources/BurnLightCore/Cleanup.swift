import Foundation
import CryptoKit
import Darwin

public enum CleanupMode: String, Sendable {
    case temporary, history
}

public struct CleanupPolicy: Sendable {
    public let mode: CleanupMode
    public let retentionDays: Int
    public init(mode: CleanupMode = .temporary, retentionDays: Int = 0) {
        self.mode = mode; self.retentionDays = retentionDays
    }
}

public struct CleanupItem: Identifiable, Sendable {
    public let id: UUID
    public let created: Date
    public let label: String
    public let allocatedBytes: UInt64
    public let paths: [String]
    fileprivate let fingerprint: String
    fileprivate let backupIDs: Set<UUID>
}

public struct CleanupPlan: Sendable {
    public let items: [CleanupItem]
    public let protectedJobs: Int
    public let warnings: [String]
    public let policy: CleanupPolicy
    fileprivate let scannedAt: Date
    public var allocatedBytes: UInt64 { items.reduce(0) { $0 + $1.allocatedBytes } }
}

public struct CleanupResult: Sendable {
    public let jobs: Int
    public let estimatedFreedBytes: UInt64
    public let errors: [String]
}

// 只操作 GUI 索引所指的私有工作目录，不搜索磁盘上的任意“缓存”或用户导出。
public final class CleanupManager: Sendable {
    private let store: JobStore
    private let temporaryRoot: URL
    private let lockRoot: URL
    private let idle: @Sendable () throws -> Bool
    public init(store: JobStore, temporaryRoot: URL = URL(fileURLWithPath: "/private/tmp"),
                lockRoot: URL = PhysicalLease.activeMarker.deletingLastPathComponent(),
                idle: @escaping @Sendable () throws -> Bool = { try CleanupManager.systemIsIdle() }) {
        self.store = store; self.temporaryRoot = temporaryRoot; self.lockRoot = lockRoot; self.idle = idle
    }
    public static func systemIsIdle() throws -> Bool {
        let process = Process(), pipe = Pipe()
        process.executableURL = URL(fileURLWithPath: "/bin/ps")
        process.arguments = ["-axo", "comm="]; process.standardOutput = pipe
        process.standardError = FileHandle.nullDevice
        try process.run()
        let data = try pipe.fileHandleForReading.readToEnd() ?? Data(); process.waitUntilExit()
        guard process.terminationStatus == 0, let text = String(data: data, encoding: .utf8) else {
            throw GUIError.invalid("无法确认是否有其他任务，请稍后重新扫描")
        }
        let workers: Set<String> = ["appendcd", "appendcd-cdrecord", "appendcd-initial-cdrecord", "appendcd-vat-cdrecord", "burnlight-export", "readcd", "drutil", "osascript"]
        var applications = 0
        for line in text.components(separatedBy: .newlines) {
            let name = URL(fileURLWithPath: line.trimmingCharacters(in: .whitespaces)).lastPathComponent
            if workers.contains(name) { return false }
            if name == "BurnLight" { applications += 1 }
        }
        return applications <= 1
    }
    private func locked<T>(_ action: () throws -> T) throws -> T {
        try JobStore.secureDirectory(lockRoot)
        let fd = open(lockRoot.appendingPathComponent("physical.lock").path, O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0o600)
        guard fd >= 0 else { throw Staging.error("无法核对清理准入") }; defer { close(fd) }
        guard flock(fd, LOCK_EX | LOCK_NB) == 0 else { throw GUIError.invalid("光盘任务正在执行，不能清理") }
        defer { flock(fd, LOCK_UN) }
        // 使用同一把物理锁，但不创建/移除 active 标记，也不解除历史刻录锁定。
        var marker = stat()
        guard lstat(lockRoot.appendingPathComponent("active.json").path, &marker) != 0, errno == ENOENT,
              try idle() else { throw GUIError.invalid("有活动或未知任务，或其他 BurnLight 实例；请结束任务后再清理") }
        return try action()
    }
    private func canonical(_ path: String) throws -> String {
        let mapped = path == "/tmp" || path.hasPrefix("/tmp/") ? "/private" + path : path
        let parts = mapped.split(separator: "/", omittingEmptySubsequences: false).dropFirst()
        guard mapped.hasPrefix("/"), parts.allSatisfy({ !$0.isEmpty && $0 != "." && $0 != ".." }) else {
            throw GUIError.invalid("任务路径含外部或非规范位置：\(path)")
        }
        return mapped
    }
    private func exists(_ path: String) throws -> Bool {
        var s = stat()
        if lstat(path, &s) == 0 { return true }
        guard errno == ENOENT else { throw Staging.error("无法检查任务材料") }; return false
    }
    private func archiveIndex(_ record: JobRecord, wholeWorkspace: Bool = true) throws -> ArchiveIndex {
        let expected = store.directory.deletingLastPathComponent().appendingPathComponent("Archives").appendingPathComponent(record.id.uuidString).path
        guard try record.archivePath.map(canonical) == expected else { throw GUIError.invalid("缺少完整归档") }
        let root = try openDirectory(expected); defer { close(root) }
        let fd = openat(root, "archive.json", O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK)
        guard fd >= 0 else { throw GUIError.invalid("归档索引不可读") }; defer { close(fd) }
        let s = try Staging.info(fd)
        guard s.st_mode & S_IFMT == S_IFREG, s.st_size <= 8 * 1024 * 1024 else { throw GUIError.invalid("归档索引无效") }
        let data = try FileHandle(fileDescriptor: dup(fd), closeOnDealloc: true).readToEnd() ?? Data()
        let index = try JSONDecoder().decode(ArchiveIndex.self, from: data)
        guard index.schema == 1, index.jobID == record.id,
              wholeWorkspace ? index.omitted.isEmpty : !index.omitted.contains(where: { $0 == "snapshot" || $0.hasPrefix("snapshot/") }),
              Set(index.roots.keys).isSubset(of: ["workspace", "snapshot"]),
              index.roots["workspace"] == record.workspacePath else { throw GUIError.invalid("归档缺项或身份不一致") }
        try DiscExport.validate(index.entries, expected: index.entries)
        return index
    }
    private func openDirectory(_ path: String, requireOwnership: Bool = true) throws -> Int32 {
        guard try canonical(URL(fileURLWithPath: path).resolvingSymlinksInPath().path) == canonical(path) else { throw GUIError.invalid("清理路径不能经过符号链接") }
        let fd = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)
        guard fd >= 0 else { throw Staging.error("无法访问待清理目录") }
        let s = try Staging.info(fd)
        guard (!requireOwnership || s.st_uid == getuid()), s.st_mode & S_IFMT == S_IFDIR else { close(fd); throw GUIError.invalid("只能清理当前用户的任务目录") }
        return fd
    }
    private func tree(_ path: String) throws -> (String, UInt64) {
        let fd = try openDirectory(path); defer { close(fd) }
        var hash = SHA256(), bytes: UInt64 = 0, count = 0
        func walk(_ parent: Int32, depth: Int) throws {
            guard depth <= 128 else { throw GUIError.invalid("清理目录层级超限") }
            let before = try Staging.info(parent)
            guard before.st_uid == getuid() else { throw GUIError.invalid("子目录属于其他用户，保留材料") }
            let names = try Staging.names(parent)
            hash.update(data: Data("D:\(before.st_dev):\(before.st_ino):\(before.st_mtimespec):\(before.st_ctimespec)\n".utf8))
            bytes += UInt64(max(0, before.st_blocks)) * 512
            for name in names {
                count += 1; guard count <= 250_000 else { throw GUIError.invalid("清理文件数量超限") }
                var s = stat()
                guard fstatat(parent, name, &s, AT_SYMLINK_NOFOLLOW) == 0 else { throw Staging.error("清理目录发生变化") }
                let type = s.st_mode & S_IFMT
                guard (type == S_IFREG || type == S_IFDIR), s.st_dev == before.st_dev else { throw GUIError.invalid("材料含符号链接、特殊文件或其他文件系统，保留该任务") }
                hash.update(data: Data("\(name.utf8.count):\(name):\(s.st_dev):\(s.st_ino):\(s.st_mode):\(s.st_nlink):\(s.st_size):\(s.st_blocks):\(s.st_mtimespec):\(s.st_ctimespec)\n".utf8))
                if type == S_IFDIR {
                    let child = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)
                    guard child >= 0 else { throw Staging.error("子目录不可访问") }; defer { close(child) }
                    guard Staging.same(s, try Staging.info(child)) else { throw GUIError.invalid("目录已被替换") }
                    try walk(child, depth: depth + 1)
                } else if s.st_nlink == 1 { bytes += UInt64(max(0, s.st_blocks)) * 512 }
            }
            guard names == (try Staging.names(parent)), Staging.same(before, try Staging.info(parent)) else { throw GUIError.invalid("扫描期间材料发生变化") }
        }
        try walk(fd, depth: 0)
        return (hash.finalize().map { String(format: "%02x", $0) }.joined(), bytes)
    }
    public func scan(policy: CleanupPolicy = CleanupPolicy(), protectedIDs: Set<UUID> = [], protectedPaths: [String] = []) throws -> CleanupPlan {
        try locked { try plan(policy: policy, now: Date(), protectedIDs: protectedIDs, protectedPaths: protectedPaths) }
    }
    private func plan(policy: CleanupPolicy, now: Date, protectedIDs: Set<UUID>, protectedPaths: [String]) throws -> CleanupPlan {
        guard [0, 7, 30].contains(policy.retentionDays) else { throw GUIError.invalid("清理保留期不支持") }
        let loaded = try store.load()
        guard loaded.warnings.isEmpty else { throw GUIError.invalid("有无法读取的任务索引，先保存诊断；清理不会删除或绕过坏索引") }
        let cutoff = now.addingTimeInterval(-Double(policy.retentionDays) * 86400)
        func eligible(_ r: JobRecord) -> Bool {
            !protectedIDs.contains(r.id) && r.created < cutoff && [.idle, .completed, .failed].contains(r.phase) &&
                r.requiresInspection != true && r.materialsRemovedAt == nil && !(r.operation?.writes == true && r.phase != .completed) && r.archiveWarning == nil
        }
        func references(_ r: JobRecord) -> [String] {
            [r.workspacePath, r.archivePath, r.snapshotPath, r.imagePath, r.evidencePath].compactMap { $0 }
        }
        let protected = try (protectedPaths + loaded.records.filter { !eligible($0) }.flatMap(references)).map(canonical)
        func blocked(_ path: String) -> Bool { protected.contains { $0 == path || $0.hasPrefix(path + "/") } }
        var items: [CleanupItem] = [], warnings: [String] = [], assigned: Set<String> = [], pathProtected: Set<UUID> = []
        for r in loaded.records where eligible(r) {
            do {
                var paths: [String] = [], backups: Set<UUID> = []
                let workspace = temporaryRoot.appendingPathComponent("burnlight-job-\(r.id.uuidString)").path
                guard try r.workspacePath.map(canonical) == workspace else { throw GUIError.invalid("工作目录不属于该任务") }
                let protectedArchive = try r.archivePath.map { blocked(try canonical($0)) } ?? false
                if blocked(workspace) || protectedArchive {
                    pathProtected.insert(r.id); continue
                }
                if policy.mode == .temporary { _ = try archiveIndex(r); backups.insert(r.id) }
                if try exists(workspace), !blocked(workspace) { paths.append(workspace) }
                if policy.mode == .history, let archive = r.archivePath {
                    let expected = store.directory.deletingLastPathComponent().appendingPathComponent("Archives").appendingPathComponent(r.id.uuidString).path
                    guard try canonical(archive) == expected else { throw GUIError.invalid("归档位置不属于该任务") }
                    if try exists(expected), !blocked(expected) { paths.append(expected) }
                }
                if let snapshot = r.snapshotPath {
                    let content = try canonical(snapshot), root = URL(fileURLWithPath: content).deletingLastPathComponent().path
                    guard URL(fileURLWithPath: root).deletingLastPathComponent() == temporaryRoot,
                          TextContract.matches(URL(fileURLWithPath: root).lastPathComponent, "^burnlight-input-[A-Za-z0-9]{6}$"),
                          URL(fileURLWithPath: content).lastPathComponent == "files" else { throw GUIError.invalid("快照位置不是 GUI 私有目录") }
                    if try exists(root), !blocked(root), !assigned.contains(root) {
                        let users = try loaded.records.filter { try $0.snapshotPath.map(canonical) == content }
                        // 共享快照只有在全部引用任务都有归档副本时才清理。
                        if users.allSatisfy(eligible) {
                            do {
                                var sharedBackups: Set<UUID> = []
                                for user in users {
                                    let index = try archiveIndex(user, wholeWorkspace: false)
                                    guard try index.roots["snapshot"].map(canonical) == root else { throw GUIError.invalid("共享快照缺少归档副本") }
                                    sharedBackups.insert(user.id)
                                }
                                paths.append(root); backups.formUnion(sharedBackups)
                            } catch {
                                warnings.append("\(r.id.uuidString.prefix(8))：共享快照保留（\(error.localizedDescription)），其他完整副本可清理")
                            }
                        }
                    }
                }
                paths = paths.filter { !assigned.contains($0) }
                guard !paths.isEmpty else { continue }
                var digest = SHA256(), bytes: UInt64 = 0
                let indexPath = store.directory.appendingPathComponent(r.id.uuidString + ".json")
                digest.update(data: Data(try Configuration.digest(indexPath).utf8))
                for path in paths { let (fingerprint, amount) = try tree(path); digest.update(data: Data((path + fingerprint).utf8)); bytes += amount }
                let fingerprint = digest.finalize().map { String(format: "%02x", $0) }.joined()
                items.append(CleanupItem(id: r.id, created: r.created, label: "\(r.operation?.rawValue ?? "快照") · \(r.phase.label)", allocatedBytes: bytes, paths: paths, fingerprint: fingerprint, backupIDs: backups))
                assigned.formUnion(paths)
            } catch { warnings.append("\(r.id.uuidString.prefix(8))：\(error.localizedDescription)") }
        }
        return CleanupPlan(items: items.sorted { $0.allocatedBytes > $1.allocatedBytes }, protectedJobs: loaded.records.filter { !eligible($0) }.count + pathProtected.count,
                           warnings: warnings, policy: policy, scannedAt: now)
    }
    private func removeTree(_ path: String) throws {
        let url = URL(fileURLWithPath: path), parent = try openDirectory(url.deletingLastPathComponent().path, requireOwnership: false)
        defer { close(parent) }
        func remove(_ parent: Int32, _ name: String, depth: Int) throws {
            guard depth <= 128 else { throw GUIError.invalid("删除层级超限") }
            var before = stat()
            guard fstatat(parent, name, &before, AT_SYMLINK_NOFOLLOW) == 0 else { throw Staging.error("材料在清理期间变化") }
            let type = before.st_mode & S_IFMT
            guard type == S_IFREG || type == S_IFDIR else { throw GUIError.invalid("删除遇到符号链接或特殊文件，停止清理") }
            if type == S_IFDIR {
                let child = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)
                guard child >= 0 else { throw Staging.error("删除目录无法访问") }; defer { close(child) }
                let opened = try Staging.info(child)
                guard Staging.same(before, opened), opened.st_uid == getuid(), opened.st_dev == (try Staging.info(parent)).st_dev else { throw GUIError.invalid("目录身份或文件系统发生变化") }
                // 仅解除待删除私有副本的只读目录权限，不触碰源目录或导出。
                guard fchmod(child, opened.st_mode & 0o777 | 0o700) == 0 else { throw Staging.error("无法删除只读副本") }
                for entry in try Staging.names(child) { try remove(child, entry, depth: depth + 1) }
                var current = stat()
                guard fstatat(parent, name, &current, AT_SYMLINK_NOFOLLOW) == 0, current.st_ino == opened.st_ino, current.st_dev == opened.st_dev else { throw GUIError.invalid("待删除目录被替换") }
            }
            guard unlinkat(parent, name, type == S_IFDIR ? AT_REMOVEDIR : 0) == 0 else { throw Staging.error("无法删除任务副本") }
        }
        try remove(parent, url.lastPathComponent, depth: 0)
    }
    public func clean(_ preview: CleanupPlan, selectedIDs: Set<UUID>, protectedIDs: Set<UUID> = [], protectedPaths: [String] = []) throws -> CleanupResult {
        try locked {
            guard !selectedIDs.isEmpty, selectedIDs.isSubset(of: Set(preview.items.map { $0.id })) else { throw GUIError.invalid("请选择已扫描的清理项目") }
            let fresh = try plan(policy: preview.policy, now: preview.scannedAt, protectedIDs: protectedIDs, protectedPaths: protectedPaths)
            let chosen = preview.items.filter { selectedIDs.contains($0.id) }
            for item in chosen {
                guard let current = fresh.items.first(where: { $0.id == item.id }), current.fingerprint == item.fingerprint,
                      current.paths == item.paths, current.backupIDs == item.backupIDs else { throw GUIError.invalid("任务材料或引用已变化，请重新扫描；尚未清理") }
            }
            let records = try store.records()
            var backups = chosen.reduce(into: Set<UUID>()) { $0.formUnion($1.backupIDs) }
            if preview.policy.mode == .history { backups.subtract(selectedIDs) }
            // 删除临时副本之前，完整复读并验证全部归档 hash；坏归档不能换来空间。
            for id in backups {
                guard let record = records.first(where: { $0.id == id }) else { throw GUIError.invalid("归档任务索引缺失") }
                _ = try store.validatedArchive(record)
            }
            var jobs = 0, bytes: UInt64 = 0, errors: [String] = []
            for item in chosen {
                do {
                    for path in item.paths { try removeTree(path) }
                    if preview.policy.mode == .history, var record = records.first(where: { $0.id == item.id }) {
                        record.materialsRemovedAt = Date(); record.archivePath = nil
                        record.workspacePath = nil; record.snapshotPath = nil; record.imagePath = nil; record.evidencePath = nil
                        try store.save(record)
                    }
                    jobs += 1; bytes += item.allocatedBytes
                } catch { errors.append("\(item.id.uuidString.prefix(8))：\(error.localizedDescription)。已删除的部分无法恢复，剩余材料保留，请重新扫描。"); break }
            }
            return CleanupResult(jobs: jobs, estimatedFreedBytes: bytes, errors: errors)
        }
    }
}
