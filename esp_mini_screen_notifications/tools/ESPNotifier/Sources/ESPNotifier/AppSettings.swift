import Foundation
import Combine

struct MonitoredApp: Codable, Identifiable, Equatable {
    var bundleId: String
    var displayName: String
    var customDisplayName: String
    var accentHex: String
    var foregroundHex: String
    var durationSeconds: Int
    var telegramSplitTitle: Bool
    var id: String { bundleId }

    init(
        bundleId: String,
        displayName: String,
        customDisplayName: String = "",
        accentHex: String? = nil,
        foregroundHex: String = "#FFFFFF",
        durationSeconds: Int = 3,
        telegramSplitTitle: Bool? = nil
    ) {
        self.bundleId = bundleId
        self.displayName = displayName
        self.customDisplayName = customDisplayName
        self.accentHex = accentHex ?? MonitoredApp.defaultAccentHex(for: bundleId, displayName: displayName)
        self.foregroundHex = foregroundHex
        self.durationSeconds = min(30, max(1, durationSeconds))
        self.telegramSplitTitle = telegramSplitTitle ?? bundleId.lowercased().contains("telegram")
    }

    enum CodingKeys: String, CodingKey {
        case bundleId, displayName, customDisplayName, accentHex, foregroundHex, durationSeconds, telegramSplitTitle
    }

    init(from decoder: Decoder) throws {
        let container = try decoder.container(keyedBy: CodingKeys.self)
        bundleId = try container.decode(String.self, forKey: .bundleId)
        displayName = try container.decode(String.self, forKey: .displayName)
        customDisplayName = try container.decodeIfPresent(String.self, forKey: .customDisplayName) ?? ""
        accentHex = try container.decodeIfPresent(String.self, forKey: .accentHex)
            ?? MonitoredApp.defaultAccentHex(for: bundleId, displayName: displayName)
        foregroundHex = try container.decodeIfPresent(String.self, forKey: .foregroundHex) ?? "#FFFFFF"
        durationSeconds = min(30, max(1, try container.decodeIfPresent(Int.self, forKey: .durationSeconds) ?? 3))
        telegramSplitTitle = try container.decodeIfPresent(Bool.self, forKey: .telegramSplitTitle)
            ?? bundleId.lowercased().contains("telegram")
    }

    var effectiveDisplayName: String {
        customDisplayName.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
            ? displayName
            : customDisplayName.trimmingCharacters(in: .whitespacesAndNewlines)
    }

    static func defaultAccentHex(for bundleId: String, displayName: String) -> String {
        let value = (bundleId + " " + displayName).lowercased()
        if value.contains("telegram") { return "#2AABEE" }
        if value.contains("mail") { return "#FFB020" }
        if value.contains("message") { return "#34C759" }
        if value.contains("discord") { return "#5865F2" }
        if value.contains("github") { return "#8A8F98" }
        if value.contains("codex") { return "#10A37F" }
        return "#55AAFF"
    }
}

class AppSettings: ObservableObject {
    static let shared = AppSettings()

    @Published var deviceURL: String {
        didSet { UserDefaults.standard.set(deviceURL, forKey: "deviceURL") }
    }
    @Published var isEnabled: Bool {
        didSet { UserDefaults.standard.set(isEnabled, forKey: "isEnabled") }
    }
    @Published var pollInterval: Int {
        didSet { UserDefaults.standard.set(pollInterval, forKey: "pollInterval") }
    }
    @Published var monitoredApps: [MonitoredApp] {
        didSet {
            if let data = try? JSONEncoder().encode(monitoredApps) {
                UserDefaults.standard.set(data, forKey: "monitoredApps")
            }
        }
    }

    private init() {
        deviceURL    = UserDefaults.standard.string(forKey: "deviceURL") ?? ""
        isEnabled    = UserDefaults.standard.object(forKey: "isEnabled") as? Bool ?? true
        pollInterval = UserDefaults.standard.object(forKey: "pollInterval") as? Int ?? 3

        if let data = UserDefaults.standard.data(forKey: "monitoredApps"),
           let apps = try? JSONDecoder().decode([MonitoredApp].self, from: data) {
            monitoredApps = apps
        } else {
            monitoredApps = []
        }
    }

    var monitoredBundleIds: Set<String> {
        Set(monitoredApps.map(\.bundleId))
    }

    func isMonitored(_ bundleId: String) -> Bool {
        monitoredBundleIds.contains(bundleId)
    }

    func setMonitored(_ bundleId: String, displayName: String, enabled: Bool) {
        if enabled {
            if !monitoredApps.contains(where: { $0.bundleId == bundleId }) {
                monitoredApps.append(MonitoredApp(bundleId: bundleId, displayName: displayName))
            }
        } else {
            monitoredApps.removeAll { $0.bundleId == bundleId }
        }
    }

    func monitoredApp(for bundleId: String) -> MonitoredApp? {
        monitoredApps.first { $0.bundleId == bundleId }
    }

    func updateMonitoredApp(_ updated: MonitoredApp) {
        guard let index = monitoredApps.firstIndex(where: { $0.bundleId == updated.bundleId }) else { return }
        monitoredApps[index] = updated
    }
}
