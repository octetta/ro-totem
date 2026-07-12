# Skred VFS Integration Notes

These are notes for adopting the newer Skred API zip/VFS support in ro-totem.
They are not an implementation plan that must happen all at once.

## Current ro-totem shape

- ro-totem owns the project ZIP format: `settings.json`, WAV files under
  `waves/`, and managed files under `files/`.
- The C host validates project archive names, sizes, duplicate entries, and
  required settings before the UI sees a loaded project.
- Loaded project ZIPs are currently extracted to an app-owned temporary
  directory. The UI rewrites project-relative paths into real temporary paths
  before calling the normal wave/settings restore flow.
- File-backed wave loading is centralized in C. JavaScript calls
  `native.loadWave(voice, path)`, and C emits the Skode `/ws` commands.
- ro-totem compiles its own `vendor/miniz/miniz.c` for project ZIP read/write.

## What the updated Skred API provides

The updated API exposes Skred VFS functions through `skred/api.h`, including:

- `skred_vfs_mount(path)` to mount a directory or ZIP file.
- `skred_vfs_mount_zip_memory(data, size, label)` for in-memory ZIP data.
- `skred_vfs_unmount()` to return to ordinary disk-backed behavior.
- `skred_vfs_read_file(path, &data, &size)` for reading from the active VFS.
- `skred_vfs_read_real_file(path, &data, &size)` for explicitly reading disk.
- `skred_vfs_free_file(data)` for buffers returned by the read helpers.

Skode file-loading commands now use the VFS/search layer. Project-relative
paths can therefore resolve from a mounted ZIP. Real filesystem paths can still
be forced with the `file:` prefix.

## Best first adoption

The cleanest first use is to stop extracting project assets that Skred itself
can load.

On project load:

1. Keep ro-totem's current ZIP validation.
2. Mount the accepted project archive with `skred_vfs_mount(project_zip_path)`.
3. Send `loadProjectFromText` a VFS root marker or the archive filename instead
   of a temporary extraction directory.
4. Keep project wave paths as `waves/name.wav`.
5. Keep managed file paths as `files/name`.
6. When the user accepts the project, keep the mount active.
7. When the user rejects, closes, or loads another project, call
   `skred_vfs_unmount()`.

Then `load_wave_channel()` can keep emitting:

```text
[waves/name.wav] /ws...
```

and Skred will read from the mounted project ZIP.

## Real files while a project ZIP is mounted

When loading a user-chosen real file while a ZIP is mounted, pass a `file:`
path into Skode:

```text
[file:/absolute/path/to/take.wav] /ws...
```

This bypasses the mounted ZIP lookup. ro-totem can do this in C for file dialog
results, while leaving project-relative paths unchanged.

## Managed file references

The current `{{file:name}}` UI feature can become simpler for loaded projects.
Instead of resolving to a temporary extracted path, it can resolve to:

```text
files/name
```

That allows `%cat`, `/ks`, `/k`, `/ls`, and other Skode file loaders to use
the mounted project ZIP directly. For newly chosen external files that are not
yet in a saved project, keep resolving to `file:/absolute/path` or the plain
absolute path depending on whether the command should bypass the mount.

## What not to replace yet

Do not replace ro-totem's project writer immediately. ro-totem still owns:

- project schema and settings migration;
- archive path validation;
- basename deduping;
- project save UI flow;
- managed-file metadata;
- size and count limits;
- write/finalize/replace behavior.

The Skred VFS is useful as a read/load layer. It is not yet a ro-totem project
archive writer.

## Integration risk: miniz symbols

The updated Skred static library includes zip/miniz code. ro-totem also links
`vendor/miniz/miniz.c`.

That can create duplicate or ambiguous `mz_*`, `tinfl_*`, and `tdefl_*`
symbols when `vendor/skred` is updated. Before relying on the newer API, choose
one of these directions:

- Preferable long term: expose enough Skred zip/project helpers that ro-totem
  can stop compiling its own miniz.
- Short term: keep ro-totem's miniz for project writing, but verify link
  behavior carefully after updating `vendor/skred`.
- Alternative: namespace one copy of miniz, though that is noisier than sharing
  a single zip implementation.

This is the first thing to check when updating the vendored Skred distribution.

## Webview assets

ro-totem currently embeds UI HTML on Linux/Windows and loads bundled `ui.html`
on macOS. Skred VFS could read webview assets from a ZIP, but webview will not
automatically serve mounted ZIP paths as URLs.

Possible future approaches:

- Keep the current embedded/bundled UI path for the core app.
- Use `skred_vfs_read_file()` to load optional UI assets or presets from a
  mounted project ZIP.
- If packaging the UI itself in a ZIP, read `ui.html` from VFS and hand the
  resulting bytes to webview as an HTML string or data URL.

This is useful, but less urgent than project asset loading.

