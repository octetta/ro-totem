# Skred Vendor Files

This directory contains the externally produced Skred interface consumed by
ro-totem:

- `include/api.h`: public C API;
- `lib/linux/libapi.a`: Linux static library; and
- `lib/macos/libapi.a`: macOS static library.

The header and both archives should be updated together from the same Skred
build or release so their binary interface remains compatible.
