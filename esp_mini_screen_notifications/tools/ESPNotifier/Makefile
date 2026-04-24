APP     = ESPNotifier.app
BIN     = ESPNotifier
RELEASE = .build/release

.PHONY: build install open clean

build:
	swift build -c release 2>&1
	rm -rf $(APP)
	mkdir -p $(APP)/Contents/MacOS
	mkdir -p $(APP)/Contents/Resources
	cp $(RELEASE)/$(BIN) $(APP)/Contents/MacOS/
	cp Info.plist $(APP)/Contents/
	@echo "✓ Built $(APP)"

open: build
	open $(APP)

install: build
	@if [ -d "$$HOME/Applications" ]; then \
		cp -r $(APP) "$$HOME/Applications/"; \
		echo "✓ Installed to ~/Applications/$(APP)"; \
	else \
		cp -r $(APP) /Applications/; \
		echo "✓ Installed to /Applications/$(APP)"; \
	fi

clean:
	swift package clean
	rm -rf $(APP)
