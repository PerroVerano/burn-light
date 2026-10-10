import Foundation
import Darwin
import BurnLightCore

struct CheckFailure: Error { let message: String }

final class Checks {
    var names: [String] = []
    func expect(_ name: String, _ condition: @autoclosure () throws -> Bool) throws {
        guard try condition() else { throw CheckFailure(message: name) }; names.append(name); print("PASS \(name)")
    }
    func rejects(_ name: String, _ action: () throws -> Void) throws {
        do { try action() } catch { names.append(name); print("PASS \(name)"); return }
        throw CheckFailure(message: "Expected rejection: " + name)
    }
}

let driveText = "Device: 1,0,0\nDrive: Lenovo Ultraslim DVD XL6M\nOptical BSD node: /dev/disk4\nMedia: CD-R\nRegistry ID: 4295022013\n"
func discText(blank: Bool = false, appendable: Bool = true, unfinished: Bool = false, sessions: Int = 2) -> String {
    "Drive: Lenovo Ultraslim DVD XL6M\nDevice: 1,0,0\nMedia: CD-R\nSessions: \(blank ? 0 : sessions)\nTracks: \(blank ? 0 : sessions)\nOptical BSD node: /dev/disk4\nRegistry ID: 4295022013\nWrite speed: 10x\nLast session LBA: \(blank ? 0 : 11702)\nNext writable LBA: \(blank ? 0 : 18904)\nFree space: 340939 blocks / 698243072 bytes\nAppendable: \(appendable ? "yes" : "no")\nBlank: \(blank ? "yes" : "no")\nPhysical next Track: 3\nUnfinished Session: \(unfinished ? "yes" : "no")\nTrack 3: start=18904 blocks=340939 reserved=0 blank=1 NWA-valid=1\n"
}
func result(_ text: String, exit: Int32 = 0, directory: URL = URL(fileURLWithPath: "/private/tmp")) -> CLIResult {
    CLIResult(exitCode: exit, stdout: text, stderr: "", logDirectory: directory)
}

actor MockRunner: CLIRunning {
    var operations: [CLIOperation] = []
    var requests: [CLIRequest] = []
    func arguments(_ operation: CLIOperation) -> [String]? { requests.last { $0.operation == operation }?.arguments }
    var failWrites = false
    var unknownWrites = false
    var cancelledWrites = false
    var blank = false
    var sessions = 2
    func configure(fail: Bool = false, unknown: Bool = false, cancelled: Bool = false, blank: Bool = false) {
        failWrites = fail; unknownWrites = unknown; cancelledWrites = cancelled; self.blank = blank
    }
    func writes() -> Int { operations.filter { $0.writes }.count }
    func run(_ request: CLIRequest, directory: URL) async throws -> CLIResult {
        operations.append(request.operation)
        requests.append(request)
        try JobStore.secureDirectory(directory)
        switch request.operation {
        case .drives: return result(driveText, directory: directory)
        case .verify: return result("IMAGE VERIFIED; this does not prove Windows mounting\n", directory: directory)
        case .initialBuild, .build:
            let image = URL(fileURLWithPath: request.arguments[2])
            try Data("mock image".utf8).write(to: image)
            try Data("mock manifest".utf8).write(to: URL(fileURLWithPath: image.path + ".manifest"))
            return result(request.operation == .initialBuild ? "INITIAL IMAGE VERIFIED; Windows acceptance PENDING\n" : "BUILD VERIFIED\n", directory: directory)
        default:
            let parent = URL(fileURLWithPath: request.arguments.last!)
            let evidence = parent.appendingPathComponent("run-ABC123")
            if !FileManager.default.fileExists(atPath: evidence.path) { try JobStore.secureDirectory(evidence) }
            let prefix = "Evidence directory: \(evidence.path)\n"
            if request.operation == .inspect { return result(prefix + discText(blank: blank, sessions: sessions), directory: directory) }
            if request.operation == .list { return result(prefix + discText(sessions: sessions) + "FILE \"old.txt\" bytes=5 LBA=33+5\n", directory: directory) }
            if request.operation == .export {
                let files = evidence.appendingPathComponent("files"); try JobStore.secureDirectory(files)
                try Data("hello".utf8).write(to: files.appendingPathComponent("old.txt"))
                let receipt = "{\"schema\":1,\"sourceKind\":\"optical\",\"filesystem\":\"iso9660+joliet\",\"device\":\"1,0,0\",\"registryID\":\"4295022013\",\"media\":\"CD-R\",\"entries\":[{\"path\":\"old.txt\",\"directory\":false,\"bytes\":5,\"sha256\":\"\(try Configuration.digest(files.appendingPathComponent("old.txt")))\"}]}"
                try Data(receipt.utf8).write(to: evidence.appendingPathComponent("export.json"))
                try Data("OPTICAL_EXPORT_VERIFIED\n".utf8).write(to: evidence.appendingPathComponent("result.txt"))
                return result(prefix + discText() + "OPTICAL EXPORT VERIFIED\n", directory: directory)
            }
            if unknownWrites { return CLIResult(exitCode: nil, stdout: prefix, stderr: "bridge lost", logDirectory: directory) }
            if cancelledWrites { return CLIResult(exitCode: nil, stdout: "", stderr: "(-128)", logDirectory: directory, authorizationCancelled: true) }
            let image = evidence.appendingPathComponent(request.operation == .initialize ? "session-first.iso" : "session-next.iso")
            try Data("mock".utf8).write(to: image); try Data("mock".utf8).write(to: URL(fileURLWithPath: image.path + ".manifest"))
            if failWrites { return result(prefix + discText(blank: blank), exit: 1, directory: directory) }
            try Data("PHYSICAL_READBACK_PASS\nWINDOWS_ACCEPTANCE_PENDING\nMedia: CD-R\nSession start: 11702\nImage SHA256: \(String(repeating: "a", count: 64))\n".utf8).write(to: evidence.appendingPathComponent("result.txt"))
            let success = "OPTICAL READBACK VERIFIED (Windows acceptance still required)\n"
            if request.operation == .readback {
                return result(prefix + discText(sessions: 3) + success + "PHYSICAL READBACK PASS; Windows acceptance PENDING\n", directory: directory)
            }
            let before = discText(blank: blank, sessions: sessions)
            sessions = blank ? 1 : sessions + 1; blank = false
            return result(prefix + before + discText(sessions: sessions) + success + "Physical readback passed, CD-R remains appendable. Windows acceptance PENDING.\n", directory: directory)
        }
    }
}

@main
struct ValidationMain {
    static func main() async {
        let checks = Checks()
        let root = URL(fileURLWithPath: "/private/tmp/burnlight-validation-\(UUID().uuidString)", isDirectory: true)
        do {
            try JobStore.secureDirectory(root)
            try contracts(checks, root)
            let snapshot = try staging(checks, root)
            try locksAndShell(checks, root)
            try exports(checks, root)
            try archivesAndRecovery(checks, root)
            try await offline(checks, root, snapshot)
            try await models(checks, root, snapshot)
            let output: [String: Any] = ["result": "PASS", "checks": checks.names, "count": checks.names.count, "root": root.path, "physicalWrites": 0, "realAdminBridgeTestedHere": false]
            try JSONSerialization.data(withJSONObject: output, options: [.prettyPrinted, .sortedKeys]).write(to: URL(fileURLWithPath: CommandLine.arguments[1]), options: .atomic)
            print("PASS: \(checks.names.count) assertions; zero physical writes")
        } catch {
            print("FAIL: \(error)")
            exit(1)
        }
    }
    static func contracts(_ c: Checks, _ root: URL) throws {
        let drive = try TextContract.drives(result(driveText))[0]
        let file = FileEntry(path: "目录/same.txt", directory: false, bytes: 1)
        let folder = FileEntry(path: "目录", directory: true, bytes: 0)
        try c.expect("same-path files detected but directories merge", try ConflictPolicy.conflicts(input: [folder, file], existing: [folder, file]) == [file.path])
        try c.rejects("conflict choices reject directory versus file", { _ = try ConflictPolicy.conflicts(input: [FileEntry(path: "目录", directory: false, bytes: 0)], existing: [folder]) })
        try c.rejects("duplicate imported paths cannot trap", { _ = try ConflictPolicy.conflicts(input: [file], existing: [file, file]) })
        let replaceRequest = try CLIRequest(.burn, input: root, drive: drive, evidence: root, conflictPolicy: .replace)
        try c.expect("replace option is a discrete argv pair", replaceRequest.arguments.contains("--on-conflict") && replaceRequest.arguments.contains("replace"))
        try c.expect("default CLI arguments preserve rejection contract", !CLIRequest(.burn, input: root, drive: drive, evidence: root).arguments.contains("--on-conflict"))
        try c.rejects("init never receives append conflict choice", { _ = try CLIRequest(.initialize, input: root, drive: drive, evidence: root, conflictPolicy: .skip) })
        try c.expect("single drive identity", drive.device == "1,0,0" && drive.registryID == "4295022013")
        try c.expect("no native drive exit 1 recognized", TextContract.drives(CLIResult(exitCode: 1, stdout: "", stderr: "appendcd: No native optical drive detected\n", logDirectory: root)).isEmpty)
        try c.rejects("arbitrary drive failure not no-device", { _ = try TextContract.drives(CLIResult(exitCode: 1, stdout: "", stderr: "error", logDirectory: root)) })
        try c.rejects("duplicate drive identity", { _ = try TextContract.drives(result(driveText + driveText)) })
        try c.rejects("partial discovery disabled", { _ = try TextContract.drives(result("Device: 1,0,0\n")) })
        let second = driveText.replacingOccurrences(of: "1,0,0", with: "1,2,0").replacingOccurrences(of: "4295022013", with: "4295022020")
        try c.expect("same model two distinct identities", TextContract.drives(result(driveText + second)).count == 2)
        try c.expect("blank maps only to init", TextContract.disc(result(discText(blank: true)), selected: drive).writeCLIOperation == .initialize)
        try c.expect("existing complete disc maps only to burn", TextContract.disc(result(discText()), selected: drive).writeCLIOperation == .burn)
        try c.expect("closed disc disables writing", TextContract.disc(result(discText(appendable: false)), selected: drive).writeCLIOperation == nil)
        try c.expect("unfinished Session disables writing", TextContract.disc(result(discText(unfinished: true)), selected: drive).writeCLIOperation == nil)
        let zero = discText().replacingOccurrences(of: "340939 blocks / 698243072 bytes", with: "0 blocks / 0 bytes")
        try c.expect("full disc disables writing", TextContract.disc(result(zero), selected: drive).writeCLIOperation == nil)
        let unknown = discText().replacingOccurrences(of: "Media: CD-R", with: "Media: BD-R")
        try c.expect("unknown media disables writing", TextContract.disc(result(unknown), selected: drive).writeCLIOperation == nil)
        try c.rejects("duplicate field rejected", { _ = try TextContract.disc(result(discText() + "Blank: no\n"), selected: drive) })
        try c.rejects("missing field rejected", { _ = try TextContract.disc(result(discText().replacingOccurrences(of: "Blank: no\n", with: "")), selected: drive) })
        try c.rejects("identity change rejected", { _ = try TextContract.disc(result(discText().replacingOccurrences(of: "4295022013", with: "1")), selected: drive) })
        try c.rejects("unknown output rejected", { _ = try TextContract.disc(result(discText() + "new-format=yes\n"), selected: drive) })
        try c.rejects("inconsistent capacity rejected", { _ = try TextContract.disc(result(discText().replacingOccurrences(of: "698243072", with: "10")), selected: drive) })
        try c.rejects("overflow rejected without trap", { _ = try TextContract.disc(result(discText().replacingOccurrences(of: "Sessions: 2", with: "Sessions: 18446744073709551615")), selected: drive) })
        let tree = try TextContract.tree(result(discText() + "DIR  \"中文 目录\"\nFILE \"中文 目录/a \\\"quote\\\" $file.txt\" bytes=3 LBA=33+3\nFILE \"empty.txt\" bytes=0\n"))
        try c.expect("std quoted spaces Chinese quotes dollars", tree[1].path == "中文 目录/a \"quote\" $file.txt" && tree[1].bytes == 3)
        try c.rejects("unclosed quote rejected", { _ = try TextContract.tree(result("FILE \"broken bytes=1 LBA=1+1\n")) })
        try c.rejects("inline unmapped not silently omitted", { _ = try TextContract.tree(result("INLINE/UNMAPPED: \"x\"\n")) })
        try c.rejects("duplicate file rejected", { _ = try TextContract.tree(result("FILE \"x\" bytes=1 LBA=1+1\nFILE \"x\" bytes=1 LBA=1+1\n")) })
        let inspected = try TextContract.disc(result(discText()), selected: drive)
        let fileLine = "FILE \"x\" bytes=1 LBA=33+1\n"
        try c.expect("listed tree bound to matching inspected disc", TextContract.tree(result(discText() + fileLine), selected: drive, matching: inspected).count == 1)
        for field in ["Last session LBA: 11702", "Next writable LBA: 18904", "Sessions: 2", "Tracks: 2", "Registry ID: 4295022013"] {
            let altered = discText().replacingOccurrences(of: field, with: String(field.prefix { $0 != ":" }) + ": 0")
            try c.rejects("listed tree rejects changed " + field, { _ = try TextContract.tree(result(altered + fileLine), selected: drive, matching: inspected) })
        }
        try c.rejects("listed tree requires identity and geometry headers", { _ = try TextContract.tree(result(fileLine), selected: drive, matching: inspected) })
        let request = try CLIRequest(.burn, input: root, drive: drive, evidence: root)
        try c.expect("physical argv explicit device without LBA", request.arguments == ["burn", root.path, "--device", "1,0,0", "--work-dir", root.path])
        try c.expect("init-build has no physical options", CLIRequest(.initialBuild, input: root, image: root).arguments.count == 3)
        try c.rejects("physical operation needs explicit device", { _ = try CLIRequest(.inspect, evidence: root) })
        for op in [CLIOperation.inspect, .list, .build, .readback, .initialize, .burn, .export] { try c.expect("\(op.rawValue) requires global lease", op.physical) }
        let evidence = root.appendingPathComponent("run-ABC123"); try JobStore.secureDirectory(evidence)
        let completion = result("Evidence directory: \(evidence.path)\n" + discText() + discText(sessions: 3) + "OPTICAL READBACK VERIFIED (Windows acceptance still required)\nPhysical readback passed, CD-R remains appendable. Windows acceptance PENDING.\n")
        try Data("PHYSICAL_READBACK_PASS\nWINDOWS_ACCEPTANCE_PENDING\nMedia: CD-R\nSession start: 11702\nImage SHA256: \(String(repeating: "a", count: 64))\n".utf8).write(to: evidence.appendingPathComponent("result.txt"))
        try c.expect("successful physical marker plus Session increment", TextContract.physicalSuccess(completion, request: .burn, selected: drive, evidence: evidence).sessions == 3)
        try c.rejects("cdrecord exit zero alone is not success", { _ = try TextContract.physicalSuccess(result("cdrecord exit=0\n"), request: .burn, selected: drive, evidence: evidence) })
        try c.rejects("same Session count is not completion", { _ = try TextContract.physicalSuccess(result(completion.stdout.replacingOccurrences(of: "Sessions: 3", with: "Sessions: 2")), request: .burn, selected: drive, evidence: evidence) })
        try c.rejects("result file without optical verification rejected", { _ = try TextContract.physicalSuccess(result(completion.stdout.replacingOccurrences(of: "OPTICAL READBACK VERIFIED (Windows acceptance still required)", with: "")), request: .burn, selected: drive, evidence: evidence) })
        try c.rejects("substring success text not accepted as final marker", { _ = try TextContract.physicalSuccess(result(completion.stdout.replacingOccurrences(of: "Physical readback passed,", with: "FILE named Physical readback passed,")), request: .burn, selected: drive, evidence: evidence) })
        try c.expect("evidence bounded to dedicated task", TextContract.evidence(completion, under: root).resolvingSymlinksInPath().path == evidence.resolvingSymlinksInPath().path)
        try c.rejects("outside evidence path rejected", { _ = try TextContract.evidence(result("Evidence directory: /private/tmp/run-ABC123\n"), under: root) })
    }
    static func staging(_ c: Checks, _ root: URL) throws -> InputSnapshot {
        let source = root.appendingPathComponent("用户目录 ' $ ` 中文")
        try JobStore.secureDirectory(source)
        let folder = source.appendingPathComponent("中文目录"); try JobStore.secureDirectory(folder)
        try Data("hello".utf8).write(to: source.appendingPathComponent("a b '$` .txt"))
        try Data().write(to: folder.appendingPathComponent("empty.txt"))
        try Data("ignored".utf8).write(to: source.appendingPathComponent(".DS_Store"))
        try Data("ignored".utf8).write(to: folder.appendingPathComponent(".DS_Store"))
        let snapshot = try Staging.create(source: source)
        try c.expect("snapshot preserves relative paths and hashes", snapshot.fileCount == 2 && snapshot.totalBytes == 5 && snapshot.entries.contains { $0.path == "中文目录/empty.txt" })
        try c.expect("Finder cache excluded recursively", !snapshot.entries.contains { $0.path.contains(".DS_Store") })
        try Staging.validate(snapshot); try c.expect("snapshot survives complete revalidation", true)
        try Data("source changed".utf8).write(to: source.appendingPathComponent("a b '$` .txt"))
        try Staging.validate(snapshot); try c.expect("snapshot independent of later source edits", true)
        let link = source.appendingPathComponent("link")
        try FileManager.default.createSymbolicLink(at: link, withDestinationURL: folder)
        try c.rejects("symlink rejected before copying", { _ = try Staging.create(source: source) })
        try FileManager.default.removeItem(at: link)
        let fifo = source.appendingPathComponent("fifo"); mkfifo(fifo.path, 0o600)
        try c.rejects("special file rejected without blocking", { _ = try Staging.create(source: source) })
        try FileManager.default.removeItem(at: fifo)
        let token = CancellationFlag(); token.cancel()
        try c.rejects("staging cancellation stays before submit", { _ = try Staging.create(source: source, flag: token) })
        let mutation = root.appendingPathComponent("mutation"); try JobStore.secureDirectory(mutation)
        let mutableFile = mutation.appendingPathComponent("large.bin")
        try Data(repeating: 1, count: 64 * 1024 * 1024).write(to: mutableFile)
        let stop = CancellationFlag(), started = DispatchSemaphore(value: 0), stopped = DispatchSemaphore(value: 0)
        DispatchQueue.global().async {
            let handle = try! FileHandle(forWritingTo: mutableFile); started.signal()
            while (try? stop.check()) != nil { try? handle.seek(toOffset: 0); try? handle.write(contentsOf: Data([2])); usleep(1000) }
            try? handle.close(); stopped.signal()
        }
        started.wait()
        try c.rejects("source changes during copying rejected", { _ = try Staging.create(source: mutation) })
        stop.cancel(); stopped.wait()
        return snapshot
    }
    static func locksAndShell(_ c: Checks, _ root: URL) throws {
        let lockRoot = root.appendingPathComponent("lock")
        var first: PhysicalLease? = try PhysicalLease(jobDirectory: root, root: lockRoot)
        try c.rejects("another GUI instance cannot start physical task", { _ = try PhysicalLease(jobDirectory: root, root: lockRoot) })
        first = nil
        try c.rejects("unknown worker marker survives GUI lease destruction", { _ = try PhysicalLease(jobDirectory: root, root: lockRoot) })
        try FileManager.default.removeItem(at: lockRoot.appendingPathComponent("active.json")) // synthetic test fixture only
        let known = try PhysicalLease(jobDirectory: root, root: lockRoot); try known.finish()
        try c.expect("known completion removes active marker", !FileManager.default.fileExists(atPath: lockRoot.appendingPathComponent("active.json").path))
        _ = first
        let directory = root.appendingPathComponent("shell ' $ ` 中文"); try JobStore.secureDirectory(directory)
        let value = "中文 空格 ' \" $HOME $(touch /private/tmp/BURNLIGHT_MUST_NOT_EXIST) `id` \\ end"
        let process = Process(); process.executableURL = URL(fileURLWithPath: "/bin/sh")
        process.arguments = ["-c", ShellCodec.command(executable: "/usr/bin/printf", arguments: ["%s", value], directory: directory)]
        try process.run(); process.waitUntilExit()
        try c.expect("shell transport roundtrips hostile filenames literally", String(contentsOf: directory.appendingPathComponent("stdout.log"), encoding: .utf8) == value)
        try c.expect("shell writes distinct CLI exit receipt", String(contentsOf: directory.appendingPathComponent("exit.code"), encoding: .utf8) == "0\n")
        try c.expect("no command substitution executed", !FileManager.default.fileExists(atPath: "/private/tmp/BURNLIGHT_MUST_NOT_EXIST"))
        let script = ShellCodec.appleString(value)
        let decode = Process(); decode.executableURL = URL(fileURLWithPath: "/usr/bin/osascript"); decode.arguments = ["-e", "return \(script)"]
        let pipe = Pipe(); decode.standardOutput = pipe; try decode.run()
        let data = pipe.fileHandleForReading.readDataToEndOfFile(); decode.waitUntilExit()
        try c.expect("AppleScript encoding roundtrips quotes and backslash", String(data: data, encoding: .utf8) == value + "\n")
    }
    static func offline(_ c: Checks, _ root: URL, _ snapshot: InputSnapshot) async throws {
        let project = URL(fileURLWithPath: CommandLine.arguments[2])
        let config = try Configuration.bundled(contents: project.appendingPathComponent("gui/output/BurnLight.app/Contents"), storage: root.appendingPathComponent("local-storage"))
        try config.validate()
        try c.expect("bundled configuration resolves only relative dependencies", config.cli.hasSuffix("Contents/MacOS/appendcd") && config.trustedFiles.count == 7 && !config.allowPhysicalWrites)
        let runner = CLIRunner(configuration: config)
        let image = root.appendingPathComponent("actual-initial.iso")
        let built = try await runner.run(CLIRequest(.initialBuild, input: snapshot.directory, image: image), directory: root.appendingPathComponent("init-build"))
        try c.expect("real CLI offline init-build with staged hostile path", built.succeeded && built.stdout.contains("INITIAL IMAGE VERIFIED; Windows acceptance PENDING"))
        let verified = try await runner.run(CLIRequest(.verify, image: image), directory: root.appendingPathComponent("verify"))
        try c.expect("real CLI verifies generated metadata and payload hashes", verified.succeeded && verified.stdout.contains("IMAGE VERIFIED; this does not prove Windows mounting"))
        let captured = try Staging.readbackMaterials(image: image, directory: root.appendingPathComponent("captured-readback"))
        try c.expect("readback image captured under user permissions", Configuration.digest(captured) == Configuration.digest(image))
        try c.expect("readback manifest preserved verbatim", Data(contentsOf: URL(fileURLWithPath: captured.path + ".manifest")) == Data(contentsOf: URL(fileURLWithPath: image.path + ".manifest")))
        let discovery = try await runner.run(CLIRequest(.drives), directory: root.appendingPathComponent("drives"))
        _ = try TextContract.drives(discovery); try c.expect("real native drive text accepted", true)
        let sampleDrive = try TextContract.drives(result(driveText))[0]
        for operation in [CLIOperation.initialize, .burn] {
            var rejected = false
            do { _ = try await runner.run(CLIRequest(operation, input: snapshot.directory, drive: sampleDrive, evidence: root), directory: root.appendingPathComponent("forbidden-\(operation.rawValue)")) }
            catch { rejected = error.localizedDescription.contains("物理 init/burn 未启用") }
            try c.expect("read-only delivery blocks \(operation.rawValue) before admin launch", rejected)
        }
        let changedConfig = Configuration(cli: config.cli, trustedFiles: [config.cli: String(repeating: "0", count: 64)], recordDirectory: root.path)
        try c.rejects("changed CLI hash rejected before process start", { try changedConfig.validate() })
        let noisy = root.appendingPathComponent("noisy-cli")
        try Data("#!/bin/sh\n/usr/bin/yes stdout | /usr/bin/head -c 2000000\n/usr/bin/yes stderr | /usr/bin/head -c 2000000 >&2\nexit 7\n".utf8).write(to: noisy)
        chmod(noisy.path, 0o700)
        let noisyConfig = Configuration(cli: noisy.path, trustedFiles: [noisy.path: try Configuration.digest(noisy)], recordDirectory: root.path)
        let output = try await CLIRunner(configuration: noisyConfig).run(CLIRequest(.drives), directory: root.appendingPathComponent("noisy"))
        try c.expect("large stdout stderr do not block and retain exit code", output.exitCode == 7 && output.stdout.utf8.count == 2_000_000 && output.stderr.utf8.count == 2_000_000)
        let records = try JobStore(directory: URL(fileURLWithPath: config.recordDirectory)).records()
        if let live = records.first(where: { $0.operation == .readback && $0.phase == .completed }), let drive = live.device, let evidencePath = live.evidencePath {
            let job = URL(fileURLWithPath: "/private/tmp/burnlight-job-\(live.id.uuidString)")
            for command in try FileManager.default.contentsOfDirectory(at: job, includingPropertiesForKeys: nil) where command.lastPathComponent.hasPrefix("command-") {
                let metadata = try JSONSerialization.jsonObject(with: Data(contentsOf: command.appendingPathComponent("transport.json"))) as! [String: Any]
                if metadata["operation"] as? String == "readback" {
                    let real = CLIResult(exitCode: 0, stdout: try String(contentsOf: command.appendingPathComponent("stdout.log"), encoding: .utf8), stderr: try String(contentsOf: command.appendingPathComponent("stderr.log"), encoding: .utf8), logDirectory: command)
                    let verifiedDisc = try TextContract.physicalSuccess(real, request: .readback, selected: drive, evidence: URL(fileURLWithPath: evidencePath))
                    try c.expect("strict adapter accepts saved actual GUI optical readback", verifiedDisc.sessions == 2 && verifiedDisc.tracks == 2 && verifiedDisc.appendable)
                }
            }
        }
    }
    static func exports(_ c: Checks, _ root: URL) throws {
        let files = [FileEntry(path: "中文目录", directory: true, bytes: 0), FileEntry(path: "中文目录/empty.txt", directory: false, bytes: 0), FileEntry(path: "other.txt", directory: false, bytes: 5)]
        try c.expect("export directory selection includes descendants", DiscExport.selection(files, roots: ["中文目录"]).count == 2)
        try c.expect("export nested file includes parent directory", DiscExport.selection(files, roots: ["中文目录/empty.txt"]).count == 2)
        try c.expect("empty selection exports all", DiscExport.selection(files, roots: []).count == 3)
        try c.rejects("unknown selected file rejected", { _ = try DiscExport.selection(files, roots: ["absent"]) })
        for path in ["../outside", "/absolute", "a//b", "a/../b", "a\\b", "a\nb"] {
            try c.rejects("unsafe export path rejected: \(path.debugDescription)", { _ = try DiscExport.selection([FileEntry(path: path, directory: false, bytes: 1)], roots: []) })
        }
        let drive = try TextContract.drives(result(driveText))[0]
        let request = try CLIRequest(.export, input: root.appendingPathComponent("export-selection.txt"), drive: drive, evidence: root)
        try c.expect("export is readonly explicit target without LBA args", request.operation.physical && !request.operation.writes && !request.arguments.contains("--last") && request.arguments.contains(drive.registryID))
        let evidence = root.appendingPathComponent("export-cache"); try JobStore.secureDirectory(evidence)
        let content = evidence.appendingPathComponent("files"); try JobStore.secureDirectory(content)
        try JobStore.secureDirectory(content.appendingPathComponent("中文目录"))
        try Data().write(to: content.appendingPathComponent("中文目录/empty.txt"))
        try Data("hello".utf8).write(to: content.appendingPathComponent("other.txt"))
        try Data("Finder data".utf8).write(to: content.appendingPathComponent(".DS_Store"))
        let data: [String: Any] = ["schema": 1, "sourceKind": "optical", "filesystem": "iso9660+joliet", "device": drive.device, "registryID": drive.registryID, "media": "CD-R", "entries": files.map { entry in ["path": entry.path, "directory": entry.directory, "bytes": entry.bytes, "sha256": entry.directory ? NSNull() : try! Configuration.digest(content.appendingPathComponent(entry.path))] } + [["path": ".DS_Store", "directory": false, "bytes": 11, "sha256": try Configuration.digest(content.appendingPathComponent(".DS_Store"))]]]
        try JSONSerialization.data(withJSONObject: data).write(to: evidence.appendingPathComponent("export.json"))
        try Data("OPTICAL_EXPORT_VERIFIED\n".utf8).write(to: evidence.appendingPathComponent("result.txt"))
        let expected = files + [FileEntry(path: ".DS_Store", directory: false, bytes: 11)]
        let receipt = try DiscExport.receipt(result("OPTICAL EXPORT VERIFIED\n"), evidence: evidence, selected: drive, expected: expected)
        let destination = root.appendingPathComponent("用户本地目标"); try JobStore.secureDirectory(destination)
        let id = UUID(), output = try DiscExport.copyToLocal(receipt: receipt, evidence: evidence, destination: destination, jobID: id)
        try c.expect("local export preserves zero Chinese nested Finder cache files", try Data(contentsOf: output.appendingPathComponent("other.txt")) == Data("hello".utf8) && FileManager.default.fileExists(atPath: output.appendingPathComponent("中文目录/empty.txt").path) && FileManager.default.fileExists(atPath: output.appendingPathComponent(".DS_Store").path))
        try c.rejects("export never overwrites an existing final folder", { _ = try DiscExport.copyToLocal(receipt: receipt, evidence: evidence, destination: destination, jobID: id) })
        try c.expect("existing exported content unchanged", try Data(contentsOf: output.appendingPathComponent("other.txt")) == Data("hello".utf8))
        try Data("changed".utf8).write(to: content.appendingPathComponent("other.txt"))
        try c.rejects("changed optical cache blocked before local commit", { _ = try DiscExport.copyToLocal(receipt: receipt, evidence: evidence, destination: destination, jobID: UUID()) })
        try c.rejects("receipt success alone cannot replace CLI success", { _ = try DiscExport.receipt(result("OPTICAL EXPORT VERIFIED\n", exit: 1), evidence: evidence, selected: drive, expected: expected) })
        try c.rejects("missing optical success marker rejected", { _ = try DiscExport.receipt(result(""), evidence: evidence, selected: drive, expected: expected) })
    }
    static func archivesAndRecovery(_ c: Checks, _ root: URL) throws {
        let store = try JobStore(directory: root.appendingPathComponent("persistent/Records"))
        let source = root.appendingPathComponent("archive-input"); try JobStore.secureDirectory(source)
        try Data("固定输入".utf8).write(to: source.appendingPathComponent("中文.txt"))
        let snapshot = try Staging.create(source: source)
        var record = JobRecord(phase: .needsInspection, message: "synthetic failed write")
        record.operation = .burn; record.snapshotPath = snapshot.directory.path
        let workspace = try JobStore.temporaryJob(record.id)
        record.workspacePath = workspace.path
        let manifest = Data("sourcePath=\(workspace.path)/disc-cache.iso\nblocks=\(workspace.path)/disc-cache.iso.blocks\n".utf8)
        try manifest.write(to: workspace.appendingPathComponent("session-next.iso.manifest"))
        try Data("0\n7\n".utf8).write(to: workspace.appendingPathComponent("disc-cache.iso.blocks"))
        let sparse = workspace.appendingPathComponent("disc-cache.iso")
        let fd = open(sparse.path, O_RDWR | O_CREAT | O_EXCL, 0o600)
        guard fd >= 0 else { throw CheckFailure(message: "sparse test open") }
        guard ftruncate(fd, 16 * 1024 * 1024) == 0, pwrite(fd, [UInt8(7)], 1, 0) == 1 else { close(fd); throw CheckFailure(message: "sparse test write") }
        close(fd)
        try Data("known CLI output".utf8).write(to: workspace.appendingPathComponent("stdout.log"))
        try store.save(record)
        let archive = try store.archive(record, workspace: workspace); record.archivePath = archive.path; try store.save(record)
        let (_, index) = try store.validatedArchive(record)
        try c.expect("archive preserves every dependency and snapshot hash", index.entries.contains { $0.path == "workspace/disc-cache.iso.blocks" } && index.entries.contains { $0.path == "snapshot/files/中文.txt" })
        let sparseInfo = try FileManager.default.attributesOfItem(atPath: archive.appendingPathComponent("workspace/disc-cache.iso").path)
        try c.expect("archive retains logical sparse cache size", (sparseInfo[.size] as? NSNumber)?.intValue == 16 * 1024 * 1024)
        var archivedStat = stat(), sourceStat = stat()
        lstat(archive.appendingPathComponent("workspace/disc-cache.iso").path, &archivedStat); lstat(sparse.path, &sourceStat)
        try c.expect("cache copy preserves allocation bound on this filesystem", archivedStat.st_blocks <= sourceStat.st_blocks + 8)
        print("Sparse storage observed: \(sourceStat.st_blocks * 512 < sourceStat.st_size); COPYFILE_DATA_SPARSE uses documented dense fallback when unsupported")
        try c.expect("archive leaves absolute manifest references verbatim", Data(contentsOf: archive.appendingPathComponent("workspace/session-next.iso.manifest")) == manifest)
        try FileManager.default.removeItem(at: workspace)
        chmod(snapshot.directory.path, 0o700); try FileManager.default.removeItem(at: snapshot.directory.deletingLastPathComponent())
        try store.restore(record); try Staging.validate(snapshot)
        try c.expect("restore rebuilds original cache and block-map paths", Data(contentsOf: workspace.appendingPathComponent("session-next.iso.manifest")) == manifest && FileManager.default.fileExists(atPath: workspace.appendingPathComponent("disc-cache.iso.blocks").path))
        var restored = stat(); lstat(snapshot.directory.appendingPathComponent("中文.txt").path, &restored)
        try c.expect("restored staging remains read-only", restored.st_mode & 0o222 == 0)
        try Data("conflict".utf8).write(to: workspace.appendingPathComponent("stdout.log"))
        try c.rejects("restore does not overwrite different existing files", { try store.restore(record) })
        try c.expect("restore conflict leaves existing bytes intact", Data(contentsOf: workspace.appendingPathComponent("stdout.log")) == Data("conflict".utf8))
        let archivedLog = archive.appendingPathComponent("workspace/stdout.log")
        chmod(archivedLog.path, 0o600); try Data("tamper".utf8).write(to: archivedLog)
        try c.rejects("corrupt archived payload blocks restore", { try store.restore(record) })
        try Data("known CLI output".utf8).write(to: archivedLog); chmod(archivedLog.path, 0o400)
        let archiveJSON = archive.appendingPathComponent("archive.json"), originalJSON = try Data(contentsOf: archiveJSON)
        var changed = try JSONSerialization.jsonObject(with: originalJSON) as! [String: Any]
        var roots = changed["roots"] as! [String: String]; roots["snapshot"] = "/private/tmp/burnlight-input-ABC123"; changed["roots"] = roots
        try JSONSerialization.data(withJSONObject: changed).write(to: archiveJSON)
        try c.rejects("archive cannot redirect another private snapshot", { _ = try store.validatedArchive(record) })
        try originalJSON.write(to: archiveJSON)
        let loaded = try JobStore(directory: store.directory).load()
        try c.expect("history survives a new store instance", loaded.records.count == 1 && loaded.records[0].archivePath == archive.path)
        try Data("broken".utf8).write(to: store.directory.appendingPathComponent(UUID().uuidString + ".json"))
        let isolated = try store.load()
        try c.expect("malformed history isolated with visible warnings", isolated.records.count == 1 && isolated.warnings.count == 1)
        let recoveryRoot = root.appendingPathComponent("receipt-lock")
        let recoveryJob = try JobStore.temporaryJob(UUID()), command = recoveryJob.appendingPathComponent("command-\(UUID().uuidString)")
        try JobStore.secureDirectory(command)
        var lease: PhysicalLease? = try PhysicalLease(jobDirectory: command, root: recoveryRoot)
        try c.rejects("recovery cannot bypass a live GUI lease", { _ = try PhysicalLease.recoverKnownExit(root: recoveryRoot, quiescent: { true }) })
        lease = nil
        try c.rejects("missing exit receipt never unlocks worker", { _ = try PhysicalLease.recoverKnownExit(root: recoveryRoot, quiescent: { true }) })
        let receipt = command.appendingPathComponent("exit.code")
        try Data("999\n".utf8).write(to: receipt)
        try c.rejects("invalid exit receipt never unlocks worker", { _ = try PhysicalLease.recoverKnownExit(root: recoveryRoot, quiescent: { true }) })
        try Data("7\n".utf8).write(to: receipt)
        try c.rejects("live worker or abnormal daemon keeps receipt locked", { _ = try PhysicalLease.recoverKnownExit(root: recoveryRoot, quiescent: { false }) })
        try c.expect("refused recovery retains active evidence", FileManager.default.fileExists(atPath: recoveryRoot.appendingPathComponent("active.json").path))
        let proof = try PhysicalLease.recoverKnownExit(root: recoveryRoot, quiescent: { true })
        try c.expect("known exit recovery preserves actual failure code", proof.exitCode == 7 && proof.directory.path == command.resolvingSymlinksInPath().path && !FileManager.default.fileExists(atPath: recoveryRoot.appendingPathComponent("active.json").path))
        try Data("{\"operation\":\"burn\"}".utf8).write(to: command.appendingPathComponent("request.json"))
        let run = recoveryJob.appendingPathComponent("optical/run-ABC123"); try JobStore.secureDirectory(run)
        let image = run.appendingPathComponent("session-next.iso"); try Data("original".utf8).write(to: image)
        let recovered = try proof.record()
        try c.expect("known exit recovers only original write image conservatively", recovered.phase == .needsInspection && recovered.requiresInspection == true && recovered.imagePath.map { URL(fileURLWithPath: $0).resolvingSymlinksInPath().path } == image.resolvingSymlinksInPath().path)
        let otherRun = recoveryJob.appendingPathComponent("optical/run-DEF456"); try JobStore.secureDirectory(otherRun)
        try c.expect("ambiguous original image is never guessed", try proof.record().imagePath == nil)
        try c.rejects("recovered receipt cannot alter unrelated history", { _ = try proof.record(existing: JobRecord()) })
        _ = lease
    }
    @MainActor
    static func models(_ c: Checks, _ root: URL, _ snapshot: InputSnapshot) async throws {
        let conflictMock = MockRunner(), conflictModel = AppModel(runner: conflictMock, store: try JobStore(directory: root.appendingPathComponent("conflict-records")))
        await conflictModel.discover(); await conflictModel.inspect(); conflictModel.snapshot = snapshot
        conflictModel.oldFiles = [snapshot.entries.first { !$0.directory }!]
        await conflictModel.preview()
        let unresolvedArguments = await conflictMock.arguments(.build)
        try c.expect("GUI blocks unresolved file conflict before CLI build", conflictModel.previewImage == nil && !conflictModel.canWrite && unresolvedArguments == nil)
        conflictModel.conflictPolicy = .skip; await conflictModel.preview()
        let skipArguments = await conflictMock.arguments(.build)
        try c.expect("GUI skip choice reaches preview argv", conflictModel.canWrite && skipArguments?.contains("skip") == true)
        conflictModel.conflictPolicy = .replace
        try c.expect("changed conflict choice invalidates verified preview", !conflictModel.canWrite)
        await conflictModel.preview(); await conflictMock.configure(fail: true); await conflictModel.write()
        let burnArguments = await conflictMock.arguments(.burn)
        try c.expect("GUI replacement reaches writer and retains failed write lock", burnArguments?.contains("replace") == true && conflictModel.needsInspection)
        try c.expect("history persists user conflict decision", conflictModel.history.first { $0.operation == .burn }?.conflictPolicy == .replace)
        let mock = MockRunner(), store = try JobStore(directory: root.appendingPathComponent("model-records"))
        let model = AppModel(runner: mock, store: store)
        await model.discover()
        try c.expect("single drive auto-selected", model.selected != nil)
        await model.inspect(); model.snapshot = snapshot
        let destination = root.appendingPathComponent("model-export"); try JobStore.secureDirectory(destination)
        model.needsInspection = true
        await model.exportDisc(to: destination, roots: ["old.txt"])
        try c.expect("GUI model optical export completes and keeps write failure lock", model.phase == .completed && model.exportedDirectory != nil && model.needsInspection)
        try c.expect("export history records local destination", try store.records().contains { $0.operation == .export && $0.exportPath == model.exportedDirectory?.path })
        let exportWrites = await mock.writes()
        try c.expect("model export requested zero writes", exportWrites == 0)
        model.needsInspection = false
        await model.preview()
        try c.expect("model requires inspected target and verified preview", model.canWrite)
        await mock.configure(fail: true)
        await model.write()
        try c.expect("write failure enters inspection and blocks burn", model.phase == .needsInspection && model.needsInspection && !model.canWrite)
        await model.write()
        let writesAfterFailure = await mock.writes()
        try c.expect("no automatic retry or init fallback", writesAfterFailure == 1)
        let failed = try store.records().first { $0.operation?.writes == true }!
        try c.expect("failed job persists original image and snapshot", failed.imagePath != nil && failed.snapshotPath == snapshot.directory.path)
        await model.inspect(); await mock.configure()
        await model.readback(URL(fileURLWithPath: failed.imagePath!))
        try c.expect("original image readback resolves matching failure only", model.phase == .completed && !model.needsInspection && !model.canWrite)
        let writesAfterReadback = await mock.writes()
        try c.expect("readback did not create second write request", writesAfterReadback == 1)
        let unknownMock = MockRunner(), unknownStore = try JobStore(directory: root.appendingPathComponent("unknown-records"))
        let unknown = AppModel(runner: unknownMock, store: unknownStore)
        await unknown.discover(); await unknown.inspect(); unknown.snapshot = snapshot; await unknown.preview(); await unknownMock.configure(unknown: true); await unknown.write()
        try c.expect("worker unknown locks subsequent physical tasks", unknown.workerUnknown && unknown.phase == .unknown && !unknown.canWrite)
        let initialMock = MockRunner(), initial = AppModel(runner: initialMock, store: try JobStore(directory: root.appendingPathComponent("init-records")))
        await initialMock.configure(blank: true); await initial.discover(); await initial.inspect(); initial.snapshot = snapshot; await initial.preview(); await initial.write()
        try c.expect("blank flow uses explicit init and full mocked readback", initial.phase == .completed && initial.disc?.sessions == 1)
        let cancelledMock = MockRunner(), cancelled = AppModel(runner: cancelledMock, store: try JobStore(directory: root.appendingPathComponent("cancel-records")))
        await cancelled.discover(); await cancelled.inspect(); cancelled.snapshot = snapshot; await cancelled.preview(); await cancelledMock.configure(cancelled: true); await cancelled.write()
        try c.expect("known authorization cancellation stays before write", cancelled.phase == .failed && !cancelled.workerUnknown && !cancelled.needsInspection)
        let multi = AppModel(runner: mock, store: store)
        multi.drives = try TextContract.drives(result(driveText + driveText.replacingOccurrences(of: "1,0,0", with: "1,2,0").replacingOccurrences(of: "4295022013", with: "99")))
        try c.expect("multiple devices require explicit selection", multi.selected == nil && !multi.canWrite)
        model.changeDrive(); try c.expect("device change invalidates old ready state", model.disc == nil && model.previewImage == nil && model.oldFiles.isEmpty)
        let file = snapshot.directory.appendingPathComponent(snapshot.entries.first { !$0.directory }!.path)
        chmod(file.path, 0o600); try Data("tampered".utf8).write(to: file)
        try c.rejects("tampered staged payload blocks before write", { try Staging.validate(snapshot) })
    }
}
