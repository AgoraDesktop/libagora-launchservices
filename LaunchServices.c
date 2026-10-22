/*
 * LaunchServices.c — the application registry: what opens this, and what is it called.
 *
 * Copyright © 2026 Kyle J. Cardoza.
 * SPDX-FileCopyrightText: 2026 Kyle J. Cardoza
 * SPDX-License-Identifier: LGPL-3.0-or-later
 * Portions of this file may derive from GNUstep; see THIRD-PARTY.md.
 *
 * THE APPLICATION UNIVERSE IS CLOSED (WORKSPACE-PLAN.md §2, F1). The scan reads `.app` bundles — this
 * desktop's own applications — and their `Info.plist`, and it reads NOTHING ELSE: no `.desktop` file, no
 * MIME database, no `mimeapps.list`, no `xdg-open`. A foreign application is not a handler and is not run.
 *
 * A BUNDLE'S `Info.plist` IS READ THE APPLE WAY: `CFBundleDocumentTypes` (with `LSItemContentTypes`, or the
 * legacy extension/MIME/OSType arrays), `UTExportedTypeDeclarations` / `UTImportedTypeDeclarations`, and
 * `CFBundleURLTypes`. The declarations go straight into the UTI registry in `UTType.c`, so a type an
 * application declares is a type every caller can ask about.
 */
#include "LSInfo.h"
#include "UTType.h"
#include "LaunchServices_internal.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================================ *
 * STATE                                                                                               *
 * ============================================================================================ */
typedef struct AGApplication {
	CFURLRef                url;
	CFStringRef             bundleID;	/* NULL when the Info.plist omits it */
	CFMutableDictionaryRef  contentTypes;	/* UTI -> CFNumber(LSRolesMask) */
	CFMutableArrayRef       ownedTypes;	/* UTI declared by THIS bundle (UTExportedTypeDeclarations) */
	CFMutableArrayRef       schemes;	/* URL schemes */
} AGApplication;

static AGApplication          **g_apps           = NULL;
static size_t                   g_appCount       = 0;
static size_t                   g_appCap         = 0;
static CFMutableDictionaryRef   g_defaultHandlers = NULL;	/* UTI -> bundle id */
static CFMutableDictionaryRef   g_schemeHandlers  = NULL;	/* scheme -> bundle id */
static bool                     g_initialized     = false;

static CFTypeRef get(CFDictionaryRef d, const char *key)
{
	CFStringRef k = CFStringCreateWithCString(NULL, key, kCFStringEncodingUTF8);
	CFTypeRef v = CFDictionaryGetValue(d, k);
	CFRelease(k);
	return v;
}

static bool arrayContains(CFArrayRef a, CFTypeRef v)
{
	for (CFIndex i = 0; i < CFArrayGetCount(a); i++)
		if (CFEqual(CFArrayGetValueAtIndex(a, i), v)) return true;
	return false;
}

static CFArrayRef stringsFromValue(CFTypeRef v)
{
	if (v == NULL) return NULL;
	if (CFGetTypeID(v) == CFStringGetTypeID()) {
		CFMutableArrayRef a = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
		CFArrayAppendValue(a, v);
		return a;
	}
	if (CFGetTypeID(v) == CFArrayGetTypeID()) {
		CFArrayRef arr = (CFArrayRef)v;
		CFMutableArrayRef out = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
		for (CFIndex i = 0; i < CFArrayGetCount(arr); i++) {
			CFTypeRef e = CFArrayGetValueAtIndex(arr, i);
			if (CFGetTypeID(e) == CFStringGetTypeID()) CFArrayAppendValue(out, e);
		}
		return out;
	}
	return NULL;
}

static LSRolesMask roleMaskForString(CFStringRef s)
{
	if (s != NULL && CFGetTypeID(s) == CFStringGetTypeID()) {
		if (CFStringCompare(s, CFSTR("Editor"), 0) == kCFCompareEqualTo) return kLSRolesEditor;
		if (CFStringCompare(s, CFSTR("Viewer"), 0) == kCFCompareEqualTo) return kLSRolesViewer;
		if (CFStringCompare(s, CFSTR("Shell"),  0) == kCFCompareEqualTo) return kLSRolesShell;
		if (CFStringCompare(s, CFSTR("None"),   0) == kCFCompareEqualTo) return kLSRolesNone;
	}
	return kLSRolesViewer;			/* Apple's default when a document type says nothing */
}

/* ============================================================================================ *
 * READING ONE BUNDLE                                                                                  *
 * ============================================================================================ */

/* The Info.plist, through Core Foundation's bundle door, and — if that answers nothing — the two layouts
 * read directly. A bundle being registered is one the caller has just named, so the fallback is not a
 * guess about where a bundle lives; it is the two places Apple documents and no others. */
static CFDictionaryRef copyInfoPlist(CFURLRef appURL)
{
	CFDictionaryRef info = CFBundleCopyInfoDictionaryForURL(appURL);
	if (info != NULL) {
		if (CFGetTypeID(info) == CFDictionaryGetTypeID()) return info;
		CFRelease(info);
	}
	CFStringRef path = CFURLCopyFileSystemPath(appURL, kCFURLPOSIXPathStyle);
	if (path == NULL) return NULL;
	const char *cands[2] = { "Contents/Info.plist", "Info.plist" };
	CFDictionaryRef result = NULL;
	for (int i = 0; i < 2 && result == NULL; i++) {
		CFStringRef full = CFStringCreateWithFormat(NULL, NULL, CFSTR("%@/%s"), path, cands[i]);
		CFURLRef u = CFURLCreateWithFileSystemPath(NULL, full, kCFURLPOSIXPathStyle, false);
		CFRelease(full);
		CFDataRef data = CFURLCreateData(NULL, u, kCFStringEncodingUTF8, false);
		if (data != NULL) {
			CFPropertyListRef pl = CFPropertyListCreateWithData(NULL, data, 0, NULL, NULL);
			if (pl != NULL) {
				if (CFGetTypeID(pl) == CFDictionaryGetTypeID()) result = (CFDictionaryRef)pl;
				else CFRelease(pl);
			}
			CFRelease(data);
		}
		CFRelease(u);
	}
	CFRelease(path);
	return result;
}

static void addContentType(AGApplication *app, CFStringRef uti, LSRolesMask roles)
{
	LSRolesMask existing = 0;
	CFNumberRef cur = (CFNumberRef)CFDictionaryGetValue(app->contentTypes, uti);
	if (cur != NULL) CFNumberGetValue(cur, kCFNumberIntType, &existing);
	LSRolesMask merged = existing | roles;
	CFNumberRef n = CFNumberCreate(NULL, kCFNumberIntType, &merged);
	CFDictionarySetValue(app->contentTypes, uti, n);
	CFRelease(n);
}

static void registerDeclarations(CFDictionaryRef info, CFStringRef key, CFURLRef appURL, AGApplication *app, bool owned)
{
	CFTypeRef v = CFDictionaryGetValue(info, key);
	if (v == NULL || CFGetTypeID(v) != CFArrayGetTypeID()) return;
	CFArrayRef arr = (CFArrayRef)v;
	for (CFIndex i = 0; i < CFArrayGetCount(arr); i++) {
		CFTypeRef e = CFArrayGetValueAtIndex(arr, i);
		if (CFGetTypeID(e) != CFDictionaryGetTypeID()) continue;
		_AGUTTypeRegisterDeclaration((CFDictionaryRef)e, appURL);
		if (owned) {
			CFTypeRef idv = CFDictionaryGetValue((CFDictionaryRef)e, CFSTR("UTTypeIdentifier"));
			if (idv != NULL && CFGetTypeID(idv) == CFStringGetTypeID() && !arrayContains(app->ownedTypes, idv))
				CFArrayAppendValue(app->ownedTypes, idv);
		}
	}
}

static void removeAppForBundleID(CFStringRef bundleID)
{
	for (size_t i = 0; i < g_appCount; i++) {
		AGApplication *a = g_apps[i];
		if (a->bundleID == NULL || !CFEqual(a->bundleID, bundleID)) continue;
		CFRelease(a->url);
		CFRelease(a->bundleID);
		CFRelease(a->contentTypes);
		CFRelease(a->ownedTypes);
		CFRelease(a->schemes);
		free(a);
		memmove(&g_apps[i], &g_apps[i + 1], (g_appCount - i - 1) * sizeof(*g_apps));
		g_appCount--;
		return;
	}
}

static void _AGLSRegisterApplication(CFURLRef appURL)
{
	CFDictionaryRef info = copyInfoPlist(appURL);
	if (info == NULL) return;

	CFStringRef bundleID = (CFStringRef)get(info, "CFBundleIdentifier");
	if (bundleID != NULL && CFGetTypeID(bundleID) != CFStringGetTypeID()) bundleID = NULL;
	if (bundleID != NULL) removeAppForBundleID(bundleID);	/* a moved bundle, re-registered */

	AGApplication *app = calloc(1, sizeof(*app));
	app->url = (CFURLRef)CFRetain(appURL);
	app->bundleID = (bundleID != NULL) ? (CFStringRef)CFRetain(bundleID) : NULL;
	app->contentTypes = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	app->ownedTypes = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
	app->schemes = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);

	/* Declarations BEFORE document types: a document type may name a type this very bundle declares, and
	 * the extension-form below resolves through the registry. */
	registerDeclarations(info, CFSTR("UTExportedTypeDeclarations"), appURL, app, true);
	registerDeclarations(info, CFSTR("UTImportedTypeDeclarations"), appURL, app, false);

	CFTypeRef dts = get(info, "CFBundleDocumentTypes");
	if (dts != NULL && CFGetTypeID(dts) == CFArrayGetTypeID()) {
		CFArrayRef arr = (CFArrayRef)dts;
		for (CFIndex i = 0; i < CFArrayGetCount(arr); i++) {
			CFTypeRef e = CFArrayGetValueAtIndex(arr, i);
			if (CFGetTypeID(e) != CFDictionaryGetTypeID()) continue;
			CFDictionaryRef dt = (CFDictionaryRef)e;
			LSRolesMask roles = roleMaskForString((CFStringRef)get(dt, "CFBundleTypeRole"));

			CFArrayRef utis = stringsFromValue(get(dt, "LSItemContentTypes"));
			if (utis != NULL) {
				for (CFIndex j = 0; j < CFArrayGetCount(utis); j++) addContentType(app, CFArrayGetValueAtIndex(utis, j), roles);
				CFRelease(utis);
			} else {
				struct { const char *key; CFStringRef cls; } legacy[3] = {
					{ "CFBundleTypeExtensions", kUTTagClassFilenameExtension },
					{ "CFBundleTypeMIMETypes",  kUTTagClassMIMEType },
					{ "CFBundleTypeOSTypes",    kUTTagClassOSType },
				};
				for (int L = 0; L < 3; L++) {
					CFArrayRef tags = stringsFromValue(get(dt, legacy[L].key));
					if (tags == NULL) continue;
					for (CFIndex j = 0; j < CFArrayGetCount(tags); j++) {
						CFStringRef u = UTTypeCreatePreferredIdentifierForTag(legacy[L].cls, CFArrayGetValueAtIndex(tags, j), NULL);
						if (u != NULL) { addContentType(app, u, roles); CFRelease(u); }
					}
					CFRelease(tags);
				}
			}
		}
	}

	CFTypeRef uts = get(info, "CFBundleURLTypes");
	if (uts != NULL && CFGetTypeID(uts) == CFArrayGetTypeID()) {
		CFArrayRef arr = (CFArrayRef)uts;
		for (CFIndex i = 0; i < CFArrayGetCount(arr); i++) {
			CFTypeRef e = CFArrayGetValueAtIndex(arr, i);
			if (CFGetTypeID(e) != CFDictionaryGetTypeID()) continue;
			CFArrayRef schemes = stringsFromValue(get((CFDictionaryRef)e, "CFBundleURLSchemes"));
			if (schemes == NULL) continue;
			for (CFIndex j = 0; j < CFArrayGetCount(schemes); j++) {
				CFTypeRef s = CFArrayGetValueAtIndex(schemes, j);
				if (!arrayContains(app->schemes, s)) CFArrayAppendValue(app->schemes, s);
			}
			CFRelease(schemes);
		}
	}

	if (g_appCount == g_appCap) {
		g_appCap = g_appCap ? g_appCap * 2 : 16;
		g_apps = realloc(g_apps, g_appCap * sizeof(*g_apps));
	}
	g_apps[g_appCount++] = app;

	CFRelease(info);
}

static AGApplication *findAppByBundleID(CFStringRef bundleID)
{
	for (size_t i = 0; i < g_appCount; i++)
		if (g_apps[i]->bundleID != NULL && CFEqual(g_apps[i]->bundleID, bundleID)) return g_apps[i];
	return NULL;
}

/* ============================================================================================ *
 * THE SCAN                                                                                            *
 * ============================================================================================ */
static int compareNames(const void *a, const void *b) { return strcmp(*(const char *const *)a, *(const char *const *)b); }

static void scanDirectory(const char *dir)
{
	DIR *d = opendir(dir);
	if (d == NULL) return;

	char **names = NULL;
	size_t n = 0, cap = 0;
	struct dirent *e;
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.') continue;
		size_t len = strlen(e->d_name);
		if (len < 4 || strcmp(e->d_name + len - 4, ".app") != 0) continue;
		if (n == cap) { cap = cap ? cap * 2 : 16; names = realloc(names, cap * sizeof(*names)); }
		names[n++] = strdup(e->d_name);
	}
	closedir(d);

	/* SORTED, so registration order — and therefore "the first candidate" — does not depend on the
	 * filesystem's readdir order. */
	qsort(names, n, sizeof(*names), compareNames);
	for (size_t i = 0; i < n; i++) {
		char full[4096];
		snprintf(full, sizeof full, "%s/%s", dir, names[i]);
		CFStringRef s = CFStringCreateWithCString(NULL, full, kCFStringEncodingUTF8);
		CFURLRef u = CFURLCreateWithFileSystemPath(NULL, s, kCFURLPOSIXPathStyle, true);
		_AGLSRegisterApplication(u);
		CFRelease(u);
		CFRelease(s);
		free(names[i]);
	}
	free(names);
}

static void scanApplications(void)
{
	const char *env = getenv("AGORA_APPLICATIONS");
	if (env != NULL && *env != '\0') {
		char *copy = strdup(env);		/* colon-separated, so a test can point at a fixture tree */
		const char *p = copy;
		while (p != NULL) {
			const char *q = strchr(p, ':');
			size_t len = q ? (size_t)(q - p) : strlen(p);
			if (len > 0) {
				char dir[4096];
				if (len >= sizeof dir) len = sizeof dir - 1;
				memcpy(dir, p, len);
				dir[len] = '\0';
				scanDirectory(dir);
			}
			p = q ? q + 1 : NULL;
		}
		free(copy);
		return;
	}
	scanDirectory("/Applications");
	scanDirectory("/usr/local/Applications");
	const char *home = getenv("HOME");
	if (home != NULL && *home != '\0') {
		char p[4096];
		snprintf(p, sizeof p, "%s/Applications", home);
		scanDirectory(p);
	}
}

void _AGLSInitialize(void)
{
	if (g_initialized) return;
	g_initialized = true;				/* set BEFORE the scan, so the scan's own lookups do not recurse */
	if (g_defaultHandlers == NULL)
		g_defaultHandlers = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	if (g_schemeHandlers == NULL)
		g_schemeHandlers = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	_AGUTTypeInitialize();
	scanApplications();
}

static void freeApps(void)
{
	for (size_t i = 0; i < g_appCount; i++) {
		AGApplication *a = g_apps[i];
		CFRelease(a->url);
		if (a->bundleID != NULL) CFRelease(a->bundleID);
		CFRelease(a->contentTypes);
		CFRelease(a->ownedTypes);
		CFRelease(a->schemes);
		free(a);
	}
	g_appCount = 0;
}

void _AGLSReset(void)
{
	g_initialized = false;
	freeApps();
	if (g_defaultHandlers != NULL) { CFRelease(g_defaultHandlers); g_defaultHandlers = NULL; }
	if (g_schemeHandlers != NULL) { CFRelease(g_schemeHandlers); g_schemeHandlers = NULL; }
	_AGUTTypeReset();
	_AGLSInitialize();
}

/* ============================================================================================ *
 * THE PUBLIC DOORS                                                                                    *
 * ============================================================================================ */
OSStatus LSRegisterURL(CFURLRef inURL, Boolean inUpdate)
{
	(void)inUpdate;
	_AGLSInitialize();
	if (inURL != NULL) _AGLSRegisterApplication(inURL);
	return 0;					/* noErr */
}

CFArrayRef LSCopyApplicationURLsForURL(CFURLRef inURL, LSRolesMask inRoleMask)
{
	_AGLSInitialize();
	CFMutableArrayRef out = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
	if (inURL == NULL) return out;

	CFStringRef uti = _AGUTTypeForURL(inURL);
	for (int pass = 0; pass < 2; pass++) {		/* owners first, then the rest */
		bool wantOwners = (pass == 0);
		for (size_t i = 0; i < g_appCount; i++) {
			AGApplication *app = g_apps[i];
			bool owns = arrayContains(app->ownedTypes, uti);
			if (owns != wantOwners) continue;
			if (arrayContains(out, app->url)) continue;

			CFIndex n = CFDictionaryGetCount(app->contentTypes);
			if (n == 0) continue;
			const void **keys = malloc((size_t)n * sizeof(*keys));
			const void **vals = malloc((size_t)n * sizeof(*vals));
			CFDictionaryGetKeysAndValues(app->contentTypes, keys, vals);
			bool match = false;
			for (CFIndex k = 0; k < n && !match; k++) {
				CFStringRef ct = keys[k];
				if (!CFEqual(ct, uti) && !_AGUTTypeConforms(uti, ct)) continue;
				LSRolesMask role = 0;
				CFNumberGetValue(vals[k], kCFNumberIntType, &role);
				if (inRoleMask == 0 || inRoleMask == kLSRolesAll || (role & inRoleMask) != 0) match = true;
			}
			free(keys);
			free(vals);
			if (match) CFArrayAppendValue(out, app->url);
		}
	}
	CFRelease(uti);
	return out;
}

OSStatus LSGetApplicationForURL(CFURLRef inURL, LSRolesMask inRoleMask, CFURLRef *outAppURL)
{
	_AGLSInitialize();
	if (outAppURL != NULL) *outAppURL = NULL;
	if (inURL == NULL) return kLSApplicationNotFoundErr;

	CFStringRef uti = _AGUTTypeForURL(inURL);
	CFStringRef def = (g_defaultHandlers != NULL) ? (CFStringRef)CFDictionaryGetValue(g_defaultHandlers, uti) : NULL;
	if (def != NULL) {
		AGApplication *a = findAppByBundleID(def);
		if (a != NULL) {
			if (outAppURL != NULL) *outAppURL = (CFURLRef)CFRetain(a->url);
			CFRelease(uti);
			return 0;
		}
	}

	CFArrayRef apps = LSCopyApplicationURLsForURL(inURL, inRoleMask);
	OSStatus status = kLSApplicationNotFoundErr;
	if (CFArrayGetCount(apps) > 0) {
		if (outAppURL != NULL) *outAppURL = (CFURLRef)CFRetain(CFArrayGetValueAtIndex(apps, 0));
		status = 0;
	}
	CFRelease(apps);
	CFRelease(uti);
	return status;
}

/* **THE IDENTIFIER BACK TO THE BUNDLE.** The registry already holds each application's bundle id (it is what the
 * default-handler tables are keyed by), so this is a walk and a compare — the door mattered because there was no
 * way to ASK from outside, and a shell that knows an application by the `app_id` on its windows needs its bundle
 * to read a name and an icon. An identifier nothing declares answers with an empty array, like every other
 * LSCopy door here; `outError` is part of Apple's signature and is left alone, because a registry that has simply
 * never seen the bundle has not failed. */
CFArrayRef LSCopyApplicationURLsForBundleIdentifier(CFStringRef inBundleIdentifier, CFErrorRef *outError)
{
	CFMutableArrayRef out;
	AGApplication *app;

	_AGLSInitialize();
	if (outError != NULL) {
		*outError = NULL;
	}
	out = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
	if (out == NULL || inBundleIdentifier == NULL) {
		return out;
	}
	app = findAppByBundleID(inBundleIdentifier);
	if (app != NULL) {
		CFArrayAppendValue(out, app->url);
	}
	return out;
}

CFStringRef LSCopyKindStringForURL(CFURLRef inURL)
{
	_AGLSInitialize();
	if (inURL == NULL) return NULL;
	CFStringRef uti = _AGUTTypeForURL(inURL);
	CFStringRef d = _AGUTTypeCopyDescription(uti);
	CFRelease(uti);
	return d;
}

CFStringRef LSCopyDefaultRoleHandlerForContentType(CFStringRef inContentType, LSRolesMask inRoleMask)
{
	(void)inRoleMask;
	_AGLSInitialize();
	if (inContentType == NULL) return NULL;
	CFStringRef b = (CFStringRef)CFDictionaryGetValue(g_defaultHandlers, inContentType);
	return (b != NULL) ? (CFStringRef)CFRetain(b) : NULL;
}

OSStatus LSSetDefaultRoleHandlerForContentType(CFStringRef inContentType, LSRolesMask inRoleMask, CFStringRef inHandlerBundleID)
{
	(void)inRoleMask;
	_AGLSInitialize();
	if (inContentType != NULL && inHandlerBundleID != NULL) CFDictionarySetValue(g_defaultHandlers, inContentType, inHandlerBundleID);
	return 0;
}

CFStringRef LSCopyDefaultHandlerForURLScheme(CFStringRef inURLScheme)
{
	_AGLSInitialize();
	if (inURLScheme == NULL) return NULL;
	CFStringRef b = (CFStringRef)CFDictionaryGetValue(g_schemeHandlers, inURLScheme);
	return (b != NULL) ? (CFStringRef)CFRetain(b) : NULL;
}

OSStatus LSSetDefaultHandlerForURLScheme(CFStringRef inURLScheme, CFStringRef inHandlerBundleID)
{
	_AGLSInitialize();
	if (inURLScheme != NULL && inHandlerBundleID != NULL) CFDictionarySetValue(g_schemeHandlers, inURLScheme, inHandlerBundleID);
	return 0;
}

void AGLaunchServicesReset(void)
{
	_AGLSReset();
}
