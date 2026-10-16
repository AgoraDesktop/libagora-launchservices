/*
 * LSOpen.c — opening: resolve the executable, spawn it.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE SPAWN IS `posix_spawn`, NOT `NSTask`. `NSTask` lives in Foundation, and Foundation sits ABOVE Launch
 * Services in this tree's stack (`libagora-launchservices` links Core Foundation alone) — a Launch Services
 * that reached up for it would invert the stack it is a part of. `posix_spawn` is below both and is the same
 * primitive `NSTask` uses.
 *
 * THE CHILD INHERITS THE ENVIRONMENT, so a launched application is a Wayland client on the same display its
 * launcher was — which is what lets the suite's fixture see `WAYLAND_DISPLAY`, and what lets a launched Agora
 * application connect to the server at all.
 */
#include "LSOpen.h"
#include "LaunchServices_internal.h"

#include <CoreFoundation/CFBundle.h>

#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

extern char **environ;

/* AN APPLICATION BUNDLE IS A DIRECTORY NAMED `.app`. That is the surface test — not `CFBundlePackageType`,
 * because a bundle whose Info.plist is unreadable is still an application the person asked to open, and the
 * executable lookup below is what says whether it really is one. */
static bool urlIsApplicationBundle(CFURLRef url)
{
	char path[4096];
	struct stat status;
	CFStringRef extension;
	bool isApp;

	if (!CFURLGetFileSystemRepresentation(url, true, (UInt8 *)path, sizeof path)) return false;
	if (stat(path, &status) != 0 || !S_ISDIR(status.st_mode)) return false;
	extension = CFURLCopyPathExtension(url);
	isApp = (extension != NULL) &&
	        (CFStringCompare(extension, CFSTR("app"), kCFCompareCaseInsensitive) == kCFCompareEqualTo);
	if (extension != NULL) CFRelease(extension);
	return isApp;
}

/* **THE EXECUTABLE, RESOLVED HERE BECAUSE `CFBundleCopyExecutableURL` IS BROKEN IN THIS TREE'S CORE
 * FOUNDATION — MEASURED, not assumed.** It answers NULL for a MODERN `.app`, and the real `Workspace.app` is
 * no exception: the in-bundle search never reaches `Contents/MacOS`. The fix belongs in `libagora-core`;
 * until it lands, this resolves the same two places Apple documents, in Apple's own order —
 * `Contents/MacOS/<name>` first, then the wrapper itself for an old-style bundle.
 *
 * WHAT IS STILL APPLE'S IS THE NAME: `CFBundleExecutable`, read through `CFBundleGetValueForInfoDictionaryKey`,
 * which DOES work. What is ours is only the two paths. */
static CFURLRef copyExecutableURL(CFURLRef appURL)
{
	CFBundleRef bundle = CFBundleCreate(NULL, appURL);
	CFStringRef name = NULL, path = NULL;
	CFURLRef result = NULL;

	if (bundle != NULL) name = (CFStringRef)CFBundleGetValueForInfoDictionaryKey(bundle, CFSTR("CFBundleExecutable"));
	if (name != NULL && CFGetTypeID(name) != CFStringGetTypeID()) name = NULL;
	path = CFURLCopyFileSystemPath(appURL, kCFURLPOSIXPathStyle);

	if (name != NULL && path != NULL) {
		char nameBuffer[512], baseBuffer[4096];
		const char *patterns[2] = { "%s/Contents/MacOS/%s", "%s/%s" };
		int i;

		CFStringGetCString(name, nameBuffer, sizeof nameBuffer, kCFStringEncodingUTF8);
		CFStringGetCString(path, baseBuffer, sizeof baseBuffer, kCFStringEncodingUTF8);
		for (i = 0; i < 2 && result == NULL; i++) {
			char full[4096], resolved[4096];
			CFStringRef string;
			CFURLRef url;
			struct stat status;

			snprintf(full, sizeof full, patterns[i], baseBuffer, nameBuffer);
			string = CFStringCreateWithCString(NULL, full, kCFStringEncodingUTF8);
			url = CFURLCreateWithFileSystemPath(NULL, string, kCFURLPOSIXPathStyle, false);
			CFRelease(string);
			if (url == NULL) continue;
			if (CFURLGetFileSystemRepresentation(url, true, (UInt8 *)resolved, sizeof resolved) &&
			    stat(resolved, &status) == 0 && S_ISREG(status.st_mode)) result = url;
			else CFRelease(url);
		}
	}
	if (path != NULL) CFRelease(path);
	if (bundle != NULL) CFRelease(bundle);
	return result;			/* +1, or NULL */
}

/* Spawn the application's process with `arguments` (CFStringRef paths, or NULL). */
static OSStatus launchApplicationAtURL(CFURLRef appURL, CFArrayRef arguments)
{
	CFURLRef executable = copyExecutableURL(appURL);
	char executablePath[4096];
	CFIndex extra, i;
	char **argv;
	pid_t pid = 0;
	int rc;

	if (executable == NULL) return kLSNoExecutableErr;
	if (!CFURLGetFileSystemRepresentation(executable, true, (UInt8 *)executablePath, sizeof executablePath)) {
		CFRelease(executable);
		return kLSUnknownErr;
	}
	CFRelease(executable);

	extra = (arguments != NULL) ? CFArrayGetCount(arguments) : 0;
	argv = calloc((size_t)extra + 2, sizeof(char *));
	argv[0] = strdup(executablePath);
	for (i = 0; i < extra; i++) {
		CFStringRef argument = CFArrayGetValueAtIndex(arguments, i);
		char buffer[4096];

		if (CFStringGetFileSystemRepresentation(argument, buffer, sizeof buffer)) argv[i + 1] = strdup(buffer);
		else argv[i + 1] = strdup("");
	}
	argv[extra + 1] = NULL;

	rc = posix_spawn(&pid, executablePath, NULL, NULL, argv, environ);

	for (i = 0; i <= extra; i++) free(argv[i]);
	free(argv);

	if (rc == 0) return 0;
	if (rc == ENOENT) return kLSNoExecutableErr;
	if (rc == EACCES) return kLSNoLaunchPermissionErr;
	return kLSUnknownErr;
}

OSStatus LSOpenCFURLRef(CFURLRef inURL, CFURLRef *outLaunchedURL)
{
	CFURLRef application = NULL;
	CFArrayRef arguments = NULL;
	OSStatus status;

	_AGLSInitialize();
	if (outLaunchedURL != NULL) *outLaunchedURL = NULL;
	if (inURL == NULL) return kLSUnknownErr;

	if (urlIsApplicationBundle(inURL)) {
		application = (CFURLRef)CFRetain(inURL);
	} else {
		CFURLRef resolved = NULL;
		CFStringRef document;

		status = LSGetApplicationForURL(inURL, kLSRolesAll, &resolved);
		if (status != 0) return status;
		if (resolved == NULL) return kLSApplicationNotFoundErr;
		application = resolved;			/* +1 from LSGetApplicationForURL */
		document = CFURLCopyFileSystemPath(inURL, kCFURLPOSIXPathStyle);
		if (document != NULL) {
			const void *one[1] = { document };
			arguments = CFArrayCreate(NULL, one, 1, &kCFTypeArrayCallBacks);
			CFRelease(document);
		}
	}

	status = launchApplicationAtURL(application, arguments);
	if (arguments != NULL) CFRelease(arguments);
	if (status == 0 && outLaunchedURL != NULL) *outLaunchedURL = (CFURLRef)CFRetain(application);
	CFRelease(application);
	return status;
}

OSStatus LSOpenURLsWithRole(CFArrayRef inURLs, LSRolesMask inRole, LSLaunchFlags inFlags, CFArrayRef *outLaunchedApps)
{
	CFMutableArrayRef launched;
	OSStatus firstError = 0;
	CFIndex i;

	/* ACCEPTED AND NOT YET INTERPRETED — see LSOpen.h: activation and new-instance are the session's. */
	(void)inRole;
	(void)inFlags;

	_AGLSInitialize();
	if (outLaunchedApps != NULL) *outLaunchedApps = NULL;
	if (inURLs == NULL) return kLSUnknownErr;

	launched = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
	for (i = 0; i < CFArrayGetCount(inURLs); i++) {
		CFURLRef application = NULL;
		OSStatus status = LSOpenCFURLRef(CFArrayGetValueAtIndex(inURLs, i), &application);

		if (status != 0 && firstError == 0) firstError = status;
		if (application != NULL) { CFArrayAppendValue(launched, application); CFRelease(application); }
	}
	if (outLaunchedApps != NULL) *outLaunchedApps = launched;
	else CFRelease(launched);
	return firstError;
}
