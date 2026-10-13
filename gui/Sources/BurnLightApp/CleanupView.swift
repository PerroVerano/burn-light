import SwiftUI
import BurnLightCore

struct CleanupView: View {
    @ObservedObject var model: AppModel
    @Environment(\.dismiss) private var dismiss
    @State private var confirm = false
    var body: some View {
        VStack(alignment: .leading, spacing: 14) {
            Text("清理本地存储").font(.title2.bold())
            Picker("清理范围", selection: $model.cleanupMode) {
                Text("临时缓存（保留历史）").tag(CleanupMode.temporary)
                Text("历史任务材料").tag(CleanupMode.history)
            }.pickerStyle(.segmented).disabled(model.cleanupWorking)
                .onChange(of: model.cleanupMode) { _, mode in
                    model.cleanupRetentionDays = mode == .temporary ? 0 : 30
                    model.invalidateCleanupPlan()
                }
            Text(model.cleanupMode == .temporary ? "清理已有完整归档的临时日志、镜像、读盘缓存和输入快照；归档保留，可用于恢复。" : "清理已结束任务的归档与临时副本，保留小型任务摘要；清理后无法再导出该任务诊断。")
                .font(.callout).foregroundStyle(.secondary)
            HStack {
                Picker("保留期", selection: $model.cleanupRetentionDays) {
                    Text("全部已结束任务").tag(0)
                    Text("7 天以前").tag(7)
                    Text("30 天以前").tag(30)
                }.frame(width: 245).disabled(model.cleanupWorking)
                    .onChange(of: model.cleanupRetentionDays) { _, _ in model.invalidateCleanupPlan() }
                Spacer()
                Button("重新扫描") { Task { await model.scanCleanup() } }.disabled(model.cleanupWorking)
            }
            if let plan = model.cleanupPlan {
                HStack {
                    Text("可清理约 \(formatBytes(plan.allocatedBytes)) · \(plan.items.count) 个任务")
                    Spacer()
                    Button("全选") { model.cleanupSelectedIDs = Set(plan.items.map { $0.id }) }
                    Button("全不选") { model.cleanupSelectedIDs = [] }
                }.font(.subheadline).disabled(model.cleanupWorking)
                List(plan.items) { item in
                    Toggle(isOn: Binding(get: { model.cleanupSelectedIDs.contains(item.id) }, set: { value in
                        if value { model.cleanupSelectedIDs.insert(item.id) } else { model.cleanupSelectedIDs.remove(item.id) }
                    })) {
                        HStack {
                            VStack(alignment: .leading, spacing: 3) {
                                Text(item.label)
                                Text("\(item.created.formatted(date: .numeric, time: .shortened)) · \(item.id.uuidString.prefix(8))")
                                    .font(.caption).foregroundStyle(.secondary)
                            }
                            Spacer()
                            Text(formatBytes(item.allocatedBytes)).monospacedDigit()
                        }
                    }.help(item.paths.joined(separator: "\n")).disabled(model.cleanupWorking)
                }.frame(minHeight: 180)
                Text("\(plan.protectedJobs) 个近期、当前或待检查任务保留。按实际占用估算，稀疏镜像不按标称大小统计；系统快照可能延后空间回收。")
                    .font(.caption).foregroundStyle(.secondary)
                if !plan.warnings.isEmpty {
                    DisclosureGroup("\(plan.warnings.count) 个任务材料未纳入清理") {
                        ScrollView { Text(plan.warnings.joined(separator: "\n")).font(.caption).textSelection(.enabled) }.frame(maxHeight: 100)
                    }
                }
            } else { Spacer() }
            HStack {
                if model.cleanupWorking { ProgressView().controlSize(.small) }
                Text(model.cleanupMessage).font(.callout).textSelection(.enabled)
            }
            Text("不清理原始输入、已导出文件、光盘内容或应用安装包；活动/未知任务和失败刻录材料保留。")
                .font(.caption).foregroundStyle(.secondary)
            HStack {
                Button("关闭") { dismiss() }.keyboardShortcut(.cancelAction).disabled(model.cleanupWorking)
                Spacer()
                Text("已选 \(model.cleanupSelectedIDs.count) 项 · 约 \(formatBytes(model.selectedCleanupBytes))").font(.caption)
                Button("清理所选…", role: .destructive) { confirm = true }
                    .disabled(model.cleanupWorking || model.cleanupPlan == nil || model.cleanupSelectedIDs.isEmpty)
            }
        }.padding(24).frame(width: 660, height: 640)
            .interactiveDismissDisabled(model.cleanupWorking)
            .task { await model.scanCleanup() }
            .alert("永久删除所选任务材料？", isPresented: $confirm) {
                Button("取消", role: .cancel) {}
                Button("永久删除", role: .destructive) { Task { await model.cleanStorage() } }
            } message: {
                Text("所选 \(model.cleanupSelectedIDs.count) 项，预计释放 \(formatBytes(model.selectedCleanupBytes))。\(model.cleanupMode == .temporary ? "完整历史归档会保留。" : "归档也会删除，仅保留任务摘要。")此操作无法撤销。")
            }
    }
}
