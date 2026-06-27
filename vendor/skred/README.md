# Skred Vendor Files

This directory contains the externally produced Skred interface consumed by
ro-totem:

- `include/skred/api.h`: public C API;
- `include/skred/skred-version.h`: generated version header;
- `lib/linux/libapi.a`: Linux static library from the maxed dist package; and
- `lib/macos/libapi.a`: macOS static library from the maxed dist package.

The header and both archives should be updated together from the same Skred
build or release so their binary interface remains compatible.

The Makefile also accepts Pulp's newer platform-scoped dist layout without
renormalizing it:

```text
vendor/skred/dist/linux-x86_64/skred-0.24.0-maxed/include/skred/api.h
vendor/skred/dist/linux-x86_64/skred-0.24.0-maxed/lib64/libapi.a
vendor/skred/dist/darwin-arm64/skred-0.24.0-maxed/include/skred/api.h
vendor/skred/dist/darwin-arm64/skred-0.24.0-maxed/lib/libapi.a
```

The `dist/` component may be omitted if only the platform directory is copied
under `vendor/skred`. Use `make skred-paths` to see which package the build
selected.

To pull an API package from the Pulp repository explicitly:

```sh
make pulp-api
make pulp-api PULP_VERSION=0.24.0 PULP_PLATFORM=linux-x86_64
```

`PULP_VERSION=latest` is the default and reads Pulp's current `VERSION` file.
Fetched packages are installed under `vendor/skred/dist/<platform>/`.

When more than one package is installed, pin the build with:

```sh
make linux SKRED_VERSION=0.24.0
make skred-paths SKRED_VERSION=0.24.0
```
