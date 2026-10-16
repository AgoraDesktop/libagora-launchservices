/*
 * launch.c — the opening door: `LSOpenCFURLRef` and `LSOpenURLsWithRole`.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * WHAT THIS HOLDS UP: an application bundle is LAUNCHED — its process really runs — and a DOCUMENT is launched
 * in the application that owns its type, with the document passed as an argument. The fixture is built here
 * rather than committed: `Probe.app` is an `Info.plist` and a shell script, and the script writes its
 * arguments to a sentinel file, so "it ran" is a file on disk and not a line of output.
 *
 * THE ENVIRONMENT IS THE OTHER THING MEASURED: the child inherits it, so it sees the sentinel's path — which is
 * also what would let a launched application connect to the window server (`WAYLAND_DISPLAY`).
 */
#define _DEFAULT_SOURCE

#include <LaunchServices/LSOpen.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int g_failures;

static void check(bool condition, const char *what)
{
	if (!condition) g_failures++;
	printf("  [%s] %s\n", condition ? "ok" : "FAIL", what);
}

static CFURLRef fileURL(const char *path)
{
	CFStringRef string = CFStringCreateWithCString(NULL, path, kCFStringEncodingUTF8);
	CFURLRef url = CFURLCreateWithFileSystemPath(NULL, string, kCFURLPOSIXPathStyle, false);
	CFRelease(string);
	return url;
}

/* TWO URLS ARE THE SAME APPLICATION WHEN THEIR PATHS ARE, and the `isDirectory` flag is why this is not
 * `CFEqual`: the registry builds an application URL as a DIRECTORY URL, and a directory URL compares unequal
 * to the same path built as a file. Comparing paths is the question actually being asked. */
static bool samePath(CFURLRef a, CFURLRef b)
{
	char pathA[4096], pathB[4096];
	size_t lengthA, lengthB;

	if (a == NULL || b == NULL) return false;
	if (!CFURLGetFileSystemRepresentation(a, true, (UInt8 *)pathA, sizeof pathA)) return false;
	if (!CFURLGetFileSystemRepresentation(b, true, (UInt8 *)pathB, sizeof pathB)) return false;
	lengthA = strlen(pathA);
	lengthB = strlen(pathB);
	while (lengthA > 1 && pathA[lengthA - 1] == '/') pathA[--lengthA] = '\0';
	while (lengthB > 1 && pathB[lengthB - 1] == '/') pathB[--lengthB] = '\0';
	return strcmp(pathA, pathB) == 0;
}

static bool writeText(const char *path, const char *text)
{
	FILE *file = fopen(path, "wb");
	if (file == NULL) return false;
	fputs(text, file);
	fclose(file);
	return true;
}

static bool waitForFile(const char *path, int milliseconds)
{
	int waited;

	for (waited = 0; waited < milliseconds; waited += 20) {
		struct stat status;
		if (stat(path, &status) == 0 && status.st_size > 0) return true;
		usleep(20000);
	}
	return false;
}

static const char *readText(const char *path)
{
	static char buffer[8192];
	FILE *file = fopen(path, "rb");
	size_t count;

	if (file == NULL) return NULL;
	count = fread(buffer, 1, sizeof buffer - 1, file);
	fclose(file);
	buffer[count] = '\0';
	return buffer;
}

/* THE FIXTURE: a bundle whose executable is a script that records its arguments. */
static bool buildProbe(const char *root)
{
	char directory[4096], plist[4096], probe[4096], command[4096];
	const char *manifest =
		"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
		"<plist version=\"1.0\"><dict>"
		"<key>CFBundleIdentifier</key><string>com.agora.probe</string>"
		"<key>CFBundleExecutable</key><string>Probe</string>"
		"<key>CFBundlePackageType</key><string>APPL</string>"
		"<key>UTExportedTypeDeclarations</key><array><dict>"
		  "<key>UTTypeIdentifier</key><string>com.agora.probe.document</string>"
		  "<key>UTTypeDescription</key><string>Probe Document</string>"
		  "<key>UTTypeConformsTo</key><array><string>public.data</string></array>"
		  "<key>UTTypeTagSpecification</key><dict>"
		    "<key>public.filename-extension</key><array><string>prb</string></array>"
		  "</dict></dict></array>"
		"<key>CFBundleDocumentTypes</key><array><dict>"
		  "<key>CFBundleTypeName</key><string>Probe Document</string>"
		  "<key>CFBundleTypeRole</key><string>Editor</string>"
		  "<key>LSItemContentTypes</key><array><string>com.agora.probe.document</string></array>"
		"</dict></array>"
		"</dict></plist>\n";

	snprintf(directory, sizeof directory, "%s/Probe.app/Contents/MacOS", root);
	snprintf(command, sizeof command, "mkdir -p '%s'", directory);
	if (system(command) != 0) return false;

	snprintf(plist, sizeof plist, "%s/Probe.app/Contents/Info.plist", root);
	if (!writeText(plist, manifest)) return false;

	snprintf(probe, sizeof probe, "%s/Probe.app/Contents/MacOS/Probe", root);
	if (!writeText(probe, "#!/bin/sh\nprintf '%s\\n' \"$@\" > \"$AGORA_PROBE_OUT\"\n")) return false;
	return chmod(probe, 0755) == 0;
}

int main(void)
{
	char template[] = "/tmp/agora-launch-XXXXXX";
	char *root = mkdtemp(template);
	char appPath[4096], documentPath[4096], sentinel[4096], missingPath[4096], command[4096];
	CFURLRef appURL, documentURL, missingURL, launched;
	OSStatus status;

	if (root == NULL) { perror("mkdtemp"); return 1; }
	if (!buildProbe(root)) { fprintf(stderr, "launch: could not build the fixture\n"); return 1; }

	snprintf(appPath, sizeof appPath, "%s/Probe.app", root);
	snprintf(documentPath, sizeof documentPath, "%s/note.prb", root);
	snprintf(sentinel, sizeof sentinel, "%s/out.txt", root);
	snprintf(missingPath, sizeof missingPath, "%s/Nope.app", root);
	if (!writeText(documentPath, "the probe's document\n")) return 1;

	setenv("AGORA_APPLICATIONS", root, 1);
	setenv("AGORA_PROBE_OUT", sentinel, 1);
	printf("launch: %s\n", root);
	AGLaunchServicesReset();

	appURL = fileURL(appPath);
	documentURL = fileURL(documentPath);
	missingURL = fileURL(missingPath);

	/* ---- 1. an APPLICATION, which is what "launch a .app" means ---------------------------- */
	unlink(sentinel);
	launched = NULL;
	status = LSOpenCFURLRef(appURL, &launched);
	check(status == 0, "launching an application bundle returns noErr");
	check(launched != NULL && samePath(launched, appURL), "and names the application it launched");
	check(waitForFile(sentinel, 3000), "and the application RAN — it wrote its sentinel");
	if (launched != NULL) CFRelease(launched);
	usleep(100000);

	/* ---- 2. a DOCUMENT, in the application that owns its type, given the document ----------- */
	unlink(sentinel);
	launched = NULL;
	status = LSOpenCFURLRef(documentURL, &launched);
	check(status == 0, "opening a document returns noErr");
	check(launched != NULL && samePath(launched, appURL), "and launches the type's OWNING application");
	check(waitForFile(sentinel, 3000), "and that application ran");
	{
		const char *text = readText(sentinel);
		check(text != NULL && strstr(text, documentPath) != NULL,
		      "and it was given the DOCUMENT as an argument");
	}
	if (launched != NULL) CFRelease(launched);
	usleep(100000);

	/* ---- 3. an application that is not there ---------------------------------------------- */
	status = LSOpenCFURLRef(missingURL, NULL);
	check(status != 0, "opening an application that does not exist returns an error");

	/* ---- 4. the array door ---------------------------------------------------------------- */
	{
		const void *one[1] = { documentURL };
		CFArrayRef urls = CFArrayCreate(NULL, one, 1, &kCFTypeArrayCallBacks);
		CFArrayRef applications = NULL;

		status = LSOpenURLsWithRole(urls, kLSRolesAll, kLSLaunchDefaults, &applications);
		check(status == 0 && applications != NULL && CFArrayGetCount(applications) == 1,
		      "LSOpenURLsWithRole opens a document and names the application");
		CFRelease(applications);
		CFRelease(urls);
	}

	CFRelease(appURL);
	CFRelease(documentURL);
	CFRelease(missingURL);

	snprintf(command, sizeof command, "rm -rf '%s'", root);
	(void)system(command);

	printf("launch: %d check(s) failed\n", g_failures);
	return g_failures == 0 ? 0 : 1;
}
