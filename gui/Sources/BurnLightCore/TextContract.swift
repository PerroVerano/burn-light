import Foundation

public enum TextContract {
    public static func matches(_ text: String, _ pattern: String) -> Bool {
        text.range(of: pattern, options: .regularExpression) != nil
    }
    static func lines(_ text: String) throws -> [String] {
        guard !text.contains("\0"), text.utf8.count <= 32 * 1024 * 1024 else {
            throw GUIError.invalid("CLI 输出过大或包含无效字符，请检查完整日志")
        }
        return text.components(separatedBy: .newlines).filter { !$0.isEmpty }
    }
    static func uniqueFields(_ lines: [String], keys: Set<String>) throws -> [String: String] {
        var fields: [String: String] = [:]
        for line in lines {
            guard let split = line.range(of: ": ") else { continue }
            let key = String(line[..<split.lowerBound])
            guard keys.contains(key) else { continue }
            guard fields[key] == nil else { throw GUIError.invalid("CLI 字段重复：\(key)") }
            fields[key] = String(line[split.upperBound...])
        }
        guard Set(fields.keys) == keys else { throw GUIError.invalid("CLI 缺少必要字段：\(keys.subtracting(fields.keys).sorted().joined(separator: ", "))") }
        return fields
    }
    static func unsigned(_ text: String) throws -> UInt64 {
        guard matches(text, "^[0-9]+$"), let value = UInt64(text) else { throw GUIError.invalid("CLI 数值无效：\(text)") }
        return value
    }
    static func flag(_ text: String) throws -> Bool {
        guard text == "yes" || text == "no" else { throw GUIError.invalid("CLI 状态无效：\(text)") }
        return text == "yes"
    }
    public static func drives(_ result: CLIResult) throws -> [Drive] {
        if result.exitCode == 1 && result.stdout.isEmpty && result.stderr.trimmingCharacters(in: .whitespacesAndNewlines) == "appendcd: No native optical drive detected" { return [] }
        guard result.succeeded else { throw GUIError.invalid("发现光驱失败；请查看日志") }
        let records = try lines(result.stdout)
        guard !records.isEmpty, records.count % 5 == 0 else { throw GUIError.invalid("光驱输出格式不完整") }
        var drives: [Drive] = []
        for offset in stride(from: 0, to: records.count, by: 5) {
            let fields = try uniqueFields(Array(records[offset..<offset+5]), keys: ["Device", "Drive", "Optical BSD node", "Media", "Registry ID"])
            guard matches(fields["Device"]!, "^[0-9]+,[0-9]+,[0-9]+$"), !fields["Drive"]!.isEmpty,
                  try unsigned(fields["Registry ID"]!) > 0 else { throw GUIError.invalid("光驱身份无效") }
            let drive = Drive(device: fields["Device"]!, name: fields["Drive"]!, bsdNode: fields["Optical BSD node"]!, media: fields["Media"]!, registryID: fields["Registry ID"]!)
            guard !drives.contains(where: { $0.id == drive.id || $0.device == drive.device }) else { throw GUIError.invalid("光驱身份重复") }
            drives.append(drive)
        }
        return drives
    }
    static let discKeys: Set<String> = ["Drive", "Device", "Media", "Sessions", "Tracks", "Optical BSD node", "Registry ID", "Write speed", "Last session LBA", "Next writable LBA", "Free space", "Appendable", "Blank", "Physical next Track", "Unfinished Session"]
    static func discFromLines(_ records: [String], selected: Drive) throws -> Disc {
        let f = try uniqueFields(records, keys: discKeys)
        guard f["Device"] == selected.device, f["Registry ID"] == selected.registryID, f["Drive"] == selected.name else {
            throw GUIError.invalid("光驱已断开、替换或身份变化，请重新发现设备")
        }
        guard matches(f["Write speed"]!, "^[0-9]+([.][0-9]+)?x$"),
              matches(f["Free space"]!, "^[0-9]+ blocks / [0-9]+ bytes$") else { throw GUIError.invalid("容量或速度格式变化") }
        let capacity = f["Free space"]!.components(separatedBy: " ")
        let blocks = try unsigned(capacity[0]), bytes = try unsigned(capacity[3])
        guard blocks <= UInt64.max / 2048, blocks * 2048 == bytes else { throw GUIError.invalid("容量字段不一致") }
        guard try unsigned(f["Physical next Track"]!) <= 99,
              try unsigned(f["Sessions"]!) <= 99, try unsigned(f["Tracks"]!) <= 99,
              try unsigned(f["Last session LBA"]!) <= UInt64(UInt32.max), try unsigned(f["Next writable LBA"]!) <= UInt64(UInt32.max), blocks <= UInt64(UInt32.max) else {
            throw GUIError.invalid("介质字段超出核心地址与 Track 范围")
        }
        var disc = try Disc(drive: selected, media: f["Media"]!, sessions: unsigned(f["Sessions"]!), tracks: unsigned(f["Tracks"]!), freeBytes: bytes,
                        appendable: flag(f["Appendable"]!), blank: flag(f["Blank"]!), unfinished: flag(f["Unfinished Session"]!),
                        lastLBA: unsigned(f["Last session LBA"]!), nextLBA: unsigned(f["Next writable LBA"]!))
        let formats = records.filter { $0.hasPrefix("Filesystem: ") }
        let views = records.filter { $0.hasPrefix("Filesystem view: ") }
        let compatibility = records.filter { $0.hasPrefix("UDF compatibility: ") }
        guard formats.count <= 1, views.count <= 1, compatibility.count <= 1 else { throw GUIError.invalid("文件系统绑定重复") }
        if let line = formats.first {
            let format = String(line.dropFirst(12))
            guard ["udf1.02", "udf2.01+vat"].contains(format) else { throw GUIError.invalid("不支持的文件系统") }
            disc.filesystem = format
        }
        if let line = views.first {
            let view = String(line.dropFirst(17))
            guard disc.filesystem == "udf2.01+vat", matches(view, "^[a-f0-9]{64}$") else { throw GUIError.invalid("无效的 VAT 事务绑定") }
            disc.filesystemView = view
        }
        guard disc.filesystem != "udf2.01+vat" || disc.filesystemView != nil else { throw GUIError.invalid("缺少 VAT 事务绑定") }
        if let line = compatibility.first {
            guard disc.filesystem == "udf2.01+vat", line == "UDF compatibility: windows-fid-location-v1" else { throw GUIError.invalid("不支持的 UDF 兼容分支") }
            disc.udfCompatibility = "windows-fid-location-v1"
        }
        return disc
    }
    public static func disc(_ result: CLIResult, selected: Drive) throws -> Disc {
        guard result.succeeded else { throw GUIError.invalid(result.authorizationCancelled ? "管理员认证已取消" : "介质检查未完成；请查看日志") }
        let records = try lines(result.stdout)
        for line in records where !knownHeader(line) && !matches(line, "^Track [0-9]+: start=[0-9]+ blocks=[0-9]+ reserved=[01] blank=[01] NWA-valid=[01]$") {
            throw GUIError.invalid("无法识别介质输出：\(line)")
        }
        return try discFromLines(records, selected: selected)
    }
    static func knownHeader(_ line: String) -> Bool {
        if let split = line.range(of: ": "), discKeys.contains(String(line[..<split.lowerBound])) { return true }
        return line == "OPTICAL EXPORT VERIFIED" || line.hasPrefix("Evidence directory: ") || line.hasPrefix("Filesystem recognition: ") ||
            matches(line, "^(ISO9660 PVD|Joliet SVD) LBA=[0-9]+ VolumeSpaceSize=[0-9]+ Root=[0-9]+ RootBytes=[0-9]+ PathTableBytes=[0-9]+ L=[0-9]+ M=[0-9]+$") || line.hasPrefix("UDF volume: ") ||
            matches(line, "^Filesystem: udf(1[.]02|2[.]01[+]vat)$") || matches(line, "^Filesystem view: [a-f0-9]{64}$") || line == "UDF compatibility: windows-fid-location-v1"
    }
    // C++ std::quoted 只转义反斜线和双引号；不能按空格拆分文件名。
    static func quoted(_ text: String) throws -> (String, String) {
        var chars = Array(text); guard chars.first == "\"" else { throw GUIError.invalid("目录项缺少引号") }
        chars.removeFirst(); var path = "", escaped = false
        for (index, char) in chars.enumerated() {
            if escaped { path.append(char); escaped = false; continue }
            if char == "\\" { escaped = true; continue }
            if char == "\"" { return (path, String(chars.dropFirst(index + 1))) }
            path.append(char)
        }
        throw GUIError.invalid("目录项引号不完整")
    }
    public static func tree(_ result: CLIResult, selected: Drive, matching expected: Disc) throws -> [FileEntry] {
        let entries = try tree(result)
        let headers = try lines(result.stdout).filter { !$0.hasPrefix("DIR  ") && !$0.hasPrefix("FILE ") }
        let headerResult = CLIResult(exitCode: result.exitCode, stdout: headers.joined(separator: "\n"), stderr: result.stderr, logDirectory: result.logDirectory)
        guard try disc(headerResult, selected: selected) == expected else {
            throw GUIError.invalid("目录查询与介质检查的 Session、地址或容量不同，请重新检查介质")
        }
        return entries
    }
    public static func tree(_ result: CLIResult) throws -> [FileEntry] {
        guard result.succeeded else { throw GUIError.invalid("目录读取未完成") }
        var entries: [FileEntry] = []
        for line in try lines(result.stdout) {
            if line.hasPrefix("DIR  ") || line.hasPrefix("FILE ") {
                let directory = line.hasPrefix("DIR  ")
                let (path, suffix) = try quoted(String(line.dropFirst(5)))
                guard !path.isEmpty, !entries.contains(where: { $0.path == path }) else { throw GUIError.invalid("目录项为空或重复") }
                let bytes: UInt64
                if directory {
                    guard suffix.isEmpty else { throw GUIError.invalid("未知目录字段") }; bytes = 0
                } else {
                    guard matches(suffix, "^ bytes=[0-9]+( LBA=[0-9]+[+][0-9]+)+$") || suffix == " bytes=0" else { throw GUIError.invalid("文件大小或 extent 字段不完整") }
                    bytes = try unsigned(String(suffix.components(separatedBy: " ")[1].dropFirst(6)))
                }
                entries.append(FileEntry(path: path, directory: directory, bytes: bytes))
            } else if !knownHeader(line) && !matches(line, "^Track [0-9]+: start=[0-9]+ blocks=[0-9]+ reserved=[01] blank=[01] NWA-valid=[01]$") {
                throw GUIError.invalid("无法识别目录输出：\(line)")
            }
        }
        guard !entries.isEmpty else { throw GUIError.invalid("没有可解析的目录树") }
        return entries
    }
    public static func evidence(_ result: CLIResult, under parent: URL) throws -> URL {
        let paths = try lines(result.stdout).filter { $0.hasPrefix("Evidence directory: ") }.map { String($0.dropFirst(20)) }
        guard paths.count == 1 else { throw GUIError.invalid("记录目录缺失或重复") }
        let url = URL(fileURLWithPath: paths[0]).standardizedFileURL
        guard url.deletingLastPathComponent().path == parent.standardizedFileURL.path, matches(url.lastPathComponent, "^run-[A-Za-z0-9]{6}$") else {
            throw GUIError.invalid("CLI 记录目录越过本任务边界")
        }
        let values = try url.resourceValues(forKeys: [.isSymbolicLinkKey, .isDirectoryKey])
        guard values.isDirectory == true, values.isSymbolicLink != true else { throw GUIError.invalid("记录目录不是安全目录") }
        return url
    }
    public static func physicalSuccess(_ result: CLIResult, request: CLIOperation, selected: Drive, evidence: URL) throws -> Disc {
        guard result.succeeded else { throw GUIError.invalid("物理操作没有明确成功") }
        let marker = try lines(String(contentsOf: evidence.appendingPathComponent("result.txt"), encoding: .utf8))
        let records = try lines(result.stdout)
        guard marker.count == 5, marker[0] == "PHYSICAL_READBACK_PASS", marker[1] == "WINDOWS_ACCEPTANCE_PENDING",
              records.contains("OPTICAL READBACK VERIFIED (Windows acceptance still required)") || records.contains("OPTICAL UDF READBACK VERIFIED (Windows acceptance still required)") else {
            throw GUIError.invalid("缺少实际光盘读回证据")
        }
        let fields = try uniqueFields(Array(marker.dropFirst(2)), keys: ["Media", "Session start", "Image SHA256"])
        guard matches(fields["Image SHA256"]!, "^[a-f0-9]{64}$") else { throw GUIError.invalid("读回证据 hash 格式无效") }
        let starts = records.indices.filter { records[$0].hasPrefix("Drive: ") }
        guard starts.count == (request.writes ? 2 : 1) else { throw GUIError.invalid("读回介质状态缺失或重复") }
        let start = starts.last!
        let disc = try discFromLines(Array(records[start...]), selected: selected)
        guard disc.appendable, !disc.unfinished, !disc.blank, disc.nextLBA > disc.lastLBA,
              fields["Media"] == disc.media, try unsigned(fields["Session start"]!) == disc.lastLBA else { throw GUIError.invalid("读回后介质状态或结果证据尚未满足完成条件") }
        if request.writes {
            let before = try discFromLines(Array(records[starts[0]..<start]), selected: selected)
            guard disc.sessions == before.sessions + 1, disc.tracks == before.tracks + 1,
                  records.contains("Physical readback passed, \(disc.media) remains appendable. Windows acceptance PENDING.") else {
                throw GUIError.invalid("Session/Track 增量或最终读回结果不一致")
            }
        } else {
            guard records.contains("PHYSICAL READBACK PASS; Windows acceptance PENDING") else { throw GUIError.invalid("缺少只读补验完成标记") }
        }
        return disc
    }
}
