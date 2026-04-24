import Foundation

struct NotificationContent {
    var title: String = ""
    var rawTitle: String = ""
    var body: String = ""
    var sender: String = ""
    var subtitle: String = ""
}

enum NotificationParser {
    private static let titleKeys = ["titl", "title", "head"]
    private static let bodyKeys = ["body", "message", "text"]
    private static let senderKeys = ["subt", "subtitle", "sender"]

    static func parse(_ data: Data) -> NotificationContent {
        // Strategy 1: NSKeyedUnarchiver (handles the majority of notifications)
        if let content = parseViaKeyedUnarchiver(data) {
            return content
        }
        // Strategy 2: Plain PropertyList (some older/simpler notifications)
        if let content = parseViaPropertyList(data) {
            return content
        }
        // Strategy 3: Raw string extraction fallback
        return parseViaRawBytes(data)
    }

    static func debugDump(_ data: Data, maxEntries: Int = 120) -> [String] {
        var lines: [String] = [
            "payload bytes=\(data.count) prefix=\(hexPrefix(data, limit: 24))"
        ]
        var seen = Set<String>()
        var entryCount = 0

        func add(_ line: String) {
            guard entryCount < maxEntries, seen.insert(line).inserted else { return }
            lines.append(line)
            entryCount += 1
        }

        if let plist = try? PropertyListSerialization.propertyList(from: data, format: nil) as? [String: Any],
           plist["$archiver"] as? String == "NSKeyedArchiver",
           let objects = plist["$objects"] as? [Any] {
            lines.append("archive objects=\(objects.count)")
            if let top = plist["$top"] {
                collectDebugValues(
                    decodeArchivedValue(top, objects: objects),
                    path: "$top",
                    add: add
                )
            }
            for (index, obj) in objects.enumerated() where entryCount < maxEntries {
                collectDebugValues(
                    decodeArchivedValue(obj, objects: objects),
                    path: "$objects[\(index)]",
                    add: add
                )
            }
        } else if let plist = try? PropertyListSerialization.propertyList(from: data, format: nil) {
            collectDebugValues(plist, path: "$plist", add: add)
        }

        let raw = rawStrings(in: data)
            .filter { !$0.isEmpty && $0 != "$null" }
        lines.append("raw strings=\(raw.count)")
        for (index, string) in raw.prefix(80).enumerated() {
            lines.append("raw[\(index)]=\"\(compactForDump(string, limit: 240))\"")
        }

        if entryCount >= maxEntries {
            lines.append("decoded dump truncated at \(maxEntries) entries")
        }
        return lines
    }

    // MARK: - Strategy 1: NSKeyedUnarchiver

    private static func parseViaKeyedUnarchiver(_ data: Data) -> NotificationContent? {
        guard data.prefix(8) == Data("bplist00".utf8) else { return nil }

        do {
            let unarchiver = try NSKeyedUnarchiver(forReadingFrom: data)
            unarchiver.requiresSecureCoding = false

            // Try decoding as NSDictionary directly
            if let dict = unarchiver.decodeObject(forKey: NSKeyedArchiveRootObjectKey) as? NSDictionary {
                return extractFromDict(dict)
            }

            // Try decoding as NSArray and look for a dict inside
            if let arr = unarchiver.decodeObject(forKey: NSKeyedArchiveRootObjectKey) as? NSArray {
                for item in arr {
                    if let dict = item as? NSDictionary, let content = extractFromDict(dict) {
                        return content
                    }
                }
            }
        } catch {}

        // Fallback: parse as raw plist and walk $objects manually
        return parseNSKeyedArchiverManually(data)
    }

    private static func extractFromDict(_ dict: NSDictionary) -> NotificationContent? {
        extractFromValue(dictionaryToSwift(dict))
    }

    // Manual $objects traversal for cases NSKeyedUnarchiver can't decode
    private static func parseNSKeyedArchiverManually(_ data: Data) -> NotificationContent? {
        guard let plist = try? PropertyListSerialization.propertyList(from: data, format: nil) as? [String: Any],
              plist["$archiver"] as? String == "NSKeyedArchiver",
              let objects = plist["$objects"] as? [Any]
        else { return nil }

        for obj in objects {
            let decoded = decodeArchivedValue(obj, objects: objects)
            if let content = extractFromValue(decoded) {
                return content
            }
        }
        return nil
    }

    // MARK: - Strategy 2: Plain plist

    private static func parseViaPropertyList(_ data: Data) -> NotificationContent? {
        guard let plist = try? PropertyListSerialization.propertyList(from: data, format: nil) as? [String: Any],
              plist["$archiver"] == nil
        else { return nil }

        return extractFromValue(plist)
    }

    // MARK: - Strategy 3: Raw bytes scan

    private static func parseViaRawBytes(_ data: Data) -> NotificationContent {
        // Extract printable ASCII/UTF-8 runs of at least 6 chars
        let strings = rawStrings(in: data)

        // Filter out plist/binary artifacts
        let artifacts: Set<String> = ["$null", "NSKeyedArchiver", "bplist", "$top", "$objects"]
        let candidates = strings
            .filter { s in !artifacts.contains(where: { s.hasPrefix($0) }) }
            .sorted { $0.count > $1.count }

        return NotificationContent(
            title:  candidates.count > 1 ? candidates[1] : "",
            body:   candidates.first ?? "",
            sender: ""
        )
    }

    // MARK: - Archive decoding helpers

    private static func collectDebugValues(
        _ value: Any,
        path: String,
        add: (String) -> Void,
        depth: Int = 0
    ) {
        guard depth < 10 else { return }

        if let string = stringValue(value), !string.isEmpty {
            add("\(path)=\"\(compactForDump(string, limit: 240))\"")
            return
        }

        if let number = value as? NSNumber {
            add("\(path)=\(number)")
            return
        }

        if let dict = value as? [String: Any] {
            for key in dict.keys.sorted() {
                guard let child = dict[key] else { continue }
                collectDebugValues(child, path: "\(path).\(key)", add: add, depth: depth + 1)
            }
            return
        }

        if let dict = value as? NSDictionary {
            collectDebugValues(dictionaryToSwift(dict), path: path, add: add, depth: depth + 1)
            return
        }

        if let array = value as? [Any] {
            for (index, child) in array.prefix(30).enumerated() {
                collectDebugValues(child, path: "\(path)[\(index)]", add: add, depth: depth + 1)
            }
            if array.count > 30 {
                add("\(path) truncated array count=\(array.count)")
            }
            return
        }

        if let array = value as? NSArray {
            collectDebugValues(Array(array), path: path, add: add, depth: depth + 1)
        }
    }

    private static func extractFromValue(_ value: Any) -> NotificationContent? {
        if let dict = value as? [String: Any] {
            let title = firstString(in: dict, keys: titleKeys)
            let body = firstString(in: dict, keys: bodyKeys)
            let sender = firstString(in: dict, keys: senderKeys)
            if !title.isEmpty || !body.isEmpty {
                let normalized = splitArrowTitle(title)
                return NotificationContent(title: normalized.title, rawTitle: title, body: body, sender: sender, subtitle: normalized.subtitle)
            }
            for nested in dict.values {
                if let content = extractFromValue(nested) {
                    return content
                }
            }
        } else if let dict = value as? NSDictionary {
            return extractFromValue(dictionaryToSwift(dict))
        } else if let array = value as? [Any] {
            for nested in array {
                if let content = extractFromValue(nested) {
                    return content
                }
            }
        } else if let array = value as? NSArray {
            for nested in array {
                if let content = extractFromValue(nested) {
                    return content
                }
            }
        }
        return nil
    }

    private static func firstString(in dict: [String: Any], keys: [String]) -> String {
        for key in keys {
            guard let value = dict[key], let string = stringValue(value), !string.isEmpty else { continue }
            return string
        }
        return ""
    }

    private static func decodeArchivedValue(_ value: Any, objects: [Any], depth: Int = 0) -> Any {
        guard depth < 24 else { return value }
        if let idx = uidIndex(value), idx >= 0, idx < objects.count {
            return decodeArchivedValue(objects[idx], objects: objects, depth: depth + 1)
        }
        if let dict = value as? [String: Any] {
            if let keys = dict["NS.keys"] as? [Any], let values = dict["NS.objects"] as? [Any] {
                var decoded: [String: Any] = [:]
                for (rawKey, rawValue) in zip(keys, values) {
                    let keyValue = decodeArchivedValue(rawKey, objects: objects, depth: depth + 1)
                    let key = stringValue(keyValue) ?? String(describing: keyValue)
                    decoded[key] = decodeArchivedValue(rawValue, objects: objects, depth: depth + 1)
                }
                return decoded
            }
            if let rawString = dict["NS.string"] {
                return decodeArchivedValue(rawString, objects: objects, depth: depth + 1)
            }
            return dict.mapValues { decodeArchivedValue($0, objects: objects, depth: depth + 1) }
        }
        if let dict = value as? NSDictionary {
            return decodeArchivedValue(dictionaryToSwift(dict), objects: objects, depth: depth)
        }
        if let array = value as? [Any] {
            return array.map { decodeArchivedValue($0, objects: objects, depth: depth + 1) }
        }
        if let array = value as? NSArray {
            return array.map { decodeArchivedValue($0, objects: objects, depth: depth + 1) }
        }
        return value
    }

    private static func uidIndex(_ value: Any) -> Int? {
        if let dict = value as? [String: Any] {
            if let intValue = dict["CF$UID"] as? Int { return intValue }
            if let number = dict["CF$UID"] as? NSNumber { return number.intValue }
        }

        for child in Mirror(reflecting: value).children {
            guard let label = child.label, label.contains("value") else { continue }
            if let intValue = child.value as? Int { return intValue }
            if let number = child.value as? NSNumber { return number.intValue }
        }

        let description = String(describing: value)
        guard description.lowercased().contains("uid") else { return nil }
        return description
            .split(whereSeparator: { !$0.isNumber })
            .last
            .flatMap { Int($0) }
    }

    private static func stringValue(_ value: Any) -> String? {
        if let string = value as? String {
            return cleanString(string)
        }
        if let string = value as? NSString {
            return cleanString(string as String)
        }
        return nil
    }

    private static func cleanString(_ value: String) -> String {
        value == "$null" ? "" : value
    }

    private static func splitArrowTitle(_ value: String) -> (title: String, subtitle: String) {
        let marker = " →"
        guard let range = value.range(of: marker) else { return (value, "") }
        let trimmed = value[..<range.lowerBound].trimmingCharacters(in: .whitespacesAndNewlines)
        let subtitle = value[range.upperBound...].trimmingCharacters(in: .whitespacesAndNewlines)
        return trimmed.isEmpty ? (value, "") : (String(trimmed), String(subtitle))
    }

    private static func dictionaryToSwift(_ dict: NSDictionary) -> [String: Any] {
        var result: [String: Any] = [:]
        for (key, value) in dict {
            result[String(describing: key)] = value
        }
        return result
    }

    private static func rawStrings(in data: Data) -> [String] {
        var strings: [String] = []
        var current = Data()

        for byte in data {
            if byte >= 0x20 && byte < 0x7f {
                current.append(byte)
            } else {
                if current.count >= 3, let s = String(data: current, encoding: .utf8) {
                    strings.append(s)
                }
                current = Data()
            }
        }
        if current.count >= 3, let s = String(data: current, encoding: .utf8) {
            strings.append(s)
        }
        return strings
    }

    private static func hexPrefix(_ data: Data, limit: Int) -> String {
        data.prefix(limit)
            .map { String(format: "%02x", $0) }
            .joined(separator: " ")
    }

    private static func compactForDump(_ value: String, limit: Int) -> String {
        let normalized = value
            .replacingOccurrences(of: "\r", with: "\\r")
            .replacingOccurrences(of: "\n", with: "\\n")
        guard normalized.count > limit else { return normalized }
        return String(normalized.prefix(limit)) + "…"
    }
}
