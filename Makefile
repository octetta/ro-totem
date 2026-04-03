all: \
  rototem \
  #

rototem: rototem.c
	gcc -ObjC -DOBJC_OLD_DISPATCH_PROTOTYPES=1 rototem.c -DWEBVIEW_COCOA=1 -framework WebKit -o rototem
	cp rototem rototem.app/Contents/MacOS
	cp ui.html rototem.app/Contents/MacOS
	cp rototem.icns rototem.app/Contents/Resources
	cp Info.plist rototem.app/Contents
