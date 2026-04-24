import AppKit

@main
struct ESPNotifierApp {
    static func main() {
        let app = NSApplication.shared
        let delegate = AppDelegate()
        app.delegate = delegate
        app.run()
    }
}

class AppDelegate: NSObject, NSApplicationDelegate {
    private var menuBarController: MenuBarController!

    func applicationDidFinishLaunching(_ notification: Notification) {
        // Hide from Dock (belt-and-suspenders alongside LSUIElement in Info.plist)
        NSApp.setActivationPolicy(.accessory)

        menuBarController = MenuBarController()

        // Start watcher if enabled
        if AppSettings.shared.isEnabled {
            NotificationWatcher.shared.start()
        }

        // Show Settings on first launch (no device configured yet)
        if AppSettings.shared.deviceURL.isEmpty {
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.3) {
                SettingsWindowController.shared.showWindow()
            }
        }
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
        false  // Menu bar app keeps running without windows
    }
}
