/*
 * UTType.h — Uniform Type Identifiers: Apple's type model, keyed the way Apple keys it.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE MODEL IS APPLE'S AND ONLY APPLE'S (WORKSPACE-PLAN.md §2, F1). A type is a reverse-DNS identifier —
 * `public.jpeg`, `com.apple.application-bundle` — with a CONFORMANCE HIERARCHY above it and a set of TAGS
 * below it: a filename extension, a MIME type, an OSType. Types arrive from an Agora bundle's `Info.plist`,
 * in `UTExportedTypeDeclarations` (types the bundle defines) and `UTImportedTypeDeclarations` (types it
 * understands), and from nowhere else. Agora does not run other desktops' applications, so there is NO
 * `.desktop` importer, NO MIME database and NO `xdg` — see the component README, and `README.md`'s
 * "No systemd" and "application universe is closed" conventions in the parent tree.
 *
 * WHAT IS AND IS NOT HERE. The registry, the conformance walk and the tag lookups are implemented. A type
 * with no declaration anywhere is given a `dyn.` identifier — Apple's own fallback — whose exact encoding
 * is THIS TREE'S (Apple's is private), stated rather than implied. The seed set below is a subset of
 * Apple's `UTCoreTypes.h`: the types an Agora desktop actually names, each under the identifier Apple
 * gives it.
 */
#ifndef LAUNCHSERVICES_UTTYPE_H
#define LAUNCHSERVICES_UTTYPE_H

#include <CoreFoundation/CoreFoundation.h>

/* A UTI IS A CFString, which is Apple's own decision and the reason these doors take and return one. */
typedef CFStringRef UTTypeRef;

/* ============================================================================================ *
 * THE FOUR TAG CLASSES. Each is a string, and each is the SAME string Apple uses for the key under  *
 * `UTTypeTagSpecification`: a declaration and a lookup name the class one way, so the two cannot    *
 * drift.                                                                                             *
 * ============================================================================================ */
extern const CFStringRef kUTTagClassFilenameExtension;	/* "public.filename-extension" */
extern const CFStringRef kUTTagClassMIMEType;		/* "public.mime-type"          */
extern const CFStringRef kUTTagClassNSPboardType;	/* "com.apple.nspboard-type"   */
extern const CFStringRef kUTTagClassOSType;		/* "com.apple.ostype"          */

/* ============================================================================================ *
 * THE BUILT-IN TYPES. A subset of Apple's `UTCoreTypes.h`; each name carries Apple's identifier.      *
 * ============================================================================================ */
extern const CFStringRef kUTTypeItem;			/* public.item                     */
extern const CFStringRef kUTTypeContent;		/* public.content                  */
extern const CFStringRef kUTTypeData;			/* public.data                     */
extern const CFStringRef kUTTypeText;			/* public.text                     */
extern const CFStringRef kUTTypePlainText;		/* public.plain-text               */
extern const CFStringRef kUTTypeUTF8PlainText;		/* public.utf8-plain-text          */
extern const CFStringRef kUTTypeRTF;			/* public.rtf                      */
extern const CFStringRef kUTTypeHTML;			/* public.html                     */
extern const CFStringRef kUTTypeXML;			/* public.xml                      */
extern const CFStringRef kUTTypeSourceCode;		/* public.source-code              */
extern const CFStringRef kUTTypeImage;			/* public.image                    */
extern const CFStringRef kUTTypeJPEG;			/* public.jpeg                     */
extern const CFStringRef kUTTypePNG;			/* public.png                      */
extern const CFStringRef kUTTypeTIFF;			/* public.tiff                     */
extern const CFStringRef kUTTypeGIF;			/* public.gif                      */
extern const CFStringRef kUTTypePDF;			/* public.pdf                      */
extern const CFStringRef kUTTypeAudio;			/* public.audio                    */
extern const CFStringRef kUTTypeMP3;			/* public.mp3                      */
extern const CFStringRef kUTTypeMPEG4Audio;		/* public.mpeg-4-audio             */
extern const CFStringRef kUTTypeMovie;			/* public.movie                    */
extern const CFStringRef kUTTypeMPEG4;			/* public.mpeg-4                   */
extern const CFStringRef kUTTypeDirectory;		/* public.directory                */
extern const CFStringRef kUTTypeFolder;			/* public.folder                   */
extern const CFStringRef kUTTypeVolume;			/* public.volume                   */
extern const CFStringRef kUTTypeBundle;			/* com.apple.bundle                */
extern const CFStringRef kUTTypePackage;		/* com.apple.package               */
extern const CFStringRef kUTTypeApplication;		/* com.apple.application           */
extern const CFStringRef kUTTypeApplicationBundle;	/* com.apple.application-bundle    */
extern const CFStringRef kUTTypeExecutable;		/* public.executable               */
extern const CFStringRef kUTTypeUnixExecutable;		/* public.unix-executable          */
extern const CFStringRef kUTTypeURL;			/* public.url                      */
extern const CFStringRef kUTTypeFileURL;		/* public.file-url                 */
extern const CFStringRef kUTTypeSymLink;		/* public.symlink                  */
extern const CFStringRef kUTTypeAliasFile;		/* public.alias                    */
extern const CFStringRef kUTTypeArchive;		/* public.archive                  */
extern const CFStringRef kUTTypeZipArchive;		/* public.zip-archive              */
extern const CFStringRef kUTTypeTarArchive;		/* public.tar-archive              */

/* ============================================================================================ *
 * THE DOORS.                                                                                          *
 * ============================================================================================ */

/* THE PREFERRED TYPE FOR A TAG, and the one Apple's callers ask for most. `inConformingToUTI` narrows the
 * answer to a type that conforms to it — `UTTypeCreatePreferredIdentifierForTag(kUTTagClassFilenameExtension,
 * CFSTR("jpg"), kUTTypeImage)`. A tag nothing declares yields a `dyn.` identifier rather than NULL, which is
 * Apple's own contract: a file always has a type. */
CFStringRef UTTypeCreatePreferredIdentifierForTag(CFStringRef inTagClass, CFStringRef inTag, CFStringRef inConformingToUTI);

/* EVERY type carrying the tag, optionally narrowed by conformance. */
CFArrayRef UTTypeCreateAllIdentifiersForTag(CFStringRef inTagClass, CFStringRef inTag, CFStringRef inConformingToUTI);

/* THE PREFERRED TAG OF A CLASS FOR A TYPE — `public.jpeg`'s `kUTTagClassMIMEType` is "image/jpeg". A type
 * with no tag of its own is asked through its conformance chain. NULL when neither has one. */
CFStringRef UTTypeCopyPreferredTagWithClass(CFStringRef inUTI, CFStringRef inTagClass);

/* CONFORMANCE, walked transitively: `public.jpeg` conforms to `public.data` through `public.image`. */
Boolean UTTypeConformsTo(CFStringRef inUTI, CFStringRef inConformsToUTI);

Boolean UTTypeEqual(CFStringRef inUTI1, CFStringRef inUTI2);

/* THE DECLARATION, as a dictionary — `UTTypeIdentifier`, `UTTypeDescription`, `UTTypeConformsTo`,
 * `UTTypeTagSpecification` — or NULL for a dynamic type no bundle declared. */
CFDictionaryRef UTTypeCopyDeclaration(CFStringRef inUTI);

/* THE USER-VISIBLE NAME, which is what the Finder's `Kind` column shows. */
CFStringRef UTTypeCopyDescription(CFStringRef inUTI);

/* THE BUNDLE THAT DECLARED IT, or NULL for a built-in or dynamic type. */
CFURLRef UTTypeCopyDeclaringBundleURL(CFStringRef inUTI);

#endif /* LAUNCHSERVICES_UTTYPE_H */
