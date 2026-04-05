all: \
  rototem \
  #

LDFLAGS += -framework CoreFoundation

SHARE = rototem-easter-egg.zip

rototem: rototem.c
	gcc -ObjC -DOBJC_OLD_DISPATCH_PROTOTYPES=1 rototem.c -DWEBVIEW_COCOA=1 -framework WebKit -framework CoreFoundation -o rototem
	cp rototem rototem.app/Contents/MacOS
	cp ui.html rototem.app/Contents/Resources
	cp mini-skred rototem.app/Contents/Resources
	chmod +x rototem.app/Contents/Resources/mini-skred
	cp rototem.icns rototem.app/Contents/Resources
	cp Info.plist rototem.app/Contents
	xattr -cr rototem.app
	codesign --force --deep --sign - rototem.app
	rm -rf $(SHARE)
	zip -r -y $(SHARE) rototem.app
