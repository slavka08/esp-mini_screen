import SwiftUI
import AppKit
import ServiceManagement

// MARK: - App list row

struct AppListRow: View {
    let record: AppRecord
    let isSelected: Bool
    let appConfig: MonitoredApp?
    let onToggle: (Bool) -> Void
    let onUpdate: (MonitoredApp) -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            HStack {
                Toggle("", isOn: Binding(get: { isSelected }, set: { onToggle($0) }))
                    .toggleStyle(.checkbox)
                    .labelsHidden()
                VStack(alignment: .leading, spacing: 1) {
                    Text(appConfig?.effectiveDisplayName ?? record.displayName)
                        .fontWeight(.medium)
                    Text(record.bundleId)
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
                Spacer()
                Text("\(record.count)")
                    .font(.caption)
                    .foregroundColor(.secondary)
                    .frame(width: 40, alignment: .trailing)
            }

            if isSelected, let appConfig {
                VStack(alignment: .leading, spacing: 8) {
                    TextField("Display name", text: Binding(
                        get: { appConfig.customDisplayName },
                        set: { value in
                            var updated = appConfig
                            updated.customDisplayName = value
                            onUpdate(updated)
                        }
                    ))
                    .textFieldStyle(.roundedBorder)

                    HStack(spacing: 18) {
                        CompactColorPicker(title: "Accent", selection: Binding(
                            get: { Color(hex: appConfig.accentHex) ?? .accentColor },
                            set: { value in
                                var updated = appConfig
                                updated.accentHex = value.hexString ?? appConfig.accentHex
                                onUpdate(updated)
                            }
                        ))
                        CompactColorPicker(title: "Text", selection: Binding(
                            get: { Color(hex: appConfig.foregroundHex) ?? .white },
                            set: { value in
                                var updated = appConfig
                                updated.foregroundHex = value.hexString ?? appConfig.foregroundHex
                                onUpdate(updated)
                            }
                        ))
                        Spacer()
                    }

                    HStack(alignment: .top) {
                        Text("Visible")
                            .frame(width: 52, alignment: .leading)
                        Text("\(appConfig.durationSeconds) seconds")
                            .frame(maxWidth: .infinity, alignment: .leading)
                        Stepper("", value: Binding(
                            get: { appConfig.durationSeconds },
                            set: { value in
                                var updated = appConfig
                                updated.durationSeconds = min(30, max(1, value))
                                onUpdate(updated)
                            }
                        ), in: 1...30)
                        .labelsHidden()
                    }

                    if record.bundleId.lowercased().contains("telegram") {
                        Toggle("Telegram: split sender and chat from title", isOn: Binding(
                            get: { appConfig.telegramSplitTitle },
                            set: { value in
                                var updated = appConfig
                                updated.telegramSplitTitle = value
                                onUpdate(updated)
                            }
                        ))
                    }
                }
                .padding(.leading, 22)
            }
        }
        .padding(.vertical, 2)
    }
}

struct CompactColorPicker: View {
    let title: String
    @Binding var selection: Color

    var body: some View {
        HStack(spacing: 8) {
            Text(title)
                .frame(width: 52, alignment: .leading)
            ColorPicker(title, selection: $selection)
                .labelsHidden()
                .fixedSize()
        }
    }
}

extension Color {
    init?(hex: String) {
        var value = hex.trimmingCharacters(in: .whitespacesAndNewlines)
        if value.hasPrefix("#") { value.removeFirst() }
        guard value.count == 6, let intValue = Int(value, radix: 16) else { return nil }
        self.init(
            red: Double((intValue >> 16) & 0xFF) / 255.0,
            green: Double((intValue >> 8) & 0xFF) / 255.0,
            blue: Double(intValue & 0xFF) / 255.0
        )
    }

    var hexString: String? {
        guard let color = NSColor(self).usingColorSpace(.sRGB) else { return nil }
        let r = Int(round(color.redComponent * 255))
        let g = Int(round(color.greenComponent * 255))
        let b = Int(round(color.blueComponent * 255))
        return String(format: "#%02X%02X%02X", r, g, b)
    }
}

// MARK: - Settings View

struct SettingsView: View {
    @ObservedObject private var settings = AppSettings.shared

    @State private var connectionStatus: ConnectionStatus = .unknown
    @State private var testNotifStatus: TestStatus = .idle

    enum TestStatus {
        case idle, sending, sent, failed
        var label: String {
            switch self {
            case .idle:    return ""
            case .sending: return "Sending…"
            case .sent:    return "✓ Sent"
            case .failed:  return "✗ Failed"
            }
        }
        var color: Color {
            switch self {
            case .sent:   return .green
            case .failed: return .red
            default:      return .secondary
            }
        }
    }
    @State private var availableApps: [AppRecord] = []
    @State private var isLoadingApps = false
    @State private var hasFullDiskAccess = NotificationDB.hasAccess()
    @State private var launchAtLogin = (SMAppService.mainApp.status == .enabled)

    enum ConnectionStatus {
        case unknown, checking, ok, failed
        var label: String {
            switch self {
            case .unknown:   return "–"
            case .checking:  return "Checking…"
            case .ok:        return "● Connected"
            case .failed:    return "○ Unreachable"
            }
        }
        var color: Color {
            switch self {
            case .ok:     return .green
            case .failed: return .red
            default:      return .secondary
            }
        }
    }

    var body: some View {
        ScrollView {
            Form {
            // MARK: Device
            Section("Device") {
                HStack {
                    TextField("http://192.168.x.x", text: $settings.deviceURL)
                        .textFieldStyle(.roundedBorder)
                    Button("Test") { testConnection() }
                        .disabled(settings.deviceURL.isEmpty)
                }
                HStack {
                    Text("Status:")
                        .foregroundColor(.secondary)
                    Text(connectionStatus.label)
                        .foregroundColor(connectionStatus.color)
                }
                HStack {
                    Button("Send Test Notification") { sendTestNotification() }
                        .disabled(settings.deviceURL.isEmpty || testNotifStatus == .sending)
                    if testNotifStatus != .idle {
                        Text(testNotifStatus.label)
                            .foregroundColor(testNotifStatus.color)
                            .font(.callout)
                    }
                }
            }

            // MARK: Monitored Apps
            Section {
                if !hasFullDiskAccess {
                    HStack {
                        Image(systemName: "exclamationmark.triangle.fill")
                            .foregroundColor(.orange)
                        Text("Full Disk Access required to load apps.")
                            .foregroundColor(.secondary)
                        Button("Open Settings") { openPrivacySettings() }
                    }
                } else if isLoadingApps {
                    HStack {
                        ProgressView().controlSize(.small)
                        Text("Loading apps…").foregroundColor(.secondary)
                    }
                } else if availableApps.isEmpty {
                    Text("No notification history found.")
                        .foregroundColor(.secondary)
                } else {
                    VStack(spacing: 0) {
                        ForEach(Array(availableApps.enumerated()), id: \.element.id) { index, app in
                            AppListRow(
                                record: app,
                                isSelected: settings.isMonitored(app.bundleId),
                                appConfig: settings.monitoredApp(for: app.bundleId)
                            ) { enabled in
                                settings.setMonitored(app.bundleId, displayName: app.displayName, enabled: enabled)
                            } onUpdate: { updated in
                                settings.updateMonitoredApp(updated)
                            }
                            .padding(.horizontal, 10)
                            .padding(.vertical, 8)

                            if index < availableApps.count - 1 {
                                Divider()
                                    .padding(.leading, 34)
                            }
                        }
                    }
                    .overlay(
                        RoundedRectangle(cornerRadius: 6)
                            .stroke(Color.secondary.opacity(0.18), lineWidth: 1)
                    )
                }
            } header: {
                HStack {
                    Text("Monitored Apps")
                    Spacer()
                    Button("Refresh") { loadApps() }
                        .buttonStyle(.plain)
                        .foregroundColor(.accentColor)
                        .disabled(!hasFullDiskAccess)
                }
            }

            // MARK: General
            Section("General") {
                HStack(alignment: .top) {
                    Text("Poll interval:")
                    Text("\(settings.pollInterval) seconds")
                        .frame(maxWidth: .infinity, alignment: .leading)
                    Stepper("", value: $settings.pollInterval, in: 1...30)
                        .labelsHidden()
                        .onChange(of: settings.pollInterval) { _ in
                            NotificationWatcher.shared.restart()
                        }
                }
                Toggle("Launch at Login", isOn: $launchAtLogin)
                    .onChange(of: launchAtLogin) { enabled in
                        setLaunchAtLogin(enabled)
                    }
            }

            // MARK: Permissions
            Section("Permissions") {
                HStack {
                    Image(systemName: hasFullDiskAccess ? "checkmark.circle.fill" : "xmark.circle.fill")
                        .foregroundColor(hasFullDiskAccess ? .green : .red)
                    Text("Full Disk Access")
                    Spacer()
                    if hasFullDiskAccess {
                        Button("Recheck") { recheckAccess() }
                            .buttonStyle(.plain)
                            .foregroundColor(.accentColor)
                    } else {
                        Button("Recheck") { recheckAccess() }
                            .buttonStyle(.plain)
                            .foregroundColor(.accentColor)
                        Button("Open System Settings") { openPrivacySettings() }
                    }
                }
                if !hasFullDiskAccess {
                    HStack(alignment: .top, spacing: 6) {
                        Image(systemName: "info.circle")
                            .foregroundColor(.secondary)
                            .font(.caption)
                            .padding(.top, 1)
                        VStack(alignment: .leading, spacing: 4) {
                            Text("macOS applies Full Disk Access only after the app restarts.")
                                .font(.caption)
                                .foregroundColor(.secondary)
                            Text("Checking: \(NotificationDB.dbPath.path)")
                                .font(.caption)
                                .foregroundColor(.secondary)
                                .textSelection(.enabled)
                            Button("Quit & Reopen") { relaunch() }
                                .controlSize(.small)
                        }
                    }
                }
            }
            }
            .formStyle(.grouped)
            .padding()
        }
        .frame(width: 480)
        .onAppear {
            recheckAccess()
        }
        .onReceive(NotificationCenter.default.publisher(for: NSApplication.didBecomeActiveNotification)) { _ in
            recheckAccess()
        }
    }

    // MARK: - Actions

    private func sendTestNotification() {
        testNotifStatus = .sending
        Task {
            do {
                try await ESPClient.sendNotification(
                    deviceURL: settings.deviceURL,
                    bundleId: "com.esp-mini-screen.notifier",
                    app: "ESPNotifier",
                    sender: "Test",
                    title: "Test notification",
                    subtitle: "Settings",
                    body: "ESPNotifier is working correctly!",
                    accentHex: "#55AAFF",
                    foregroundHex: "#FFFFFF",
                    durationSeconds: 3,
                    deliveredDate: nil
                )
                await MainActor.run {
                    testNotifStatus = .sent
                    // Reset after 3 seconds
                    DispatchQueue.main.asyncAfter(deadline: .now() + 3) {
                        testNotifStatus = .idle
                    }
                }
            } catch {
                await MainActor.run {
                    testNotifStatus = .failed
                    DispatchQueue.main.asyncAfter(deadline: .now() + 3) {
                        testNotifStatus = .idle
                    }
                }
            }
        }
    }

    private func testConnection() {
        connectionStatus = .checking
        Task {
            let ok = await ESPClient.testConnection(deviceURL: settings.deviceURL)
            await MainActor.run { connectionStatus = ok ? .ok : .failed }
        }
    }

    private func loadApps() {
        guard !isLoadingApps else { return }
        isLoadingApps = true
        Task.detached(priority: .userInitiated) {
            let apps = NotificationDB.getAllApps()
            await MainActor.run {
                self.availableApps = apps
                self.isLoadingApps = false
            }
        }
    }

    private func relaunch() {
        let task = Process()
        task.launchPath = "/usr/bin/open"
        task.arguments = [Bundle.main.bundlePath]
        try? task.run()
        NSApp.terminate(nil)
    }

    private func recheckAccess() {
        let hadAccess = hasFullDiskAccess
        hasFullDiskAccess = NotificationDB.hasAccess()
        if hasFullDiskAccess && (!hadAccess || availableApps.isEmpty) {
            loadApps()
        }
    }

    private func openPrivacySettings() {
        NSWorkspace.shared.open(
            URL(string: "x-apple.systempreferences:com.apple.preference.security?Privacy_AllFiles")!
        )
    }

    private func setLaunchAtLogin(_ enabled: Bool) {
        do {
            if enabled {
                try SMAppService.mainApp.register()
            } else {
                try SMAppService.mainApp.unregister()
            }
        } catch {
            // Silently fail — SMAppService can throw if bundle is unsigned
        }
    }
}
