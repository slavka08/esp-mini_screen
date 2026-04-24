// swift-tools-version: 5.9
import PackageDescription

let package = Package(
    name: "ESPNotifier",
    platforms: [.macOS(.v13)],
    targets: [
        .executableTarget(
            name: "ESPNotifier",
            path: "Sources/ESPNotifier",
            swiftSettings: [
                .unsafeFlags(["-framework", "AppKit"])
            ]
        )
    ]
)
