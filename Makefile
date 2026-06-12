# ro-totem cross-platform build and packaging
#
# Default `make` output:
#   Linux   -> portable .tar.gz
#   macOS   -> signed .app in a .zip
#   Windows -> portable .zip (preliminary; see windows-check)

# -----------------------------------------------------------------------------
# Common settings
# -----------------------------------------------------------------------------

APP_NAME := ro-totem

ifeq ($(OS),Windows_NT)
HOST_OS := Windows
HOST_ARCH ?= x86_64
else
HOST_OS := $(shell uname -s)
HOST_ARCH := $(shell uname -m)
endif

ASSETS_DIR := assets
BUILD_DIR := build
DIST_DIR := dist
PACKAGING_DIR := packaging
SKRED_DIR := vendor/skred
WEBVIEW_DIR := vendor/webview
MINIZ_DIR := vendor/miniz
MINIZ_SOURCE := $(MINIZ_DIR)/miniz.c

COMMON_SOURCES := rototem.c ui.html \
	$(SKRED_DIR)/include/api.h \
	$(MINIZ_DIR)/miniz.h $(MINIZ_SOURCE) \
	$(WEBVIEW_DIR)/webview.h

ifeq ($(HOST_OS),Windows)
DEFAULT_TARGET := windows-package
else ifeq ($(HOST_OS),Darwin)
DEFAULT_TARGET := macos-package
else ifeq ($(HOST_OS),Linux)
DEFAULT_TARGET := linux-package
else
$(error Unsupported build platform: $(HOST_OS))
endif

.DEFAULT_GOAL := all

.PHONY: all clean \
	linux linux-bundle linux-package appdir appimage \
	macos bundle macos-package \
	windows windows-check windows-package

all: $(DEFAULT_TARGET)

# -----------------------------------------------------------------------------
# Linux
# -----------------------------------------------------------------------------

LINUX_BUILD_DIR := $(BUILD_DIR)/linux
LINUX_BINARY := $(LINUX_BUILD_DIR)/$(APP_NAME)
LINUX_PACKAGE_NAME := $(APP_NAME)-linux-$(HOST_ARCH)
LINUX_PACKAGE_DIR := $(DIST_DIR)/$(LINUX_PACKAGE_NAME)
LINUX_ARCHIVE := $(DIST_DIR)/$(LINUX_PACKAGE_NAME).tar.gz
LINUX_CFLAGS = $(shell pkg-config --cflags gtk+-3.0 webkit2gtk-4.1)
LINUX_LIBS = $(shell pkg-config --libs gtk+-3.0 webkit2gtk-4.1)

linux: $(LINUX_BINARY)

$(LINUX_BINARY): $(COMMON_SOURCES) $(WEBVIEW_DIR)/webview-gtk.c \
		$(SKRED_DIR)/lib/linux/libapi.a
	mkdir -p $(LINUX_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LINUX_CFLAGS) \
		-DWEBVIEW_GTK=1 rototem.c $(MINIZ_SOURCE) \
		-L$(SKRED_DIR)/lib/linux -lapi $(LINUX_LIBS) -lm \
		-o $@

linux-package: $(LINUX_BINARY)
	rm -rf $(LINUX_PACKAGE_DIR)
	mkdir -p $(LINUX_PACKAGE_DIR)
	cp $(LINUX_BINARY) $(LINUX_PACKAGE_DIR)/
	cp ui.html $(LINUX_PACKAGE_DIR)/
	cp $(ASSETS_DIR)/rototem.png $(LINUX_PACKAGE_DIR)/
	cp $(PACKAGING_DIR)/linux/README.txt $(LINUX_PACKAGE_DIR)/
	mkdir -p $(DIST_DIR)
	tar -C $(DIST_DIR) -czf $(LINUX_ARCHIVE) $(LINUX_PACKAGE_NAME)

# Compatibility with the previous Linux target name.
linux-bundle: linux-package

# -----------------------------------------------------------------------------
# Linux AppImage
# -----------------------------------------------------------------------------

APPDIR := $(DIST_DIR)/$(APP_NAME).AppDir
APPDIR_USR := $(APPDIR)/usr
APPIMAGE := $(DIST_DIR)/$(APP_NAME)-linux-$(HOST_ARCH).AppImage
APPIMAGETOOL ?= appimagetool

appdir: $(LINUX_BINARY)
	rm -rf $(APPDIR)
	mkdir -p $(APPDIR_USR)/bin
	mkdir -p $(APPDIR_USR)/share/applications
	mkdir -p $(APPDIR_USR)/share/icons/hicolor/728x728/apps
	cp $(LINUX_BINARY) $(APPDIR_USR)/bin/$(APP_NAME)
	cp ui.html $(APPDIR_USR)/bin/
	cp $(PACKAGING_DIR)/linux/AppRun $(APPDIR)/
	cp $(PACKAGING_DIR)/linux/$(APP_NAME).desktop $(APPDIR)/
	cp $(PACKAGING_DIR)/linux/$(APP_NAME).desktop \
		$(APPDIR_USR)/share/applications/
	cp $(ASSETS_DIR)/rototem.png $(APPDIR)/$(APP_NAME).png
	cp $(ASSETS_DIR)/rototem.png \
		$(APPDIR_USR)/share/icons/hicolor/728x728/apps/$(APP_NAME).png
	ln -s $(APP_NAME).png $(APPDIR)/.DirIcon
	chmod +x $(APPDIR)/AppRun $(APPDIR_USR)/bin/$(APP_NAME)

appimage: appdir
	ARCH=$(HOST_ARCH) APPIMAGE_EXTRACT_AND_RUN=1 \
		$(APPIMAGETOOL) $(APPDIR) $(APPIMAGE)
	chmod +x $(APPIMAGE)

# -----------------------------------------------------------------------------
# macOS
# -----------------------------------------------------------------------------

MACOS_BUILD_DIR := $(BUILD_DIR)/macos
MACOS_EXECUTABLE_NAME := rototem
MACOS_BINARY := $(MACOS_BUILD_DIR)/$(MACOS_EXECUTABLE_NAME)
MACOS_APP := $(APP_NAME).app
MACOS_CONTENTS := $(MACOS_APP)/Contents
MACOS_EXECUTABLES := $(MACOS_CONTENTS)/MacOS
MACOS_RESOURCES := $(MACOS_CONTENTS)/Resources
MACOS_ARCHIVE := ro-totem-gemini-zeta-one-equus.zip

$(MACOS_BINARY): $(COMMON_SOURCES) $(WEBVIEW_DIR)/webview-cocoa.c \
		$(SKRED_DIR)/lib/macos/libapi.a
	mkdir -p $(MACOS_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -g -ObjC \
		-DOBJC_OLD_DISPATCH_PROTOTYPES=1 -DWEBVIEW_COCOA=1 \
		rototem.c $(MINIZ_SOURCE) \
		-framework WebKit -framework CoreFoundation \
		-L$(SKRED_DIR)/lib/macos -lapi \
		-o $@

macos-package: $(MACOS_BINARY)
	rm -rf $(MACOS_APP)
	mkdir -p $(MACOS_EXECUTABLES) $(MACOS_RESOURCES)
	cp $(ASSETS_DIR)/Info.plist $(MACOS_CONTENTS)/
	cp $(MACOS_BINARY) $(MACOS_EXECUTABLES)/$(MACOS_EXECUTABLE_NAME)
	cp ui.html $(MACOS_RESOURCES)/
	cp $(ASSETS_DIR)/rototem.icns $(MACOS_RESOURCES)/
	test -x $(MACOS_EXECUTABLES)/$(MACOS_EXECUTABLE_NAME)
	xattr -cr $(MACOS_APP)
	codesign --force --deep --sign - $(MACOS_APP)
	rm -f $(MACOS_ARCHIVE)
	zip -r -y $(MACOS_ARCHIVE) $(MACOS_APP)

macos: macos-package

# Compatibility with the previous macOS target name.
bundle: macos-package

# -----------------------------------------------------------------------------
# Windows (preliminary Zig build)
# -----------------------------------------------------------------------------

ZIG ?= zig
WINDOWS_TARGET ?= x86_64-windows-gnu
WINDOWS_ARCH ?= x86_64
WINDOWS_CC = $(ZIG) cc -target $(WINDOWS_TARGET)
WINDOWS_BUILD_DIR := $(BUILD_DIR)/windows
WINDOWS_BINARY := $(WINDOWS_BUILD_DIR)/$(APP_NAME).exe
WINDOWS_PACKAGE_NAME := $(APP_NAME)-windows-$(WINDOWS_ARCH)
WINDOWS_PACKAGE_DIR := $(DIST_DIR)/$(WINDOWS_PACKAGE_NAME)
WINDOWS_ARCHIVE := $(DIST_DIR)/$(WINDOWS_PACKAGE_NAME).zip
WINDOWS_SKRED_LIB := $(SKRED_DIR)/lib/windows/libapi.a
WINDOWS_WEBVIEW2_HEADER := $(WEBVIEW_DIR)/WebView2.h
WINDOWS_WEBVIEW2_SOURCE := $(WEBVIEW_DIR)/webview-win32-edge.c
WINDOWS_OPTIONAL_INPUTS := $(wildcard \
	$(WINDOWS_SKRED_LIB) \
	$(WINDOWS_WEBVIEW2_HEADER) \
	$(WINDOWS_WEBVIEW2_SOURCE))
WINDOWS_SYSTEM_LIBS ?= -luser32 -lole32 -loleaut32 -luuid -lcomctl32
WINDOWS_LDFLAGS ?= -mwindows

# Keep these checks separate so an incomplete Windows setup fails with a useful
# message instead of a long compiler error.
windows-check:
	@missing=0; \
	if ! test -f $(WINDOWS_SKRED_LIB); then \
		echo "Missing $(WINDOWS_SKRED_LIB)"; \
		echo "Build libapi.a for $(WINDOWS_TARGET) and place it there."; \
		missing=1; \
	fi; \
	if ! test -f $(WINDOWS_WEBVIEW2_HEADER); then \
		echo "Missing $(WINDOWS_WEBVIEW2_HEADER)"; \
		echo "Add the WebView2 header expected by the vendored Windows backend."; \
		missing=1; \
	fi; \
	if ! test -f $(WINDOWS_WEBVIEW2_SOURCE); then \
		echo "Missing $(WINDOWS_WEBVIEW2_SOURCE)"; \
		echo "Add the WebView2 implementation expected by webview-win32.c."; \
		missing=1; \
	fi; \
	test $$missing -eq 0

windows: windows-check
	$(MAKE) --no-print-directory $(WINDOWS_BINARY)

$(WINDOWS_BINARY): $(COMMON_SOURCES) $(WEBVIEW_DIR)/webview-win32.c \
		$(WINDOWS_OPTIONAL_INPUTS)
	mkdir -p $(WINDOWS_BUILD_DIR)
	$(WINDOWS_CC) $(CPPFLAGS) $(CFLAGS) \
		-I$(WEBVIEW_DIR) -DWEBVIEW_WINAPI=1 rototem.c $(MINIZ_SOURCE) \
		-L$(SKRED_DIR)/lib/windows -lapi \
		$(WINDOWS_SYSTEM_LIBS) $(WINDOWS_LDFLAGS) -o $@

windows-package: windows
	rm -rf $(WINDOWS_PACKAGE_DIR)
	mkdir -p $(WINDOWS_PACKAGE_DIR)
	cp $(WINDOWS_BINARY) $(WINDOWS_PACKAGE_DIR)/
	cp ui.html $(WINDOWS_PACKAGE_DIR)/
	cp $(PACKAGING_DIR)/windows/README.txt $(WINDOWS_PACKAGE_DIR)/
	rm -f $(WINDOWS_ARCHIVE)
	cd $(DIST_DIR) && zip -r \
		$(notdir $(WINDOWS_ARCHIVE)) $(WINDOWS_PACKAGE_NAME)

# -----------------------------------------------------------------------------
# Cleanup
# -----------------------------------------------------------------------------

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR) $(MACOS_APP)
	rm -f $(MACOS_ARCHIVE) rototem rototem_bin
