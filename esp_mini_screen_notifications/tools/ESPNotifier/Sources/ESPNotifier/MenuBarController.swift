import AppKit
import Combine

@MainActor
class MenuBarController {
    private var statusItem: NSStatusItem!
    private var menu: NSMenu!
    private var toggleItem: NSMenuItem!
    private var deviceStatusItem: NSMenuItem!
    private var cancellables = Set<AnyCancellable>()

    // Periodically check device reachability
    private var deviceCheckTimer: Timer?
    private var deviceReachable: Bool = false

    init() {
        statusItem = NSStatusBar.system.statusItem(withLength: NSStatusItem.variableLength)
        buildMenu()
        updateIcon()

        // Observe settings changes to update menu
        AppSettings.shared.$isEnabled
            .receive(on: DispatchQueue.main)
            .sink { [weak self] _ in self?.updateIcon(); self?.updateToggleItem() }
            .store(in: &cancellables)

        startDeviceCheck()
    }

    // MARK: - Menu construction

    private func buildMenu() {
        menu = NSMenu()

        toggleItem = NSMenuItem(title: "", action: #selector(toggleForwarding), keyEquivalent: "")
        toggleItem.target = self
        updateToggleItem()
        menu.addItem(toggleItem)

        menu.addItem(.separator())

        deviceStatusItem = NSMenuItem(title: "Device: not configured", action: nil, keyEquivalent: "")
        deviceStatusItem.isEnabled = false
        menu.addItem(deviceStatusItem)

        menu.addItem(.separator())

        let settings = NSMenuItem(title: "Settings…", action: #selector(openSettings), keyEquivalent: ",")
        settings.target = self
        menu.addItem(settings)

        menu.addItem(.separator())

        let quit = NSMenuItem(title: "Quit ESPNotifier", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        menu.addItem(quit)

        statusItem.menu = menu
    }

    private func updateToggleItem() {
        let enabled = AppSettings.shared.isEnabled
        toggleItem.title = enabled ? "● Forwarding Active" : "○ Forwarding Paused"
        toggleItem.state = enabled ? .on : .off
    }

    private func updateIcon() {
        let enabled = AppSettings.shared.isEnabled
        let imageName = enabled ? "bell.fill" : "bell.slash.fill"
        let img = NSImage(systemSymbolName: imageName, accessibilityDescription: nil)
        img?.isTemplate = true
        statusItem.button?.image = img
    }

    private func updateDeviceStatus() {
        let url = AppSettings.shared.deviceURL
        if url.isEmpty {
            deviceStatusItem.title = "Device: not configured"
        } else {
            let host = URL(string: url)?.host ?? url
            let dot = deviceReachable ? "●" : "○"
            let status = deviceReachable ? "connected" : "unreachable"
            deviceStatusItem.title = "Device: \(host) \(dot) \(status)"
        }
    }

    // MARK: - Actions

    @objc private func toggleForwarding() {
        AppSettings.shared.isEnabled.toggle()
        if AppSettings.shared.isEnabled {
            NotificationWatcher.shared.start()
        } else {
            NotificationWatcher.shared.stop()
        }
    }

    @objc private func openSettings() {
        SettingsWindowController.shared.showWindow()
    }

    // MARK: - Device reachability check

    private func startDeviceCheck() {
        deviceCheckTimer = Timer.scheduledTimer(withTimeInterval: 15, repeats: true) { [weak self] _ in
            Task { @MainActor in self?.checkDevice() }
        }
        checkDevice()
    }

    private func checkDevice() {
        let url = AppSettings.shared.deviceURL
        guard !url.isEmpty else {
            DispatchQueue.main.async { self.updateDeviceStatus() }
            return
        }
        Task {
            let reachable = await ESPClient.testConnection(deviceURL: url)
            DispatchQueue.main.async {
                self.deviceReachable = reachable
                self.updateDeviceStatus()
            }
        }
    }
}
