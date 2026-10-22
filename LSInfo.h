/*
 * LSInfo.h — Launch Services' questions about applications: what opens this, and what is it called.
 *
 * Copyright © 2026 Kyle J. Cardoza.
 * SPDX-FileCopyrightText: 2026 Kyle J. Cardoza
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Portions of this file may derive from GNUstep; see THIRD-PARTY.md.
 *
 * THE APPLICATION REGISTRY IS AGORA BUNDLES ONLY (WORKSPACE-PLAN.md §2, F1). Agora does not run other
 * desktops' applications, so the scan reads `.app` bundles whose `Info.plist` declares document types and
 * URL schemes the Apple way, and it reads nothing else. There is no `.desktop` importer and no `xdg-open`.
 *
 * THE ONE DEVIATION, STATED AT THE DOOR. Apple's 10.6 `LSGetApplicationForURL` takes an `FSRef *outAppRef`
 * out-parameter, and `FSRef` is Carbon, which Agora does not have and will not grow. The door here keeps
 * Apple's NAME, its arguments and its status result and drops only that out-parameter; the URL it already
 * returns is the whole answer. Everything else in this header is Apple's own signature.
 */
#ifndef LAUNCHSERVICES_LSINFO_H
#define LAUNCHSERVICES_LSINFO_H

#include <CoreFoundation/CoreFoundation.h>

/* ============================================================================================ *
 * THE ROLE MASK, WITH APPLE'S OWN BIT VALUES. `kLSRolesNone` is a bit with a name rather than zero,  *
 * which is Apple's and not a slip: a document type may say "this application does not open it".        *
 * ============================================================================================ */
enum {
	kLSRolesNone	= 0x00000001,
	kLSRolesViewer	= 0x00000002,
	kLSRolesEditor	= 0x00000004,
	kLSRolesShell	= 0x00000008,
	kLSRolesAll	= 0xffffffff
};
typedef UInt32 LSRolesMask;

/* ============================================================================================ *
 * APPLE'S OWN STATUS VALUES, WITH APPLE'S OWN VALUES — measured from the 10.6 `LSInfo.h` AFTER     *
 * this header had one of them wrong: it carried `kLSApplicationNotFoundErr = -10811`, and -10811 is  *
 * `kLSNotAnApplicationErr`; Apple's `kLSApplicationNotFoundErr` is **-10814**. The values are now   *
 * transcribed rather than recalled.                                                                  *
 * ============================================================================================ */
enum {
	kLSAppInTrashErr            = -10660,
	kLSUnknownErr               = -10810,
	kLSNotAnApplicationErr      = -10811,
	kLSDataUnavailableErr       = -10813,
	kLSApplicationNotFoundErr   = -10814,
	kLSUnknownTypeErr           = -10815,
	kLSLaunchInProgressErr      = -10818,
	kLSServerCommunicationErr   = -10822,
	kLSNoLaunchPermissionErr    = -10826,
	kLSNoExecutableErr          = -10827
};

/* ============================================================================================ *
 * THE DOORS.                                                                                          *
 * ============================================================================================ */

/* REGISTER ONE APPLICATION from its bundle URL: read its `Info.plist`, add its exported and imported type
 * declarations to the UTI registry, and record which content types and URL schemes it claims. */
OSStatus LSRegisterURL(CFURLRef inURL, Boolean inUpdate);

/* THE APPLICATION THAT OPENS AN ITEM. The item's type is taken from the filesystem — a directory is a
 * folder, an application bundle is an application bundle, a file is typed by its extension — and the
 * application that OWNS that type is preferred over one that merely views it. */
OSStatus LSGetApplicationForURL(CFURLRef inURL, LSRolesMask inRoleMask, CFURLRef *outAppURL);

/* EVERY application that opens the item — the "Open With" menu, owners first. */
CFArrayRef LSCopyApplicationURLsForURL(CFURLRef inURL, LSRolesMask inRoleMask);

/* **THE APPLICATION BUNDLES THAT DECLARE THIS IDENTIFIER** — Apple's door, and the one a shell asks when it
 * knows an application by name rather than by file. The window server publishes a window's `app_id`, which is
 * the bundle identifier; this turns that back into the bundle on disk, which is what a name and an icon are read
 * from. Apple's signature takes an `outError`; Agora's registry cannot fail to answer, so it is accepted and left
 * alone, and an identifier nothing declares gets an EMPTY ARRAY rather than NULL — the same answer
 * `LSCopyApplicationURLsForURL` gives, and one a caller can iterate without a special case. */
CFArrayRef LSCopyApplicationURLsForBundleIdentifier(CFStringRef inBundleIdentifier, CFErrorRef *outError);

/* THE "Kind" STRING for an item's type: "JPEG image", "Folder", "Writer Document". */
CFStringRef LSCopyKindStringForURL(CFURLRef inURL);

/* THE DEFAULT HANDLER BINDINGS, which is the user's "Change All…" choice. Read and write are here; where
 * they are PERSISTED is not yet decided (see the component README) and today they live for the process. */
CFStringRef LSCopyDefaultRoleHandlerForContentType(CFStringRef inContentType, LSRolesMask inRoleMask);
OSStatus LSSetDefaultRoleHandlerForContentType(CFStringRef inContentType, LSRolesMask inRoleMask, CFStringRef inHandlerBundleID);
CFStringRef LSCopyDefaultHandlerForURLScheme(CFStringRef inURLScheme);
OSStatus LSSetDefaultHandlerForURLScheme(CFStringRef inURLScheme, CFStringRef inHandlerBundleID);

/* ============================================================================================ *
 * AN AGORA-OWNED DOOR, AND WHY IT HAS TO EXIST.                                                       *
 *                                                                                                     *
 * macOS has a Launch Services DAEMON that watches the application directories and rescans on its own.   *
 * Agora has no such daemon yet, so the scan happens once, lazily, on the first question — and a caller   *
 * that has changed the world (installed an application, moved one) must say so. This is that door. The   *
 * `AG` prefix is deliberate: it is Agora's, not Apple's, and it does not pretend otherwise. Apple's own  *
 * `LSRegisterURL` remains the door for a single application.                                            *
 * ============================================================================================ */
void AGLaunchServicesReset(void);

#endif /* LAUNCHSERVICES_LSINFO_H */
