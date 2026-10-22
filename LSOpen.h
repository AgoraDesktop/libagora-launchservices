/*
 * LSOpen.h — opening: launch an application, or the application that owns an item.
 *
 * Copyright © 2026 Kyle J. Cardoza.
 * SPDX-FileCopyrightText: 2026 Kyle J. Cardoza
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * `LSOpenCFURLRef` IS APPLE'S OWN SIGNATURE AND IS CF-ONLY, so it is transcribed exactly: a URL in, the URL
 * of the application actually launched out, and an OSStatus.
 *
 * **`LSOpenURLsWithRole` TAKES APPLE'S NAME AND A REDUCED SIGNATURE, AND THE REDUCTION IS STATED.** Apple's
 * takes `AEKeyDesc *`, a `const LSApplicationParameters *` — a struct holding an `FSRef *` and an
 * `AppleEvent *` — and a `ProcessSerialNumber *` out-array. Every one of those is Carbon, and this tree has no
 * Carbon: the same reason `LSGetApplicationForURL` drops its `FSRef *`. What is left is what is CF-only — the
 * URLs, the role mask, the launch flags and an array of the applications launched — and the Carbon parameters
 * are gone rather than stubbed.
 *
 * WHAT LAUNCHING MEANS HERE, AND WHAT IT DOES NOT. This SPAWNS the application's process: it resolves the
 * executable through `CFBundleCopyExecutableURL` and calls `posix_spawn`. It does NOT bring the application to
 * the front, it does NOT route a document to an already-running instance, and it does NOT track the process's
 * lifetime — those are ACTIVATION, SINGLE-INSTANCE and the SESSION, and the plan puts them in the `session`
 * component (`WORKSPACE-PLAN.md` §2 and W13). A launched process is therefore a child this library does not
 * reap; the session is what will own it.
 *
 * AND A DOCUMENT OPENS IN ITS DEFAULT APPLICATION, which is the other half of Apple's door: given a document,
 * the type registry's default application is resolved — the same answer `LSOpenCFURLRef` gives on the Mac —
 * and the document's path is passed to it as an argument.
 */
#ifndef LAUNCHSERVICES_LSOPEN_H
#define LAUNCHSERVICES_LSOPEN_H

#include <CoreFoundation/CoreFoundation.h>
#include <LaunchServices/LSInfo.h>

/* Apple's `LSLaunchFlags`, with Apple's own values. They are accepted and NOT YET INTERPRETED, because the
 * behaviours they name — activation, new instances, the Classic environment — are the session's. */
typedef UInt32 LSLaunchFlags;

enum {
	kLSLaunchDefaults             = 0x00000001,
	kLSLaunchAndPrint             = 0x00000002,
	kLSLaunchReserved2            = 0x00000004,
	kLSLaunchReserved3            = 0x00000008,
	kLSLaunchReserved4            = 0x00000010,
	kLSLaunchReserved5            = 0x00000020,
	kLSLaunchAndDisplayErrors     = 0x00000040,
	kLSLaunchInhibitBGOnly        = 0x00000080,
	kLSLaunchDontAddToRecents     = 0x00000100,
	kLSLaunchDontSwitch           = 0x00000200,
	kLSLaunchNoParams             = 0x00000800,
	kLSLaunchAsync                = 0x00010000,
	kLSLaunchStartClassic         = 0x00020000,
	kLSLaunchInClassic            = 0x00040000,
	kLSLaunchNewInstance          = 0x00080000,
	kLSLaunchAndHide              = 0x00100000,
	kLSLaunchAndHideOthers        = 0x00200000,
	kLSLaunchHasUntrustedContents = 0x00400000
};

/* OPEN AN APPLICATION, A DOCUMENT OR A FOLDER. An application bundle is launched; anything else is opened in
 * the application that owns its type, with the item passed as an argument. `outLaunchedURL` receives the
 * application actually launched (+1), and may be NULL. */
OSStatus LSOpenCFURLRef(CFURLRef inURL, CFURLRef *outLaunchedURL);

/* THE SAME, FOR MANY URLS. `outLaunchedApps` receives one application per URL, in order, and may be NULL. */
OSStatus LSOpenURLsWithRole(CFArrayRef inURLs, LSRolesMask inRole, LSLaunchFlags inFlags, CFArrayRef *outLaunchedApps);

#endif /* LAUNCHSERVICES_LSOPEN_H */
