# Define paths
APP_NAME = ro-totem.app
CONTENTS = $(APP_NAME)/Contents
MACOS    = $(CONTENTS)/MacOS
RESOURCES = $(CONTENTS)/Resources
SHARE    = ro-totem-easter-2026.zip
ASSETS = assets

all: bundle

linux-bundle:
	gcc rototem.c -DWEBVIEW_GTK=1 `pkg-config --cflags --libs gtk+-3.0 webkit2gtk-4.0` -o rototem

rototem_bin: rototem.c
	gcc -g -ObjC -DOBJC_OLD_DISPATCH_PROTOTYPES=1 rototem.c \
		-DWEBVIEW_COCOA=1 -framework WebKit -framework CoreFoundation \
		-o rototem_bin

bundle: rototem_bin
	# 1. Start fresh
	rm -rf $(APP_NAME)
	mkdir -p $(MACOS) $(RESOURCES)

	# 2. Populate structure
	cp $(ASSETS)/Info.plist $(CONTENTS)/
	cp rototem_bin $(MACOS)/rototem
	cp ui.html $(RESOURCES)/
	cp $(ASSETS)/mini-skred $(RESOURCES)/
	cp $(ASSETS)/rototem.icns $(RESOURCES)/
	chmod +x $(MACOS)/rototem $(RESOURCES)/mini-skred

	# 3. Strip all attributes BEFORE signing
	# This removes the "downloaded from internet" flag and stale ACLs
	xattr -cr $(APP_NAME)

	# 4. Ad-hoc sign the whole bundle deep
	codesign --force --deep --sign - $(APP_NAME)

	# 5. Clean and Zip
	rm -f $(SHARE)
	zip -r -y $(SHARE) $(APP_NAME)

clean:
	rm -rf $(APP_NAME) rototem_bin $(SHARE)