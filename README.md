# libagora-launchservices

**Launch Services for Agora** — the type model (Uniform Type Identifiers) and the questions about
applications: what a thing *is*, what opens it, and what it is *called*.

Agora is an installable desktop environment for Linux and the BSDs; see the project README in the parent
directory (`../README.md`). `libagora-launchservices` sits on `libagora-core` (Core Foundation) and **below**
Foundation; the AppKit's `NSWorkspace` is that layer's face over this one.

## Provenance

Written for Agora against Apple's 10.6 `LaunchServices.framework` headers — not extracted from anywhere. See
[`PROVENANCE.md`](PROVENANCE.md).

## Status

**Built, C throughout, over Core Foundation alone, and measured.** The library links Core Foundation and libc
and **nothing else** — no third-party library and no foreign application source:

```
$ readelf -d build/libagora-launchservices.so | grep NEEDED
  Shared library: [libagora-core.so]
  Shared library: [libc.so.6]
```

**The model is Apple's and the application universe is closed.** Types arrive from an Agora bundle's
`Info.plist` — `UTExportedTypeDeclarations`, `UTImportedTypeDeclarations`, `CFBundleDocumentTypes` — and there
is no `.desktop` importer, no MIME database and no `xdg` anywhere in the scan. Agora does not run other
desktops' applications, so a foreign application is not a handler and is not run.

### What is measured, not assumed

`ctest` runs one suite, `tests/launchservices.c`, over a fixture tree of **three Agora bundles** and no
`.desktop` file. **19 checks, 0 fail**:

- a filename extension resolves to the type its declaring bundle gave it, and that type conforms to
  `public.text` and `public.data` through the hierarchy (and `public.data` does **not** conform to
  `public.image`);
- the declared MIME tag is found; a built-in type (`public.png`) is matched case-insensitively and conforms
  to `public.image`;
- an application naming `public.image` is offered for a PNG **through conformance**, not identity;
- the `Kind` string is the declared description (`"Writer Document"`), not the extension;
- `Open With` returns the owner and the viewer, **owner first**, and `LSGetApplicationForURL` answers with the
  owner — until an explicit default handler overrides it;
- a legacy `CFBundleTypeExtensions` claim is honoured, and an extension nothing declares gets a `dyn.`
  identifier rather than none;
- a **moved bundle is re-found after a rescan**, its old path is gone, and the default application follows it.

## The doors

**`UTType.h`** — `UTTypeCreatePreferredIdentifierForTag`, `UTTypeCreateAllIdentifiersForTag`,
`UTTypeCopyPreferredTagWithClass`, `UTTypeConformsTo`, `UTTypeEqual`, `UTTypeCopyDeclaration`,
`UTTypeCopyDescription`, `UTTypeCopyDeclaringBundleURL`, and the four tag classes.

**`LSInfo.h`** — `LSRegisterURL`, `LSGetApplicationForURL`, `LSCopyApplicationURLsForURL`,
`LSCopyKindStringForURL`, the `LSCopy`/`LSSet` default-handler pairs, and the Agora-owned
`AGLaunchServicesReset`.

### What is deliberately not here

- **No launch.** *Opening* an item (`LSOpenCFURLRef`) belongs to the session layer; this library answers
  *which* application and stops there.
- **No daemon.** The scan is once, lazily, on the first question; `AGLaunchServicesReset` is how a caller that
  changed the world says so. macOS rescans from a daemon; Agora will not grow one until a second consumer
  wants it.
- **Default handlers are not persisted yet.** They live for the process; where the "Change All…" store is
  written is an open question.
- **No `.desktop`, no MIME database, no `xdg-open`** — named here so their absence is a decision and not an
  omission (`../WORKSPACE-PLAN.md` §2, F1).

## Build and run

```sh
cmake -S . -B build -DCMAKE_C_COMPILER=/usr/bin/clang-19 \
      -DCMAKE_PREFIX_PATH=../build-prefix -DCMAKE_INSTALL_PREFIX=../build-prefix
cmake --build build
cd build && ctest --output-on-failure
```

Its one dependency is the tree's own Core Foundation (`libagora-core`); it links no system library of its
own and vendors nothing.

## Licence

MIT — see [`LICENSE`](LICENSE).
