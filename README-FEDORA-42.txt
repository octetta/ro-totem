sudo dnf install gtk3-devel webkit2gtk4.0-devel
gcc rototem.c -DWEBVIEW_GTK=1 `pkg-config --cflags --libs gtk+-3.0 webkit2gtk-4.0` -o rototem
