import Foundation
import CryptoKit
import Darwin

public struct Configuration: Codable, Sendable {
    public let cli: String
    public let trustedFiles: [String: String]
    public let recordDirectory: String
    public let allowPhysicalWrites: Bool
    public let exportCLI: String?
    public init(cli: String, trustedFiles: [String: String], recordDirectory: String, allowPhysicalWrites: Bool = false, exportCLI: String? = nil) {
        self.cli = cli; self.trustedFiles = trustedFiles; self.recordDirectory = recordDirectory
        self.allowPhysicalWrites = allowPhysicalWrites
        self.exportCLI = exportCLI
    }
    public static func bundled(contents: URL, storage: URL? = nil) throws -> Configuration {
        let contents = contents.resolvingSymlinksInPath()
        let raw = try JSONDecoder().decode(Configuration.self, from: Data(contentsOf: contents.appendingPathComponent("Resources/LocalConfiguration.json")))
        guard raw.recordDirectory == "ApplicationSupport/Records", raw.cli == "MacOS/appendcd", raw.exportCLI == "MacOS/burnlight-export" else {
            throw GUIError.invalid("应用配置版本不支持，请完整重建 .app")
        }
        func resolve(_ path: String) throws -> String {
            let components = path.split(separator: "/", omittingEmptySubsequences: false)
            guard components.count == 2, ["MacOS", "Frameworks"].contains(String(components[0])),
                  TextContract.matches(String(components[1]), "^[A-Za-z0-9][A-Za-z0-9_.-]*$"), components[1] != ".", components[1] != ".." else {
                throw GUIError.invalid("应用依赖位置不合法")
            }
            let url = contents.appendingPathComponent(path)
            guard url.standardizedFileURL.path == url.resolvingSymlinksInPath().path else { throw GUIError.invalid("应用依赖不能是符号链接") }
            return url.path
        }
        var trusted: [String: String] = [:]
        for (path, hash) in raw.trustedFiles { trusted[try resolve(path)] = hash }
        let data = storage ?? FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Application Support/BurnLight", isDirectory: true)
        return try Configuration(cli: resolve(raw.cli), trustedFiles: trusted, recordDirectory: data.appendingPathComponent("Records").path,
                                 allowPhysicalWrites: raw.allowPhysicalWrites, exportCLI: resolve(raw.exportCLI!))
    }
    public static func digest(_ url: URL) throws -> String {
        let fd = open(url.resolvingSymlinksInPath().path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW)
        guard fd >= 0 else { throw GUIError.invalid("无法读取受信任文件：\(url.path)") }; defer { close(fd) }
        var hash = SHA256(), buffer = [UInt8](repeating: 0, count: 1024 * 1024)
        var s = stat(); guard fstat(fd, &s) == 0, s.st_mode & S_IFMT == S_IFREG else { throw GUIError.invalid("受信任文件不是普通文件") }
        while true {
            let n = read(fd, &buffer, buffer.count)
            if n < 0 && errno == EINTR { continue }
            guard n >= 0 else { throw GUIError.invalid("受信任文件读取失败") }
            if n == 0 { break }; hash.update(data: Data(buffer.prefix(n)))
        }
        return hash.finalize().map { String(format: "%02x", $0) }.joined()
    }
    public func validate() throws {
        guard trustedFiles[cli] != nil else { throw GUIError.invalid("CLI 不在本机构建信任清单") }
        for (path, expected) in trustedFiles {
            guard try Self.digest(URL(fileURLWithPath: path)) == expected else { throw GUIError.invalid("本机依赖已变化，停止执行：\(path)") }
        }
    }
}

public enum ShellCodec {
    public static func quote(_ value: String) -> String { "'" + value.replacingOccurrences(of: "'", with: "'\\''") + "'" }
    public static func appleString(_ value: String) -> String {
        "\"" + value.replacingOccurrences(of: "\\", with: "\\\\").replacingOccurrences(of: "\"", with: "\\\"")
            .replacingOccurrences(of: "\r", with: "\\r").replacingOccurrences(of: "\n", with: "\\n") + "\""
    }
    public static func command(executable: String, arguments: [String], directory: URL) -> String {
        let stdout = quote(directory.appendingPathComponent("stdout.log").path)
        let stderr = quote(directory.appendingPathComponent("stderr.log").path)
        let temporary = quote(directory.appendingPathComponent("exit.tmp").path)
        let receipt = quote(directory.appendingPathComponent("exit.code").path)
        let invocation = ([executable] + arguments).map(quote).joined(separator: " ")
        // 环境与参数均受限，退出收据独立于 osascript 的退出码。没有 kill/timeout。
        return "umask 077; /usr/bin/env -i PATH=/usr/bin:/bin:/usr/sbin:/sbin LC_ALL=C \(invocation) >\(stdout) 2>\(stderr); burnlight_status=$?; /usr/bin/printf '%s\\n' \"$burnlight_status\" >\(temporary) && /bin/mv \(temporary) \(receipt)"
    }
}

// flock 防止多个 GUI 实例并发；active 标记跨崩溃保留，未知 worker 不释放准入。
public final class PhysicalLease {
    public let root: URL
    private let fd: Int32
    private let marker: URL
    private var completed = false
    public init(jobDirectory: URL, root: URL? = nil) throws {
        self.root = root ?? URL(fileURLWithPath: "/private/tmp/burnlight-physical-\(getuid())", isDirectory: true)
        try JobStore.secureDirectory(self.root)
        marker = self.root.appendingPathComponent("active.json")
        fd = open(self.root.appendingPathComponent("physical.lock").path, O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0o600)
        guard fd >= 0 else { throw GUIError.invalid("无法建立全局物理任务锁") }
        guard flock(fd, LOCK_EX | LOCK_NB) == 0 else { close(fd); throw GUIError.invalid("另一个 GUI 物理任务仍在运行") }
        if FileManager.default.fileExists(atPath: marker.path) {
            flock(fd, LOCK_UN); close(fd)
            throw GUIError.invalid("上一次物理任务尚无确定退出收据。已阻止新操作；请保留 active.json 与日志，人工确认 worker 和系统服务已恢复。")
        }
        do {
            let data = try JSONSerialization.data(withJSONObject: ["jobDirectory": jobDirectory.path, "started": ISO8601DateFormatter().string(from: Date())], options: [.prettyPrinted, .sortedKeys])
            try data.write(to: marker, options: .withoutOverwriting)
        } catch { flock(fd, LOCK_UN); close(fd); throw error }
    }
    public func finish() throws { try FileManager.default.removeItem(at: marker); completed = true }
    deinit { flock(fd, LOCK_UN); close(fd) }
    public static var activeMarker: URL { URL(fileURLWithPath: "/private/tmp/burnlight-physical-\(getuid())/active.json") }
}

public protocol CLIRunning {
    func run(_ request: CLIRequest, directory: URL) async throws -> CLIResult
}

public final class CLIRunner: CLIRunning, Sendable {
    private let configuration: Configuration
    public init(configuration: Configuration) { self.configuration = configuration }
    static func read(_ path: URL) throws -> String {
        let attrs = try FileManager.default.attributesOfItem(atPath: path.path)
        guard (attrs[.size] as? NSNumber)?.intValue ?? 0 <= 32 * 1024 * 1024 else { throw GUIError.invalid("输出超过解析上限；完整日志已保存") }
        return try String(contentsOf: path, encoding: .utf8)
    }
    public func run(_ request: CLIRequest, directory: URL) async throws -> CLIResult {
        try await withCheckedThrowingContinuation { continuation in
            DispatchQueue.global(qos: .userInitiated).async {
                do { continuation.resume(returning: try self.execute(request, directory: directory)) }
                catch { continuation.resume(throwing: error) }
            }
        }
    }
    private func execute(_ request: CLIRequest, directory: URL) throws -> CLIResult {
        guard !request.operation.writes || configuration.allowPhysicalWrites else {
            throw GUIError.invalid("本次交付为离线/只读版本，物理 init/burn 未启用")
        }
        try configuration.validate()
        let executable: String
        if request.operation == .export {
            guard let adapter = configuration.exportCLI, configuration.trustedFiles[adapter] != nil else { throw GUIError.invalid("缺少受信任的只读导出适配器，请重新构建") }
            executable = adapter
        } else { executable = configuration.cli }
        try JobStore.secureDirectory(directory)
        try JSONSerialization.data(withJSONObject: ["operation": request.operation.rawValue, "arguments": request.arguments], options: [.prettyPrinted, .sortedKeys])
            .write(to: directory.appendingPathComponent("request.json"), options: .withoutOverwriting)
        var handles: [String: FileHandle] = [:]
        for name in ["stdout.log", "stderr.log", "bridge.stdout.log", "bridge.stderr.log", "exit.tmp"] {
            let url = directory.appendingPathComponent(name)
            let fd = open(url.path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0o600)
            guard fd >= 0 else { throw GUIError.invalid("日志位置已存在或不可用") }
            handles[name] = FileHandle(fileDescriptor: fd, closeOnDealloc: true)
        }
        defer { for handle in handles.values { try? handle.close() } }
        let lease = try request.operation.physical ? PhysicalLease(jobDirectory: directory) : nil
        let process = Process()
        process.currentDirectoryURL = directory
        process.environment = ["PATH": "/usr/bin:/bin:/usr/sbin:/sbin", "LC_ALL": "C"]
        if request.operation.physical {
            process.executableURL = URL(fileURLWithPath: "/usr/bin/osascript")
            let shell = ShellCodec.command(executable: executable, arguments: request.arguments, directory: directory)
            process.arguments = ["-e", "do shell script \(ShellCodec.appleString(shell)) with administrator privileges"]
            process.standardOutput = handles["bridge.stdout.log"]
            process.standardError = handles["bridge.stderr.log"]
        } else {
            process.executableURL = URL(fileURLWithPath: executable)
            process.arguments = request.arguments
            process.standardOutput = handles["stdout.log"]; process.standardError = handles["stderr.log"]
        }
        do { try process.run() }
        catch { try lease?.finish(); throw error } // 未启动认证进程，可以明确释放锁。
        process.waitUntilExit()
        var exitCode: Int32?, cancelled = false
        if request.operation.physical {
            let receipt = directory.appendingPathComponent("exit.code")
            if let text = try? String(contentsOf: receipt, encoding: .utf8), TextContract.matches(text, "^[0-9]{1,3}\n$"),
               let code = Int32(text.trimmingCharacters(in: .newlines)), code <= 255 {
                exitCode = code
                try lease?.finish()
            } else {
                let bridgeError = (try? Self.read(directory.appendingPathComponent("bridge.stderr.log"))) ?? ""
                cancelled = process.terminationReason == .exit && process.terminationStatus != 0 && bridgeError.contains("(-128)")
                if cancelled { try lease?.finish() }
                // 其他桥接失败不推断 root worker 已退出，保留 active 标记。
            }
        } else { exitCode = process.terminationReason == .exit ? process.terminationStatus : 128 + process.terminationStatus }
        let stdout = try Self.read(directory.appendingPathComponent("stdout.log"))
        var stderr = try Self.read(directory.appendingPathComponent("stderr.log"))
        if request.operation.physical && exitCode == nil { stderr += "\n" + (try Self.read(directory.appendingPathComponent("bridge.stderr.log"))) }
        let result = CLIResult(exitCode: exitCode, stdout: stdout, stderr: stderr, logDirectory: directory, authorizationCancelled: cancelled)
        let metadata: [String: Any] = ["operation": request.operation.rawValue, "arguments": request.arguments, "exitCode": exitCode.map { Int($0) } as Any? ?? NSNull(), "authorizationCancelled": cancelled, "workerStateUnknown": result.unknown]
        try JSONSerialization.data(withJSONObject: metadata, options: [.prettyPrinted, .sortedKeys]).write(to: directory.appendingPathComponent("transport.json"), options: .atomic)
        return result
    }
}
