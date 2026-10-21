/*
 * launchservices.c — F1's measurement.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * WHAT THIS HOLDS UP, in WORKSPACE-PLAN.md's words for F1:
 *
 *   * an item has an IDENTITY INDEPENDENT OF ITS EXTENSION — a declared type, resolved from a filename
 *     extension, that conforms to `public.text` through the hierarchy;
 *   * the DEFAULT APPLICATION and `Open With` CAN BE ANSWERED from Agora's own applications, with the
 *     bundle that OWNS a type preferred over one that merely views it;
 *   * a MOVED BUNDLE IS RE-FOUND after a rescan;
 *   * and NOTHING FOREIGN IS ON THE FIXTURE — the tree holds three Agora bundles and no `.desktop` file.
 */
#define _DEFAULT_SOURCE

#include <LaunchServices/LaunchServices.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;

static void check(bool condition, const char *what)
{
	if (!condition) g_failures++;
	printf("  [%s] %s\n", condition ? "ok" : "FAIL", what);
}

/* The last path component of an application URL, as a C string, so a test can name the bundle it means. */
static bool lastNameIs(CFURLRef url, const char *name)
{
	CFStringRef last = CFURLCopyLastPathComponent(url);
	if (last == NULL) return false;
	char buf[256];
	bool ok = CFStringGetCString(last, buf, sizeof buf, kCFStringEncodingUTF8) && strcmp(buf, name) == 0;
	CFRelease(last);
	return ok;
}

static bool listContains(CFArrayRef apps, const char *name)
{
	for (CFIndex i = 0; i < CFArrayGetCount(apps); i++)
		if (lastNameIs(CFArrayGetValueAtIndex(apps, i), name)) return true;
	return false;
}

static CFURLRef fileURL(const char *path)
{
	CFStringRef s = CFStringCreateWithCString(NULL, path, kCFStringEncodingUTF8);
	CFURLRef u = CFURLCreateWithFileSystemPath(NULL, s, kCFURLPOSIXPathStyle, false);
	CFRelease(s);
	return u;
}

int main(void)
{
	/* A PRIVATE COPY OF THE FIXTURES, so the move test moves a copy and not the source tree. */
	char tmpl[] = "/tmp/agora-ls-XXXXXX";
	char *root = mkdtemp(tmpl);
	if (root == NULL) { perror("mkdtemp"); return 1; }

	char cmd[4096];
	snprintf(cmd, sizeof cmd,
	         "cp -R '%s/Writer.app' '%s/' && cp -R '%s/Viewer.app' '%s/' && cp -R '%s/Legacy.app' '%s/'",
	         FIXTURES_DIR, root, FIXTURES_DIR, root, FIXTURES_DIR, root);
	if (system(cmd) != 0) { fprintf(stderr, "fixture copy failed\n"); return 1; }

	setenv("AGORA_APPLICATIONS", root, 1);
	AGLaunchServicesReset();

	printf("launchservices: %s\n", root);

	/* ---- 1. a declared type, resolved from its extension -------------------------------- */
	CFStringRef wrt = UTTypeCreatePreferredIdentifierForTag(kUTTagClassFilenameExtension, CFSTR("wrt"), NULL);
	check(wrt != NULL && CFEqual(wrt, CFSTR("com.agora.writer.document")),
	      "the declaring bundle's type is preferred for its extension");
	check(UTTypeConformsTo(wrt, kUTTypeText), "the declared type conforms to public.text");
	check(UTTypeConformsTo(wrt, kUTTypeData), "and to public.data through the hierarchy");
	check(!UTTypeConformsTo(kUTTypeData, kUTTypeImage), "but public.data does not conform to public.image");

	CFStringRef mime = UTTypeCopyPreferredTagWithClass(wrt, kUTTagClassMIMEType);
	check(mime != NULL && CFEqual(mime, CFSTR("application/x-agora-writer")),
	      "the declared MIME tag is found");
	if (mime != NULL) CFRelease(mime);

	/* ---- 2. the built-in seed, matched case-insensitively -------------------------------- */
	CFStringRef png = UTTypeCreatePreferredIdentifierForTag(kUTTagClassFilenameExtension, CFSTR("PNG"), NULL);
	check(png != NULL && CFEqual(png, kUTTypePNG), "a built-in type is matched case-insensitively");
	check(UTTypeConformsTo(png, kUTTypeImage) && UTTypeConformsTo(png, kUTTypeData),
	      "public.png conforms to public.image and public.data");

	/* The CONFORMANCE direction of Open With: Viewer.app names `public.image`, and a PNG conforms to it. */
	CFURLRef pngFile = fileURL("/tmp/agora-sample.png");
	CFArrayRef pngApps = LSCopyApplicationURLsForURL(pngFile, kLSRolesAll);
	check(listContains(pngApps, "Viewer.app"),
	      "an application naming public.image is offered for a PNG, through conformance");
	CFRelease(pngApps);
	CFRelease(pngFile);

	/* ---- 3. the Kind string, which is what a view shows --------------------------------- */
	CFURLRef sample = fileURL("/tmp/agora-sample.wrt");
	CFStringRef kind = LSCopyKindStringForURL(sample);
	check(kind != NULL && CFEqual(kind, CFSTR("Writer Document")),
	      "the Kind string is the declared description, not the extension");
	if (kind != NULL) CFRelease(kind);

	/* ---- 4. Open With: the owner and the viewer, owner first ----------------------------- */
	CFArrayRef apps = LSCopyApplicationURLsForURL(sample, kLSRolesAll);
	check(CFArrayGetCount(apps) >= 2, "two applications claim the type");
	check(listContains(apps, "Writer.app") && listContains(apps, "Viewer.app"),
	      "both the owner and the viewer are candidates");
	check(CFArrayGetCount(apps) > 0 && lastNameIs(CFArrayGetValueAtIndex(apps, 0), "Writer.app"),
	      "and the OWNER is first, which is the ordering the Open With menu shows");
	CFRelease(apps);

	/* ---- 5. the default application ------------------------------------------------------ */
	CFURLRef def = NULL;
	OSStatus st = LSGetApplicationForURL(sample, kLSRolesAll, &def);
	check(st == 0 && def != NULL && lastNameIs(def, "Writer.app"),
	      "the default application is the bundle that owns the type");
	if (def != NULL) CFRelease(def);

	/* ---- 5b. the bundle identifier back to the bundle ------------------------------------ */
	/* **THE DOOR A SHELL NEEDS, because the window server publishes a window's `app_id` and not its path.** An
	 * identifier maps to the one bundle that declares it; an unknown one answers with an EMPTY ARRAY rather than
	 * NULL, so a caller iterates without a special case. */
	{
		CFArrayRef byID = LSCopyApplicationURLsForBundleIdentifier(CFSTR("com.agora.writer"), NULL);

		check(byID != NULL && CFArrayGetCount(byID) == 1 && lastNameIs(CFArrayGetValueAtIndex(byID, 0),
			"Writer.app"), "a bundle identifier resolves to its bundle");
		if (byID != NULL) CFRelease(byID);
		byID = LSCopyApplicationURLsForBundleIdentifier(CFSTR("io.github.agoradesktop.nothing"), NULL);
		check(byID != NULL && CFArrayGetCount(byID) == 0,
		      "and an identifier nothing declares is an empty array, not NULL");
		if (byID != NULL) CFRelease(byID);
	}

	/* A user's explicit choice overrides the owner. */
	LSSetDefaultRoleHandlerForContentType(CFSTR("com.agora.writer.document"), kLSRolesAll, CFSTR("com.agora.viewer"));
	def = NULL;
	st = LSGetApplicationForURL(sample, kLSRolesAll, &def);
	check(st == 0 && def != NULL && lastNameIs(def, "Viewer.app"),
	      "an explicit default handler overrides the owner");
	if (def != NULL) CFRelease(def);

	/* ---- 6. a legacy CFBundleTypeExtensions claim, and the dyn. fallback ----------------- */
	CFURLRef legacyFile = fileURL("/tmp/agora-sample.lgy");
	CFArrayRef legacyApps = LSCopyApplicationURLsForURL(legacyFile, kLSRolesAll);
	check(listContains(legacyApps, "Legacy.app"), "a legacy CFBundleTypeExtensions claim is honoured");
	CFRelease(legacyApps);
	CFStringRef lgy = UTTypeCreatePreferredIdentifierForTag(kUTTagClassFilenameExtension, CFSTR("lgy"), NULL);
	check(lgy != NULL && CFStringHasPrefix(lgy, CFSTR("dyn.")),
	      "an extension nothing declares gets a dyn. identifier rather than none");
	if (lgy != NULL) CFRelease(lgy);
	CFRelease(legacyFile);

	/* ---- 7. a moved bundle, re-found after a rescan -------------------------------------- */
	char from[4096], to[4096];
	snprintf(from, sizeof from, "%s/Writer.app", root);
	snprintf(to, sizeof to, "%s/Writer2.app", root);
	if (rename(from, to) != 0) { perror("rename"); return 1; }
	AGLaunchServicesReset();

	CFArrayRef moved = LSCopyApplicationURLsForURL(sample, kLSRolesAll);
	check(listContains(moved, "Writer2.app"), "the moved bundle is re-found after a rescan");
	check(!listContains(moved, "Writer.app"), "and its old path is gone");
	CFRelease(moved);

	/* An item still resolves to the moved owner by default. */
	def = NULL;
	st = LSGetApplicationForURL(sample, kLSRolesAll, &def);
	check(st == 0 && def != NULL && lastNameIs(def, "Writer2.app"),
	      "and the default application follows the bundle");
	if (def != NULL) CFRelease(def);

	CFRelease(sample);
	CFRelease(png);
	CFRelease(wrt);

	snprintf(cmd, sizeof cmd, "rm -rf '%s'", root);
	(void)system(cmd);

	printf("launchservices: %d check(s) failed\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
