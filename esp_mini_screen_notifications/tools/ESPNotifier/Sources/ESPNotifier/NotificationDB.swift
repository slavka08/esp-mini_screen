import Foundation
import AppKit
import SQLite3

struct DBRecord {
    let recId: Int
    let bundleId: String
    let data: Data
    let deliveredDate: Double?
}

struct AppRecord: Identifiable {
    let bundleId: String
    let displayName: String
    let count: Int
    var id: String { bundleId }
}

class NotificationDB {
    // Candidate notification DB paths across macOS releases.
    private static let candidatePaths: [String] = [
        // Observed on modern macOS builds.
        "Library/Group Containers/group.com.apple.usernoted/db2/db",
        // Alternate Group Container name seen on some installs.
        "Library/Group Containers/group.com.apple.UserNotifications/db2/db",
        // Legacy path (older macOS versions).
        "Library/Application Support/com.apple.notificationcenter/db2/db",
    ]

    private static var _dbPath: URL?
    static var dbPath: URL {
        if let cached = _dbPath { return cached }
        let home = FileManager.default.homeDirectoryForCurrentUser
        var firstExisting: URL?
        for rel in candidatePaths {
            let url = home.appendingPathComponent(rel)
            if FileManager.default.fileExists(atPath: url.path), firstExisting == nil {
                firstExisting = url
            }
            if let fh = FileHandle(forReadingAtPath: url.path) {
                fh.closeFile()
                _dbPath = url
                return url
            }
        }
        // If the file exists but is not readable yet (e.g. FDA not granted), keep that path.
        let fallback = firstExisting ?? home.appendingPathComponent(candidatePaths[0])
        _dbPath = fallback
        return fallback
    }

    static func hasAccess() -> Bool {
        _dbPath = nil  // clear cache so we re-probe after relaunch
        guard let fh = FileHandle(forReadingAtPath: dbPath.path) else { return false }
        fh.closeFile()
        return true
    }

    private static func openReadOnly() -> OpaquePointer? {
        // Use URI with mode=ro so we never acquire a write lock
        let uri = "file:\(dbPath.path)?mode=ro"
        var db: OpaquePointer?
        guard sqlite3_open_v2(uri, &db, SQLITE_OPEN_READONLY | SQLITE_OPEN_URI, nil) == SQLITE_OK else {
            sqlite3_close(db)
            return nil
        }
        sqlite3_busy_timeout(db, 3000)
        return db
    }

    // Copy DB to a temp file to avoid WAL lock issues
    private static func openViaCopy() -> OpaquePointer? {
        let tmp = FileManager.default.temporaryDirectory
            .appendingPathComponent(UUID().uuidString + ".db")
        do {
            try FileManager.default.copyItem(at: dbPath, to: tmp)
            // Copy SQLite sidecar files if present so the snapshot is consistent.
            // SQLite names these files as "<db>-wal" and "<db>-shm".
            let wal = URL(fileURLWithPath: dbPath.path + "-wal")
            let shm = URL(fileURLWithPath: dbPath.path + "-shm")
            let tmpWal = URL(fileURLWithPath: tmp.path + "-wal")
            let tmpShm = URL(fileURLWithPath: tmp.path + "-shm")
            if FileManager.default.fileExists(atPath: wal.path) {
                try? FileManager.default.copyItem(at: wal, to: tmpWal)
            }
            if FileManager.default.fileExists(atPath: shm.path) {
                try? FileManager.default.copyItem(at: shm, to: tmpShm)
            }
        } catch {
            return nil
        }
        var db: OpaquePointer?
        guard sqlite3_open_v2(tmp.path, &db, SQLITE_OPEN_READONLY, nil) == SQLITE_OK else {
            sqlite3_close(db)
            try? FileManager.default.removeItem(at: tmp)
            return nil
        }
        sqlite3_busy_timeout(db, 1000)
        return db
    }

    private static func open() -> OpaquePointer? {
        openReadOnly() ?? openViaCopy()
    }

    static func getCurrentMaxRecId() -> Int {
        guard let db = open() else { return 0 }
        defer { sqlite3_close(db) }
        var stmt: OpaquePointer?
        guard sqlite3_prepare_v2(db, "SELECT MAX(rec_id) FROM record", -1, &stmt, nil) == SQLITE_OK else { return 0 }
        defer { sqlite3_finalize(stmt) }
        guard sqlite3_step(stmt) == SQLITE_ROW else { return 0 }
        return Int(sqlite3_column_int64(stmt, 0))
    }

    static func getNewRecords(since lastRecId: Int, bundleIds: Set<String>) -> [DBRecord] {
        guard !bundleIds.isEmpty, let db = open() else { return [] }
        defer { sqlite3_close(db) }

        let placeholders = bundleIds.map { _ in "?" }.joined(separator: ",")
        let sql = """
            SELECT r.rec_id, a.identifier, r.data, r.delivered_date
            FROM record r
            JOIN app a ON r.app_id = a.app_id
            WHERE r.rec_id > ? AND a.identifier IN (\(placeholders))
            ORDER BY r.rec_id ASC
            """
        var stmt: OpaquePointer?
        guard sqlite3_prepare_v2(db, sql, -1, &stmt, nil) == SQLITE_OK else { return [] }
        defer { sqlite3_finalize(stmt) }

        sqlite3_bind_int64(stmt, 1, Int64(lastRecId))
        for (i, id) in bundleIds.enumerated() {
            sqlite3_bind_text(stmt, Int32(i + 2), (id as NSString).utf8String, -1, nil)
        }

        var records: [DBRecord] = []
        while sqlite3_step(stmt) == SQLITE_ROW {
            let recId = Int(sqlite3_column_int64(stmt, 0))
            let bundleId = String(cString: sqlite3_column_text(stmt, 1))
            let deliveredDate: Double? = sqlite3_column_type(stmt, 3) != SQLITE_NULL
                ? sqlite3_column_double(stmt, 3) : nil

            let bytes = sqlite3_column_blob(stmt, 2)
            let length = sqlite3_column_bytes(stmt, 2)
            guard let bytes, length > 0 else { continue }
            let data = Data(bytes: bytes, count: Int(length))

            records.append(DBRecord(recId: recId, bundleId: bundleId, data: data, deliveredDate: deliveredDate))
        }
        return records
    }

    static func getAllApps() -> [AppRecord] {
        guard let db = open() else { return [] }
        defer { sqlite3_close(db) }

        let sql = """
            SELECT a.identifier, COUNT(r.rec_id) as cnt
            FROM app a
            LEFT JOIN record r ON a.app_id = r.app_id
            GROUP BY a.app_id
            ORDER BY cnt DESC
            LIMIT 100
            """
        var stmt: OpaquePointer?
        guard sqlite3_prepare_v2(db, sql, -1, &stmt, nil) == SQLITE_OK else { return [] }
        defer { sqlite3_finalize(stmt) }

        var apps: [AppRecord] = []
        while sqlite3_step(stmt) == SQLITE_ROW {
            let bundleId = String(cString: sqlite3_column_text(stmt, 0))
            let count = Int(sqlite3_column_int64(stmt, 1))
            let displayName = resolveDisplayName(for: bundleId)
            apps.append(AppRecord(bundleId: bundleId, displayName: displayName, count: count))
        }
        return apps
    }

    static func resolveDisplayName(for bundleId: String) -> String {
        if let url = NSWorkspace.shared.urlForApplication(withBundleIdentifier: bundleId) {
            let values = (try? url.resourceValues(forKeys: [URLResourceKey.localizedNameKey]))
            if let name = values?.localizedName {
                return name
            }
        }
        // Fallback: title-case the last meaningful component of the bundle ID
        let parts = bundleId.components(separatedBy: ".")
        let skipSuffixes: Set<String> = ["app", "mac", "macos", "desktop", "ios", "osx"]
        for part in parts.reversed() where !skipSuffixes.contains(part.lowercased()) && !part.isEmpty {
            return part.prefix(1).uppercased() + part.dropFirst()
        }
        return bundleId
    }
}
