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
APP_VERSION_FILE := VERSION
APP_VERSION := $(strip $(shell cat $(APP_VERSION_FILE)))
empty :=
space := $(empty) $(empty)
APP_VERSION_SLUG := $(subst $(space),-,$(APP_VERSION))

ifeq ($(OS),Windows_NT)
HOST_OS := Windows
HOST_ARCH ?= x86_64
else
HOST_OS := $(shell uname -s)
HOST_ARCH := $(shell uname -m)
endif
HOST_OS_LOWER := $(shell printf '%s' "$(HOST_OS)" | tr '[:upper:]' '[:lower:]')

ASSETS_DIR := assets
BUILD_DIR := build
DIST_DIR := dist
PACKAGING_DIR := packaging
SKRED_DIR := vendor/skred
WEBVIEW_DIR := vendor/webview
MINIZ_DIR := vendor/miniz
P5_DIR := vendor/p5
P5_JS := $(P5_DIR)/p5.min.js
VOCO_JS := voco.js
MINIZ_SOURCE := $(MINIZ_DIR)/miniz.c
UI_HTML := ui.html
UI_EMBEDDED_HTML := $(BUILD_DIR)/ui_embedded.html
UI_HTML_HEADER := $(BUILD_DIR)/ui_html.h
APP_VERSION_HEADER := $(BUILD_DIR)/rototem_version.h

SKRED_FLAVOR ?= maxed
SKRED_VERSION ?=
SKRED_DIST_NAME := skred-$(if $(SKRED_VERSION),$(SKRED_VERSION),*)-$(SKRED_FLAVOR)
ifeq ($(HOST_OS),Darwin)
SKRED_LEGACY_PLATFORM := macos
SKRED_DIST_PLATFORMS := darwin-$(HOST_ARCH) macos-$(HOST_ARCH) macos-universal
else ifeq ($(HOST_OS),Linux)
SKRED_LEGACY_PLATFORM := linux
SKRED_DIST_PLATFORMS := linux-$(HOST_ARCH)
else ifeq ($(HOST_OS),Windows)
SKRED_LEGACY_PLATFORM := windows
SKRED_DIST_PLATFORMS := windows-$(HOST_ARCH)
else
SKRED_LEGACY_PLATFORM := $(HOST_OS_LOWER)
SKRED_DIST_PLATFORMS := $(HOST_OS_LOWER)-$(HOST_ARCH)
endif
SKRED_DIST_ROOTS ?= $(SKRED_DIR) $(SKRED_DIR)/dist
SKRED_DIST_PREFIX_CANDIDATES := $(foreach root,$(SKRED_DIST_ROOTS),$(foreach platform,$(SKRED_DIST_PLATFORMS),$(wildcard $(root)/$(platform)/$(SKRED_DIST_NAME))))
SKRED_DIST_PREFIX ?= $(lastword $(sort $(SKRED_DIST_PREFIX_CANDIDATES)))
ifeq ($(SKRED_DIST_PREFIX),)
ifneq ($(SKRED_VERSION),)
$(error No Skred $(SKRED_VERSION) $(SKRED_FLAVOR) dist found for $(SKRED_DIST_PLATFORMS) under $(SKRED_DIST_ROOTS))
endif
endif
ifneq ($(SKRED_DIST_PREFIX),)
SKRED_INCLUDE_DIR ?= $(SKRED_DIST_PREFIX)/include
SKRED_LIB_DIR ?= $(if $(wildcard $(SKRED_DIST_PREFIX)/lib64/libapi.a),$(SKRED_DIST_PREFIX)/lib64,$(SKRED_DIST_PREFIX)/lib)
else
SKRED_INCLUDE_DIR ?= $(SKRED_DIR)/include
SKRED_LIB_DIR ?= $(SKRED_DIR)/lib/$(SKRED_LEGACY_PLATFORM)
endif
SKRED_API_HEADER := $(SKRED_INCLUDE_DIR)/skred/api.h
SKRED_VERSION_HEADER := $(SKRED_INCLUDE_DIR)/skred/skred-version.h
SKRED_API_LIB := $(SKRED_LIB_DIR)/libapi.a
PULP_RAW_BASE ?= https://raw.githubusercontent.com/octetta/pulp/main
PULP_VERSION ?= latest
PULP_PLATFORM ?= $(firstword $(SKRED_DIST_PLATFORMS))
PULP_API_DIST_ROOT ?= $(SKRED_DIR)/dist
PULP_API_DIST_DIR ?= $(PULP_API_DIST_ROOT)/$(PULP_PLATFORM)
PULP_API_TMP_DIR ?= $(BUILD_DIR)/pulp-api

COMMON_SOURCES := rototem.c \
	voco.h \
	$(VOCO_JS) \
	$(APP_VERSION_FILE) \
	$(MINIZ_DIR)/miniz.h $(MINIZ_SOURCE) \
	$(P5_JS) \
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

.PHONY: all clean skred-paths pulp-api \
	linux linux-bundle linux-package appdir appimage \
	macos bundle macos-package \
	windows windows-check windows-package

all: $(DEFAULT_TARGET)

skred-paths:
	@printf 'SKRED_DIST_PREFIX=%s\n' '$(SKRED_DIST_PREFIX)'
	@printf 'SKRED_INCLUDE_DIR=%s\n' '$(SKRED_INCLUDE_DIR)'
	@printf 'SKRED_LIB_DIR=%s\n' '$(SKRED_LIB_DIR)'
	@printf 'SKRED_API_LIB=%s\n' '$(SKRED_API_LIB)'

pulp-api:
	@set -eu; \
	version='$(PULP_VERSION)'; \
	if [ "$$version" = latest ]; then \
		version="$$(curl -fsSL '$(PULP_RAW_BASE)/VERSION')"; \
	fi; \
	package="skred-$$version-$(SKRED_FLAVOR)"; \
	archive="$$package.tar.gz"; \
	url='$(PULP_RAW_BASE)/dist/$(PULP_PLATFORM)/'"$$archive"; \
	printf 'Fetching %s\n' "$$url"; \
	rm -rf '$(PULP_API_TMP_DIR)'; \
	mkdir -p '$(PULP_API_TMP_DIR)' '$(PULP_API_DIST_DIR)'; \
	curl -fL "$$url" -o '$(PULP_API_TMP_DIR)'/"$$archive"; \
	tar -C '$(PULP_API_TMP_DIR)' -xzf '$(PULP_API_TMP_DIR)'/"$$archive"; \
	test -f '$(PULP_API_TMP_DIR)'/"$$package/include/skred/api.h"; \
	if [ ! -f '$(PULP_API_TMP_DIR)'/"$$package/lib64/libapi.a" ] && \
			[ ! -f '$(PULP_API_TMP_DIR)'/"$$package/lib/libapi.a" ]; then \
		echo "Missing libapi.a in $$archive"; \
		exit 1; \
	fi; \
	rm -rf '$(PULP_API_DIST_DIR)'/"$$package"; \
	mv '$(PULP_API_TMP_DIR)'/"$$package" '$(PULP_API_DIST_DIR)'/; \
	printf 'Installed %s\n' '$(PULP_API_DIST_DIR)'/"$$package"; \
	$(MAKE) --no-print-directory skred-paths \
		SKRED_VERSION="$$version" \
		SKRED_DIST_ROOTS='$(SKRED_DIR) $(PULP_API_DIST_ROOT)'

$(UI_EMBEDDED_HTML): $(UI_HTML) $(P5_JS) $(VOCO_JS)
	mkdir -p $(dir $@)
	awk ' \
		/<script src="vendor\/p5\/p5.min.js"><\/script>/ { \
			print "<script>"; \
			while ((getline line < "$(P5_JS)") > 0) print line; \
			close("$(P5_JS)"); \
			print "</script>"; \
			next; \
		} \
		/<script src="voco.js"><\/script>/ { \
			print "<script>"; \
			while ((getline line < "$(VOCO_JS)") > 0) print line; \
			close("$(VOCO_JS)"); \
			print "</script>"; \
			next; \
		} \
		{ print } \
	' $(UI_HTML) > $@

$(UI_HTML_HEADER): $(UI_EMBEDDED_HTML)
	mkdir -p $(dir $@)
	xxd -i -n rototem_ui_html $< > $@

$(APP_VERSION_HEADER): $(APP_VERSION_FILE)
	mkdir -p $(dir $@)
	{ \
		printf '#ifndef ROTOTEM_VERSION_H\n#define ROTOTEM_VERSION_H\n\n'; \
		printf '#define ROTOTEM_VERSION "'; \
		sed 's/\\/\\\\/g; s/"/\\"/g' $< | tr -d '\n'; \
		printf '"\n\n#endif /* ROTOTEM_VERSION_H */\n'; \
	} > $@

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
SKRED_STATIC_LIBS ?= -lm -lpthread

linux: $(LINUX_BINARY)

$(LINUX_BINARY): $(COMMON_SOURCES) $(UI_HTML_HEADER) $(APP_VERSION_HEADER) \
		$(SKRED_API_HEADER) $(SKRED_VERSION_HEADER) \
		$(WEBVIEW_DIR)/webview-gtk.c \
		$(SKRED_API_LIB)
	mkdir -p $(LINUX_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LINUX_CFLAGS) \
		-I$(BUILD_DIR) -I$(SKRED_INCLUDE_DIR) \
		-DWEBVIEW_GTK=1 rototem.c $(MINIZ_SOURCE) \
		$(SKRED_API_LIB) $(LINUX_LIBS) $(SKRED_STATIC_LIBS) \
		-o $@

linux-package: $(LINUX_BINARY)
	rm -rf $(LINUX_PACKAGE_DIR)
	mkdir -p $(LINUX_PACKAGE_DIR)
	cp $(LINUX_BINARY) $(LINUX_PACKAGE_DIR)/
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
MACOS_ARCHIVE := $(APP_NAME)-$(APP_VERSION_SLUG).zip

$(MACOS_BINARY): $(COMMON_SOURCES) $(UI_HTML) $(APP_VERSION_HEADER) \
		$(SKRED_API_HEADER) $(SKRED_VERSION_HEADER) \
		$(WEBVIEW_DIR)/webview-cocoa.c \
		$(SKRED_API_LIB)
	mkdir -p $(MACOS_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -g -ObjC \
		-I$(BUILD_DIR) -I$(SKRED_INCLUDE_DIR) \
		-DOBJC_OLD_DISPATCH_PROTOTYPES=1 -DWEBVIEW_COCOA=1 \
		rototem.c $(MINIZ_SOURCE) \
		-framework WebKit -framework CoreFoundation \
		$(SKRED_API_LIB) $(SKRED_STATIC_LIBS) \
		-o $@

macos-package: $(MACOS_BINARY)
	rm -rf $(MACOS_APP)
	mkdir -p $(MACOS_EXECUTABLES) $(MACOS_RESOURCES)
	cp $(ASSETS_DIR)/Info.plist $(MACOS_CONTENTS)/
	cp $(MACOS_BINARY) $(MACOS_EXECUTABLES)/$(MACOS_EXECUTABLE_NAME)
	cp $(UI_HTML) $(MACOS_RESOURCES)/
	cp $(VOCO_JS) $(MACOS_RESOURCES)/
	mkdir -p $(MACOS_RESOURCES)/$(P5_DIR)
	cp $(P5_JS) $(MACOS_RESOURCES)/$(P5_DIR)/
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
WINDOWS_SKRED_DIST_PLATFORMS := windows-$(WINDOWS_ARCH)
WINDOWS_SKRED_DIST_PREFIX_CANDIDATES := $(foreach root,$(SKRED_DIST_ROOTS),$(foreach platform,$(WINDOWS_SKRED_DIST_PLATFORMS),$(wildcard $(root)/$(platform)/$(SKRED_DIST_NAME))))
WINDOWS_SKRED_DIST_PREFIX ?= $(lastword $(sort $(WINDOWS_SKRED_DIST_PREFIX_CANDIDATES)))
ifneq ($(WINDOWS_SKRED_DIST_PREFIX),)
WINDOWS_SKRED_INCLUDE_DIR ?= $(WINDOWS_SKRED_DIST_PREFIX)/include
WINDOWS_SKRED_LIB_DIR ?= $(if $(wildcard $(WINDOWS_SKRED_DIST_PREFIX)/lib64/libapi.a),$(WINDOWS_SKRED_DIST_PREFIX)/lib64,$(WINDOWS_SKRED_DIST_PREFIX)/lib)
else
WINDOWS_SKRED_INCLUDE_DIR ?= $(SKRED_DIR)/include
WINDOWS_SKRED_LIB_DIR ?= $(SKRED_DIR)/lib/windows
endif
WINDOWS_SKRED_LIB := $(WINDOWS_SKRED_LIB_DIR)/libapi.a
WINDOWS_SKRED_API_HEADER := $(WINDOWS_SKRED_INCLUDE_DIR)/skred/api.h
WINDOWS_SKRED_VERSION_HEADER := $(WINDOWS_SKRED_INCLUDE_DIR)/skred/skred-version.h
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

$(WINDOWS_BINARY): $(COMMON_SOURCES) $(UI_HTML_HEADER) $(APP_VERSION_HEADER) \
		$(WEBVIEW_DIR)/webview-win32.c \
		$(WINDOWS_SKRED_API_HEADER) $(WINDOWS_SKRED_VERSION_HEADER) \
		$(WINDOWS_OPTIONAL_INPUTS)
	mkdir -p $(WINDOWS_BUILD_DIR)
	$(WINDOWS_CC) $(CPPFLAGS) $(CFLAGS) \
		-I$(BUILD_DIR) -I$(WINDOWS_SKRED_INCLUDE_DIR) -I$(WEBVIEW_DIR) \
		-DWEBVIEW_WINAPI=1 rototem.c $(MINIZ_SOURCE) \
		$(WINDOWS_SKRED_LIB) \
		$(WINDOWS_SYSTEM_LIBS) $(WINDOWS_LDFLAGS) -o $@

windows-package: windows
	rm -rf $(WINDOWS_PACKAGE_DIR)
	mkdir -p $(WINDOWS_PACKAGE_DIR)
	cp $(WINDOWS_BINARY) $(WINDOWS_PACKAGE_DIR)/
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
