import SwiftUI
import AppKit

final class AppDelegate: NSObject, NSApplicationDelegate {
    weak var model: AppModel?
    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.regular)
        NSApp.activate(ignoringOtherApps: true)
    }
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        if model?.busy == true || model?.physicalActive == true || model?.workerUnknown == true {
            let alert = NSAlert(); alert.messageText = "任务仍在运行或状态未知"
            alert.informativeText = "请等待 CLI 退出收据与读回结果。关闭窗口不会取消任务；GUI 不会强制终止 writer 或认证进程。"
            alert.runModal(); return .terminateCancel
        }
        return .terminateNow
    }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { false }
    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        if !flag { sender.windows.first(where: { $0.canBecomeMain })?.makeKeyAndOrderFront(nil) }
        return true
    }
}

@main
struct BurnLightApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate
    @StateObject private var model = AppModel()
    var body: some Scene {
        WindowGroup("BurnLight") {
            ContentView(model: model).onAppear { delegate.model = model }
                .frame(minWidth: 1040, minHeight: 720)
        }
        .defaultSize(width: 1180, height: 800)
        .commands {
            CommandGroup(replacing: .newItem) {}
            CommandGroup(replacing: .help) {
                Button("BurnLight 使用说明") {
                    if let url = Bundle.main.url(forResource: "Usage", withExtension: "md") { NSWorkspace.shared.open(url) }
                }
                Button("第三方许可与来源") {
                    if let url = Bundle.main.resourceURL { NSWorkspace.shared.open(url.appendingPathComponent("ThirdPartyNotices")) }
                }
            }
        }
    }
}
