# Webview Vendor Sources

This directory contains the third-party webview abstraction and its GTK,
Cocoa, and Windows backends. Application code should include `webview.h`
through the `vendor/webview/` path and should not mix project-specific behavior
into these files.

The Windows backend references `WebView2.h` and `webview-win32-edge.c`, which
are not included in this repository. Windows builds require those external
WebView2 components.
