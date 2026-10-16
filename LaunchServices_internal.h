/*
 * LaunchServices_internal.h — what the two implementation files say to each other.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NOT INSTALLED AND NOT PUBLIC. `UTType.c` owns the type registry; `LaunchServices.c` owns the application
 * registry and the scan. Each needs the other — a type is declared by an application, and an application is
 * described in terms of the types it opens — so the seam between them is here, and it is small on purpose.
 */
#ifndef LAUNCHSERVICES_INTERNAL_H
#define LAUNCHSERVICES_INTERNAL_H

#include <CoreFoundation/CoreFoundation.h>
#include <stdbool.h>

/* ---------------------------------------------------------------------------------------------------
 * the type registry (UTType.c)
 * --------------------------------------------------------------------------------------------------- */

/* Seed the built-in types. Idempotent, and it does NOT scan applications — the application scan lives
 * below and calls back into `_AGUTTypeRegisterDeclaration`. */
void _AGUTTypeInitialize(void);

/* Tear the registry down and re-seed it. Agora-owned, for `AGLaunchServicesReset`. */
void _AGUTTypeReset(void);

/* Add one declaration (an `Info.plist` `UTExportedTypeDeclarations` or `UTImportedTypeDeclarations`
 * entry) to the registry, recording the bundle that declared it. */
void _AGUTTypeRegisterDeclaration(CFDictionaryRef declaration, CFURLRef bundleURL);

/* Does `uti` conform to `super`, directly or through the hierarchy? This is the public `UTTypeConformsTo`
 * with no initialisation and no retained result, for callers inside the library. */
bool _AGUTTypeConforms(CFStringRef uti, CFStringRef super);

/* The user-visible description of a type, +1. */
CFStringRef _AGUTTypeCopyDescription(CFStringRef uti);

/* The type of a filesystem item: a folder, an application bundle, or else by filename extension. +1, and
 * never NULL — an item always has a type. */
CFStringRef _AGUTTypeForURL(CFURLRef url);

/* ---------------------------------------------------------------------------------------------------
 * the application registry (LaunchServices.c)
 * --------------------------------------------------------------------------------------------------- */

/* Bring the whole service up: seed the types, then scan the application directories. Idempotent. Every
 * public door calls this first, because a type or a handler is only answerable once the applications that
 * declare them have been read. */
void _AGLSInitialize(void);

/* Tear everything down and bring it up again — the body of `AGLaunchServicesReset`. */
void _AGLSReset(void);

#endif /* LAUNCHSERVICES_INTERNAL_H */
