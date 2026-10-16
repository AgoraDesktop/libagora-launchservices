# Provenance

**First-party: written for Agora, not extracted from anywhere.** There is no upstream revision to pin and no
third-party code to license.

| what | where |
|---|---|
| the guide | Apple's 10.6 SDK headers for `LaunchServices.framework` — `LSInfo.h`, `UTType.h`, `UTCoreTypes.h` |
| the model | Apple's: a **UTI registry** with a conformance hierarchy and tag declarations, fed by an Agora bundle's `Info.plist` |
| third-party code | **none** |
| licence | MIT, per file (`LICENSE`) |

## What was checked against the headers, and what was not

**The doors were checked against the 10.6 SDK's `LSInfo.h` before the surface was written.** Present at the
era: `LSGetApplicationForURL` (`10.2+`), `LSCopyApplicationURLsForURL` (`10.3+`), `LSCopyKindStringForURL`
(`10.0+`), `LSCopyDefaultRoleHandlerForContentType` / `LSSetDefaultRoleHandlerForContentType` (`10.4+`).
**Absent, and previously cited here by mistake:** `LSCopyDefaultApplicationURLForURL` is 10.10, not 10.6 — the
error was caught by reading the two SDKs' `LSInfo.h` side by side and is recorded in `../WORKSPACE-PLAN.md` §2.

**AND A SECOND VALUE WAS WRONG IN THE OTHER DIRECTION.** `LSInfo.h` carried
`kLSApplicationNotFoundErr = -10811`; Apple's `-10811` is `kLSNotAnApplicationErr`, and
`kLSApplicationNotFoundErr` is **-10814**. The status enum is now transcribed from `LSInfo.h` rather than
recalled.

**AND ONE DOOR THIS COMPONENT NEEDED IS BROKEN UNDERNEATH IT.** `CFBundleCopyExecutableURL` answers NULL for a
MODERN `.app` in this tree's Core Foundation — **MEASURED**, with the real `Workspace.app` as well as a fixture —
so `LSOpen.c` resolves the executable from `CFBundleExecutable` and the two layouts Apple documents instead.
The fix belongs in `libagora-core`, and the workaround is named here so it is not mistaken for the design.
`LSOpenCFURLRef` is Apple's signature exactly; `LSOpenURLsWithRole` takes Apple's name with the Carbon
parameters dropped, said at its door.

**The one deviation is at its door and stated there.** Apple's `LSGetApplicationForURL` takes an `FSRef *`
out-parameter, and `FSRef` is Carbon, which Agora does not have and will not grow. The door keeps Apple's
name, arguments and status result and drops only that out-parameter.

**The `dyn.` encoding is this tree's.** Apple's is private, so a dynamic identifier is seven base-32
characters of an FNV-1a hash over the tag class and tag. It is stable for the same input within and across
runs; that is all a caller may rely on, and it is said in `UTType.h` as well as here.

## Licence per file

Every file is MIT. There is no third-party code in this component, so there is no per-file licence split to
record — unlike the components that translate a permissive library, whose `PROVENANCE.md` exists to pin a
revision and a licence **per file**.
