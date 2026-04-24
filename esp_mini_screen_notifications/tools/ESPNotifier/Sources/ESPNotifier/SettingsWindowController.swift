import AppKit
import SwiftUI

class SettingsWindowController {
    static let shared = SettingsWindowController()

    private var window: NSWindow?

    private init() {}

    func showWindow() {
        if let existing = window, existing.isVisible {
            existing.makeKeyAndOrderFront(nil)
            NSApp.activate(ignoringOtherApps: true)
            return
        }

        let view = NSHostingView(rootView: SettingsView())
        view.autoresizingMask = [.width, .height]

        let w = NSWindow(
            contentRect: NSRect(x: 0, y: 0, width: 480, height: 560),
            styleMask: [.titled, .closable, .miniaturizable, .resizable],
            backing: .buffered,
            defer: false
        )
        w.title = "ESPNotifier Settings"
        w.contentView = view
        w.center()
        w.setFrameAutosaveName("ESPNotifierSettings")
        w.isReleasedWhenClosed = false

        self.window = w
        w.makeKeyAndOrderFront(nil)
        NSApp.activate(ignoringOtherApps: true)
    }
}
