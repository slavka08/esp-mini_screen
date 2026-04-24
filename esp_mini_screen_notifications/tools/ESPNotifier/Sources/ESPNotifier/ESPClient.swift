import Foundation

struct ESPClient {
    enum ESPError: Error {
        case badURL
        case httpError(Int)
    }

    static func sendNotification(
        deviceURL: String,
        bundleId: String,
        app: String,
        sender: String,
        title: String,
        subtitle: String,
        body: String,
        accentHex: String,
        foregroundHex: String,
        durationSeconds: Int,
        deliveredDate: Double?
    ) async throws {
        let endpoint = deviceURL.trimmingCharacters(in: CharacterSet(charactersIn: "/")) + "/notify"
        guard let url = URL(string: endpoint) else { throw ESPError.badURL }

        let updatedAt: String
        if let ts = deliveredDate {
            // CoreData epoch offset
            let date = Date(timeIntervalSinceReferenceDate: ts)
            let formatter = DateFormatter()
            formatter.dateFormat = "HH:mm"
            updatedAt = formatter.string(from: date)
        } else {
            let formatter = DateFormatter()
            formatter.dateFormat = "HH:mm"
            updatedAt = formatter.string(from: Date())
        }

        let appName = truncate(app, to: 24)
        let safeSender = truncate(sender, to: 32)
        let safeTitle = truncate(title, to: 64)
        let safeSubtitle = truncate(subtitle, to: 64)
        let safeBody = truncate(body, to: 224)
        let durationMs = min(30, max(1, durationSeconds)) * 1000
        let payload: [String: Any] = [
            "version": 2,
            "source": [
                "appName": appName,
                "bundleId": bundleId,
                "sender": safeSender,
            ],
            "content": [
                "title": safeTitle,
                "subtitle": safeSubtitle,
                "body": safeBody,
                "time": truncate(updatedAt, to: 32),
            ],
            "style": [
                "accent": normalizeHex(accentHex, fallback: "#55AAFF"),
                "foreground": normalizeHex(foregroundHex, fallback: "#FFFFFF"),
                "durationMs": durationMs,
            ],
            "meta": [
                "category": "message",
            ],
        ]
        log(
            "POST \(url.absoluteString) payload app=\"\(appName)\" bundleId=\"\(bundleId)\" sender=\"\(compactForLog(safeSender, limit: 80))\" title=\"\(compactForLog(safeTitle, limit: 120))\" subtitle=\"\(compactForLog(safeSubtitle, limit: 120))\" body=\"\(compactForLog(safeBody, limit: 200))\" accent=\"\(normalizeHex(accentHex, fallback: "#55AAFF"))\" durationMs=\(durationMs) updatedAt=\"\(updatedAt)\""
        )

        var request = URLRequest(url: url, timeoutInterval: 6)
        request.httpMethod = "POST"
        request.setValue("application/json; charset=UTF-8", forHTTPHeaderField: "Content-Type")
        request.httpBody = try JSONSerialization.data(withJSONObject: payload, options: [])

        let (data, response) = try await URLSession.shared.data(for: request)
        if let http = response as? HTTPURLResponse {
            let responsePreview = compactForLog(String(data: data, encoding: .utf8) ?? "", limit: 200)
            if http.statusCode >= 400 {
                log("HTTP \(http.statusCode) for /notify, response=\"\(responsePreview)\"")
                throw ESPError.httpError(http.statusCode)
            }
            log("HTTP \(http.statusCode) for /notify, response=\"\(responsePreview)\"")
        }
    }

    static func testConnection(deviceURL: String) async -> Bool {
        let endpoint = deviceURL.trimmingCharacters(in: CharacterSet(charactersIn: "/")) + "/state"
        guard let url = URL(string: endpoint) else { return false }
        do {
            let (_, response) = try await URLSession.shared.data(from: url)
            return (response as? HTTPURLResponse)?.statusCode == 200
        } catch {
            return false
        }
    }

    // MARK: - Helpers

    private static func truncate(_ s: String, to max: Int) -> String {
        guard s.count > max else { return s }
        return String(s.prefix(max - 1)) + "…"
    }

    private static func compactForLog(_ s: String, limit: Int) -> String {
        let normalized = s
            .replacingOccurrences(of: "\r", with: "\\r")
            .replacingOccurrences(of: "\n", with: "\\n")
        guard normalized.count > limit else { return normalized }
        return String(normalized.prefix(limit)) + "…"
    }

    private static func log(_ msg: String) {
        let ts = DateFormatter.localizedString(from: Date(), dateStyle: .none, timeStyle: .medium)
        print("[\(ts)] [ESPClient] \(msg)")
    }

    private static func normalizeHex(_ value: String, fallback: String) -> String {
        let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
        let pattern = #"^#[0-9A-Fa-f]{6}$"#
        return trimmed.range(of: pattern, options: .regularExpression) == nil ? fallback : trimmed.uppercased()
    }
}
