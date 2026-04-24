import Foundation

class NotificationWatcher {
    static let shared = NotificationWatcher()

    private var timer: DispatchSourceTimer?
    private let queue = DispatchQueue(label: "com.esp-mini-screen.watcher", qos: .background)
    private var lastPollStatus = ""
    private var lastPollStatusLoggedAt = Date.distantPast

    private var lastRecId: Int {
        get { UserDefaults.standard.integer(forKey: "lastRecId") }
        set { UserDefaults.standard.set(newValue, forKey: "lastRecId") }
    }
    private var isBootstrapped: Bool {
        get { UserDefaults.standard.bool(forKey: "isBootstrapped") }
        set { UserDefaults.standard.set(newValue, forKey: "isBootstrapped") }
    }

    private init() {}

    func start() {
        guard timer == nil else { return }
        let t = DispatchSource.makeTimerSource(queue: queue)
        let interval = Double(AppSettings.shared.pollInterval)
        t.schedule(deadline: .now() + 1, repeating: interval)
        t.setEventHandler { [weak self] in self?.poll() }
        t.resume()
        timer = t
        log("Started polling every \(AppSettings.shared.pollInterval)s")
    }

    func stop() {
        timer?.cancel()
        timer = nil
        log("Stopped polling")
    }

    func restart() {
        stop()
        start()
    }

    // MARK: - Poll

    private func poll() {
        let settings = AppSettings.shared
        guard settings.isEnabled else {
            logPollStatus("Skip: forwarding disabled")
            return
        }
        guard !settings.deviceURL.isEmpty else {
            logPollStatus("Skip: device URL is empty")
            return
        }
        guard !settings.monitoredBundleIds.isEmpty else {
            logPollStatus("Skip: no monitored apps selected")
            return
        }

        // First-run bootstrap: skip all existing notifications
        if !isBootstrapped {
            let maxId = NotificationDB.getCurrentMaxRecId()
            lastRecId = maxId
            isBootstrapped = true
            log("Bootstrap: skipping \(maxId) existing notifications")
            return
        }

        let maxId = NotificationDB.getCurrentMaxRecId()
        if maxId > 0 && lastRecId > maxId {
            let rewindTo = Swift.max(0, maxId - 25)
            log(
                "DB rec_id moved backwards: lastRecId=\(lastRecId), dbMaxRecId=\(maxId). Rewinding cursor to \(rewindTo)"
            )
            lastRecId = rewindTo
        }

        let records = NotificationDB.getNewRecords(
            since: lastRecId,
            bundleIds: settings.monitoredBundleIds
        )
        if records.isEmpty {
            logPollStatus(
                "No matching records since lastRecId=\(lastRecId); dbMaxRecId=\(maxId); monitored=\(settings.monitoredBundleIds.sorted().joined(separator: ","))"
            )
            return
        }
        log("Found \(records.count) matching record(s) since lastRecId=\(lastRecId)")

        for record in records {
            let content = NotificationParser.parse(record.data)
            let appConfig = settings.monitoredApp(for: record.bundleId)
            let displayName = appConfig?.effectiveDisplayName ?? NotificationDB.resolveDisplayName(for: record.bundleId)
            let accentHex = appConfig?.accentHex ?? MonitoredApp.defaultAccentHex(for: record.bundleId, displayName: displayName)
            let foregroundHex = appConfig?.foregroundHex ?? "#FFFFFF"
            let durationSeconds = appConfig?.durationSeconds ?? 3
            let useSplitTitle = appConfig?.telegramSplitTitle ?? false
            let title = useSplitTitle ? content.title : (content.rawTitle.isEmpty ? content.title : content.rawTitle)
            let subtitle = useSplitTitle ? content.subtitle : ""
            let delivered = record.deliveredDate.map { String(format: "%.3f", $0) } ?? "nil"
            log(
                "DB rec_id=\(record.recId) bundle=\(record.bundleId) app=\(displayName) bytes=\(record.data.count) delivered=\(delivered) parsed sender=\"\(compactForLog(content.sender, limit: 80))\" title=\"\(compactForLog(title, limit: 120))\" subtitle=\"\(compactForLog(subtitle, limit: 120))\" body=\"\(compactForLog(content.body, limit: 200))\""
            )
            Task {
                do {
                    try await ESPClient.sendNotification(
                        deviceURL: settings.deviceURL,
                        bundleId: record.bundleId,
                        app: displayName,
                        sender: content.sender,
                        title: title,
                        subtitle: subtitle,
                        body: content.body,
                        accentHex: accentHex,
                        foregroundHex: foregroundHex,
                        durationSeconds: durationSeconds,
                        deliveredDate: record.deliveredDate
                    )
                    log("Sent [\(displayName)] \(content.title.prefix(40))")
                } catch {
                    log("WARN: send failed for [\(displayName)]: \(error)")
                }
            }

            // Advance lastRecId even on send failure to avoid re-sending stale notifications
            lastRecId = record.recId
        }
    }

    private func log(_ msg: String) {
        let ts = DateFormatter.localizedString(from: Date(), dateStyle: .none, timeStyle: .medium)
        print("[\(ts)] [Watcher] \(msg)")
    }

    private func logPollStatus(_ msg: String) {
        let now = Date()
        guard msg != lastPollStatus || now.timeIntervalSince(lastPollStatusLoggedAt) >= 30 else { return }
        lastPollStatus = msg
        lastPollStatusLoggedAt = now
        log(msg)
    }

    private func compactForLog(_ s: String, limit: Int) -> String {
        let normalized = s
            .replacingOccurrences(of: "\r", with: "\\r")
            .replacingOccurrences(of: "\n", with: "\\n")
        guard normalized.count > limit else { return normalized }
        return String(normalized.prefix(limit)) + "…"
    }
}
