# Skred Vendor Files

This directory contains the externally produced Skred interface consumed by
ro-totem:

- `include/skred/api.h`: public C API;
- `include/skred/skred-version.h`: generated version header;
- `lib/linux/libapi.a`: Linux static library from the maxed dist package; and
- `lib/macos/libapi.a`: macOS static library from the maxed dist package.

The header and both archives should be updated together from the same Skred
build or release so their binary interface remains compatible.
