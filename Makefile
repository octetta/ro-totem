APP_NAME := ro-totem
HOST_OS := $(shell uname -s)
HOST_ARCH := $(shell uname -m)

ASSETS_DIR := assets
BUILD_DIR := build
DIST_DIR := dist

SKRED_DIR := vendor/skred
WEBVIEW_DIR := vendor/webview
COMMON_SOURCES := rototem.c ui.html \
	$(SKRED_DIR)/include/api.h \
	$(WEBVIEW_DIR)/webview.h

LINUX_BUILD_DIR := $(BUILD_DIR)/linux
LINUX_BINARY := $(LINUX_BUILD_DIR)/$(APP_NAME)
LINUX_PACKAGE_NAME := $(APP_NAME)-linux-$(HOST_ARCH)
LINUX_PACKAGE_DIR := $(DIST_DIR)/$(LINUX_PACKAGE_NAME)
LINUX_ARCHIVE := $(DIST_DIR)/$(LINUX_PACKAGE_NAME).tar.gz
APPDIR := $(DIST_DIR)/$(APP_NAME).AppDir
APPDIR_USR := $(APPDIR)/usr
APPIMAGE := $(DIST_DIR)/$(APP_NAME)-linux-$(HOST_ARCH).AppImage
APPIMAGETOOL ?= appimagetool
LINUX_CFLAGS = $(shell pkg-config --cflags gtk+-3.0 webkit2gtk-4.0)
LINUX_LIBS = $(shell pkg-config --libs gtk+-3.0 webkit2gtk-4.0)

MACOS_BUILD_DIR := $(BUILD_DIR)/macos
MACOS_BINARY := $(MACOS_BUILD_DIR)/$(APP_NAME)
MACOS_APP := $(APP_NAME).app
MACOS_CONTENTS := $(MACOS_APP)/Contents
MACOS_EXECUTABLES := $(MACOS_CONTENTS)/MacOS
MACOS_RESOURCES := $(MACOS_CONTENTS)/Resources
MACOS_ARCHIVE := ro-totem-gemini-epsilon-one-2026.zip

ifeq ($(HOST_OS),Darwin)
DEFAULT_TARGET := macos-package
else ifeq ($(HOST_OS),Linux)
DEFAULT_TARGET := linux-package
else
$(error Unsupported build platform: $(HOST_OS))
endif

.DEFAULT_GOAL := all

.PHONY: all linux linux-bundle linux-package appdir appimage \
	macos bundle macos-package clean

all: $(DEFAULT_TARGET)

linux: $(LINUX_BINARY)

$(LINUX_BINARY): $(COMMON_SOURCES) $(WEBVIEW_DIR)/webview-gtk.c \
		$(SKRED_DIR)/lib/linux/libapi.a
	mkdir -p $(LINUX_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LINUX_CFLAGS) \
		-DWEBVIEW_GTK=1 rototem.c \
		-L$(SKRED_DIR)/lib/linux -lapi $(LINUX_LIBS) -lm \
		-o $@

linux-package: $(LINUX_BINARY)
	rm -rf $(LINUX_PACKAGE_DIR)
	mkdir -p $(LINUX_PACKAGE_DIR)
	cp $(LINUX_BINARY) $(LINUX_PACKAGE_DIR)/
	cp ui.html $(LINUX_PACKAGE_DIR)/
	cp $(ASSETS_DIR)/rototem.png $(LINUX_PACKAGE_DIR)/
	cp packaging/linux/README.txt $(LINUX_PACKAGE_DIR)/
	mkdir -p $(DIST_DIR)
	tar -C $(DIST_DIR) -czf $(LINUX_ARCHIVE) $(LINUX_PACKAGE_NAME)

# Compatibility with the previous Linux target name.
linux-bundle: linux-package

appdir: $(LINUX_BINARY)
	rm -rf $(APPDIR)
	mkdir -p $(APPDIR_USR)/bin
	mkdir -p $(APPDIR_USR)/share/applications
	mkdir -p $(APPDIR_USR)/share/icons/hicolor/728x728/apps
	cp $(LINUX_BINARY) $(APPDIR_USR)/bin/$(APP_NAME)
	cp ui.html $(APPDIR_USR)/bin/
	cp packaging/linux/AppRun $(APPDIR)/
	cp packaging/linux/$(APP_NAME).desktop $(APPDIR)/
	cp packaging/linux/$(APP_NAME).desktop \
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

$(MACOS_BINARY): $(COMMON_SOURCES) $(WEBVIEW_DIR)/webview-cocoa.c \
		$(SKRED_DIR)/lib/macos/libapi.a
	mkdir -p $(MACOS_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -g -ObjC \
		-DOBJC_OLD_DISPATCH_PROTOTYPES=1 -DWEBVIEW_COCOA=1 rototem.c \
		-framework WebKit -framework CoreFoundation \
		-L$(SKRED_DIR)/lib/macos -lapi \
		-o $@

macos-package: $(MACOS_BINARY)
	rm -rf $(MACOS_APP)
	mkdir -p $(MACOS_EXECUTABLES) $(MACOS_RESOURCES)
	cp $(ASSETS_DIR)/Info.plist $(MACOS_CONTENTS)/
	cp $(MACOS_BINARY) $(MACOS_EXECUTABLES)/$(APP_NAME)
	cp ui.html $(MACOS_RESOURCES)/
	cp $(ASSETS_DIR)/rototem.icns $(MACOS_RESOURCES)/
	xattr -cr $(MACOS_APP)
	codesign --force --deep --sign - $(MACOS_APP)
	rm -f $(MACOS_ARCHIVE)
	zip -r -y $(MACOS_ARCHIVE) $(MACOS_APP)

macos: macos-package

# Compatibility with the previous macOS target name.
bundle: macos-package

clean:
	rm -rf $(BUILD_DIR) $(DIST_DIR) $(MACOS_APP)
	rm -f $(MACOS_ARCHIVE) rototem rototem_bin
