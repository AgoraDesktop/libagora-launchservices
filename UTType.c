/*
 * UTType.c — the Uniform Type Identifier registry, and the seed of Apple's built-in types.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * A TYPE IS A NODE with a conforms-to list and a tag table, and the whole registry is a flat array of them.
 * LINEAR SEARCH IS THE RIGHT SHAPE HERE AND NOT A SHORTCUT: a desktop's type set is tens to hundreds of
 * entries, every door is called from the AppKit's main thread, and a hash table would buy nothing but a
 * second structure to keep consistent with the first. The ONE index is the tag table — tag class, then
 * lowercased tag, then the identifiers that carry it — because that is the door called for every file a
 * view draws.
 *
 * NOTHING HERE IS A THIRD-PARTY LIBRARY AND NOTHING HERE READS A FOREIGN DATABASE. Types are seeded from
 * the small built-in set below and added to by `Info.plist` declarations, which is the whole of the model
 * (WORKSPACE-PLAN.md §2, F1).
 */
#include "UTType.h"
#include "LaunchServices_internal.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* ============================================================================================ *
 * THE CONSTANTS                                                                                       *
 * ============================================================================================ */
const CFStringRef kUTTagClassFilenameExtension = CFSTR("public.filename-extension");
const CFStringRef kUTTagClassMIMEType          = CFSTR("public.mime-type");
const CFStringRef kUTTagClassNSPboardType      = CFSTR("com.apple.nspboard-type");
const CFStringRef kUTTagClassOSType            = CFSTR("com.apple.ostype");

const CFStringRef kUTTypeItem                  = CFSTR("public.item");
const CFStringRef kUTTypeContent               = CFSTR("public.content");
const CFStringRef kUTTypeData                  = CFSTR("public.data");
const CFStringRef kUTTypeText                  = CFSTR("public.text");
const CFStringRef kUTTypePlainText             = CFSTR("public.plain-text");
const CFStringRef kUTTypeUTF8PlainText         = CFSTR("public.utf8-plain-text");
const CFStringRef kUTTypeRTF                   = CFSTR("public.rtf");
const CFStringRef kUTTypeHTML                  = CFSTR("public.html");
const CFStringRef kUTTypeXML                   = CFSTR("public.xml");
const CFStringRef kUTTypeSourceCode            = CFSTR("public.source-code");
const CFStringRef kUTTypeImage                 = CFSTR("public.image");
const CFStringRef kUTTypeJPEG                  = CFSTR("public.jpeg");
const CFStringRef kUTTypePNG                   = CFSTR("public.png");
const CFStringRef kUTTypeTIFF                  = CFSTR("public.tiff");
const CFStringRef kUTTypeGIF                   = CFSTR("public.gif");
const CFStringRef kUTTypePDF                   = CFSTR("public.pdf");
const CFStringRef kUTTypeAudio                 = CFSTR("public.audio");
const CFStringRef kUTTypeMP3                   = CFSTR("public.mp3");
const CFStringRef kUTTypeMPEG4Audio            = CFSTR("public.mpeg-4-audio");
const CFStringRef kUTTypeMovie                 = CFSTR("public.movie");
const CFStringRef kUTTypeMPEG4                 = CFSTR("public.mpeg-4");
const CFStringRef kUTTypeDirectory             = CFSTR("public.directory");
const CFStringRef kUTTypeFolder                = CFSTR("public.folder");
const CFStringRef kUTTypeVolume                = CFSTR("public.volume");
const CFStringRef kUTTypeBundle                = CFSTR("com.apple.bundle");
const CFStringRef kUTTypePackage               = CFSTR("com.apple.package");
const CFStringRef kUTTypeApplication           = CFSTR("com.apple.application");
const CFStringRef kUTTypeApplicationBundle     = CFSTR("com.apple.application-bundle");
const CFStringRef kUTTypeExecutable            = CFSTR("public.executable");
const CFStringRef kUTTypeUnixExecutable        = CFSTR("public.unix-executable");
const CFStringRef kUTTypeURL                   = CFSTR("public.url");
const CFStringRef kUTTypeFileURL               = CFSTR("public.file-url");
const CFStringRef kUTTypeSymLink               = CFSTR("public.symlink");
const CFStringRef kUTTypeAliasFile             = CFSTR("public.alias");
const CFStringRef kUTTypeArchive               = CFSTR("public.archive");
const CFStringRef kUTTypeZipArchive            = CFSTR("public.zip-archive");
const CFStringRef kUTTypeTarArchive            = CFSTR("public.tar-archive");

/* ============================================================================================ *
 * THE REGISTRY                                                                                        *
 * ============================================================================================ */
typedef struct AGUTType {
	CFStringRef             identifier;
	CFStringRef             description;	/* NULL until declared */
	CFMutableArrayRef       conformsTo;	/* CFStringRef */
	CFMutableDictionaryRef  tags;		/* tag class -> CFMutableArrayRef of CFStringRef */
	CFURLRef                declaringBundle;/* NULL for a built-in or dynamic type */
} AGUTType;

static AGUTType              **g_types     = NULL;
static size_t                  g_typeCount = 0;
static size_t                  g_typeCap   = 0;
static CFMutableDictionaryRef  g_tagIndex  = NULL;	/* tag class -> { lowercased tag -> [identifiers] } */
static bool                    g_seeded    = false;

static CFStringRef AGStr(const char *s) { return CFStringCreateWithCString(NULL, s, kCFStringEncodingUTF8); }

static CFStringRef AGLower(CFStringRef s)
{
	CFMutableStringRef m = CFStringCreateMutableCopy(NULL, 0, s);
	if (m == NULL) return NULL;
	CFStringLowercase(m, NULL);
	return m;
}

static bool AGArrayContains(CFArrayRef a, CFTypeRef v)
{
	for (CFIndex i = 0; i < CFArrayGetCount(a); i++)
		if (CFEqual(CFArrayGetValueAtIndex(a, i), v)) return true;
	return false;
}

static AGUTType *findType(CFStringRef ident)
{
	if (ident == NULL) return NULL;
	for (size_t i = 0; i < g_typeCount; i++)
		if (CFEqual(g_types[i]->identifier, ident)) return g_types[i];
	return NULL;
}

static AGUTType *ensureType(CFStringRef ident)
{
	AGUTType *t = findType(ident);
	if (t != NULL) return t;
	if (g_typeCount == g_typeCap) {
		g_typeCap = g_typeCap ? g_typeCap * 2 : 32;
		g_types = realloc(g_types, g_typeCap * sizeof(*g_types));
	}
	t = calloc(1, sizeof(*t));
	t->identifier = CFStringCreateCopy(NULL, ident);
	t->conformsTo = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
	t->tags       = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	g_types[g_typeCount++] = t;
	return t;
}

static AGUTType *ensureTypeC(const char *ident)
{
	CFStringRef s = AGStr(ident);
	AGUTType *t = ensureType(s);
	CFRelease(s);
	return t;
}

/* The one index: which identifiers carry a given tag. Appending an identifier that is already there is
 * what makes a declaration idempotent, which matters because a rescan re-registers every bundle. */
static void indexTag(CFStringRef ident, CFStringRef cls, CFStringRef tag)
{
	CFMutableDictionaryRef byTag = (CFMutableDictionaryRef)CFDictionaryGetValue(g_tagIndex, cls);
	if (byTag == NULL) {
		byTag = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
		CFDictionarySetValue(g_tagIndex, cls, byTag);
		CFRelease(byTag);
	}
	CFStringRef lower = AGLower(tag);
	CFMutableArrayRef ids = (CFMutableArrayRef)CFDictionaryGetValue(byTag, lower);
	if (ids == NULL) {
		ids = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
		CFDictionarySetValue(byTag, lower, ids);
		CFRelease(ids);
	}
	if (!AGArrayContains(ids, ident)) CFArrayAppendValue(ids, ident);
	CFRelease(lower);
}

static void addConforms(AGUTType *t, CFStringRef parent)
{
	if (!AGArrayContains(t->conformsTo, parent)) CFArrayAppendValue(t->conformsTo, parent);
}

static void addTag(AGUTType *t, CFStringRef cls, CFStringRef tag)
{
	CFMutableArrayRef arr = (CFMutableArrayRef)CFDictionaryGetValue(t->tags, cls);
	if (arr == NULL) {
		arr = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
		CFDictionarySetValue(t->tags, cls, arr);
		CFRelease(arr);
	}
	if (!AGArrayContains(arr, tag)) CFArrayAppendValue(arr, tag);
	indexTag(t->identifier, cls, tag);
}

/* Turn whatever a plist holds — a string, or an array of strings — into a string array. NULL for
 * anything else, which is how a malformed declaration is ignored rather than guessed at. */
static CFArrayRef stringArrayFromValue(CFTypeRef v)
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

static void addTagsFromSpec(const void *key, const void *value, void *ctx)
{
	AGUTType *t = ctx;
	CFArrayRef tags = stringArrayFromValue(value);
	if (tags == NULL) return;
	for (CFIndex i = 0; i < CFArrayGetCount(tags); i++) addTag(t, (CFStringRef)key, CFArrayGetValueAtIndex(tags, i));
	CFRelease(tags);
}

void _AGUTTypeRegisterDeclaration(CFDictionaryRef declaration, CFURLRef bundleURL)
{
	if (declaration == NULL) return;
	CFTypeRef idv = CFDictionaryGetValue(declaration, CFSTR("UTTypeIdentifier"));
	if (idv == NULL || CFGetTypeID(idv) != CFStringGetTypeID()) return;

	AGUTType *t = ensureType((CFStringRef)idv);
	if (t->declaringBundle == NULL && bundleURL != NULL)
		t->declaringBundle = (CFURLRef)CFRetain(bundleURL);

	CFTypeRef dv = CFDictionaryGetValue(declaration, CFSTR("UTTypeDescription"));
	if (dv != NULL && CFGetTypeID(dv) == CFStringGetTypeID() && t->description == NULL)
		t->description = CFStringCreateCopy(NULL, (CFStringRef)dv);

	CFArrayRef conf = stringArrayFromValue(CFDictionaryGetValue(declaration, CFSTR("UTTypeConformsTo")));
	if (conf != NULL) {
		for (CFIndex i = 0; i < CFArrayGetCount(conf); i++) addConforms(t, CFArrayGetValueAtIndex(conf, i));
		CFRelease(conf);
	}

	CFTypeRef spec = CFDictionaryGetValue(declaration, CFSTR("UTTypeTagSpecification"));
	if (spec != NULL && CFGetTypeID(spec) == CFDictionaryGetTypeID())
		CFDictionaryApplyFunction((CFDictionaryRef)spec, addTagsFromSpec, t);
}

/* ============================================================================================ *
 * THE BUILT-IN SEED                                                                                   *
 * ============================================================================================ */
typedef struct {
	const char *ident;
	const char *desc;
	const char *conforms[4];
	const char *exts[5];
	const char *mimes[3];
} AGSeed;

static const AGSeed kSeeds[] = {
	{ "public.item",                     "Item",                  { NULL },                                             { NULL },                         { NULL } },
	{ "public.content",                  "Content",               { "public.item", NULL },                              { NULL },                         { NULL } },
	{ "public.data",                     "Data",                  { "public.item", NULL },                              { NULL },                         { NULL } },
	{ "public.text",                     "Text",                  { "public.data", "public.content", NULL },            { NULL },                         { NULL } },
	{ "public.plain-text",               "Plain Text",            { "public.text", NULL },                              { "txt", "text", NULL },          { "text/plain", NULL } },
	{ "public.utf8-plain-text",          "UTF-8 Plain Text",      { "public.plain-text", NULL },                        { NULL },                         { NULL } },
	{ "public.rtf",                      "Rich Text Format",      { "public.text", NULL },                              { "rtf", NULL },                  { "text/rtf", "application/rtf", NULL } },
	{ "public.html",                     "HTML",                  { "public.text", NULL },                              { "html", "htm", NULL },          { "text/html", NULL } },
	{ "public.xml",                      "XML",                   { "public.text", NULL },                              { "xml", NULL },                  { "text/xml", "application/xml", NULL } },
	{ "public.source-code",              "Source Code",           { "public.plain-text", NULL },                        { "c", "h", "m", "mm", NULL },    { NULL } },
	{ "public.image",                    "Image",                 { "public.data", "public.content", NULL },            { NULL },                         { NULL } },
	{ "public.jpeg",                     "JPEG image",            { "public.image", NULL },                             { "jpg", "jpeg", "jpe", NULL },   { "image/jpeg", NULL } },
	{ "public.png",                      "PNG image",             { "public.image", NULL },                             { "png", NULL },                  { "image/png", NULL } },
	{ "public.tiff",                     "TIFF image",            { "public.image", NULL },                             { "tif", "tiff", NULL },          { "image/tiff", NULL } },
	{ "public.gif",                      "GIF image",             { "public.image", NULL },                             { "gif", NULL },                  { "image/gif", NULL } },
	{ "public.pdf",                      "PDF document",          { "public.data", "public.content", NULL },            { "pdf", NULL },                  { "application/pdf", NULL } },
	{ "public.audio",                    "Audio",                 { "public.data", "public.content", NULL },            { NULL },                         { NULL } },
	{ "public.mp3",                      "MP3 audio",             { "public.audio", NULL },                             { "mp3", NULL },                  { "audio/mpeg", NULL } },
	{ "public.mpeg-4-audio",             "MPEG-4 audio",          { "public.audio", NULL },                             { "m4a", "aac", NULL },           { "audio/mp4", NULL } },
	{ "public.movie",                    "Movie",                 { "public.data", "public.content", NULL },            { NULL },                         { NULL } },
	{ "public.mpeg-4",                   "MPEG-4 movie",          { "public.movie", NULL },                             { "mp4", "m4v", NULL },           { "video/mp4", NULL } },
	{ "public.directory",                "Directory",             { "public.item", NULL },                              { NULL },                         { NULL } },
	{ "public.folder",                   "Folder",                { "public.directory", NULL },                         { NULL },                         { NULL } },
	{ "public.volume",                   "Volume",                { "public.item", NULL },                              { NULL },                         { NULL } },
	{ "com.apple.bundle",                "Bundle",                { "public.directory", NULL },                         { NULL },                         { NULL } },
	{ "com.apple.package",               "Package",               { "com.apple.bundle", NULL },                         { NULL },                         { NULL } },
	{ "com.apple.application",           "Application",           { "com.apple.bundle", NULL },                         { NULL },                         { NULL } },
	{ "com.apple.application-bundle",    "Application bundle",    { "com.apple.application", "com.apple.package", NULL }, { "app", NULL },                { NULL } },
	{ "public.executable",               "Executable",            { "public.data", NULL },                              { NULL },                         { NULL } },
	{ "public.unix-executable",          "Unix executable",       { "public.executable", "public.data", NULL },         { NULL },                         { NULL } },
	{ "public.url",                      "URL",                   { "public.data", NULL },                              { NULL },                         { NULL } },
	{ "public.file-url",                 "File URL",              { "public.url", NULL },                               { NULL },                         { NULL } },
	{ "public.symlink",                  "Symbolic link",         { "public.item", NULL },                              { NULL },                         { NULL } },
	{ "public.alias",                    "Alias",                 { "public.data", NULL },                              { NULL },                         { NULL } },
	{ "public.archive",                  "Archive",               { "public.data", NULL },                              { NULL },                         { NULL } },
	{ "public.zip-archive",              "ZIP archive",           { "public.archive", NULL },                           { "zip", NULL },                  { "application/zip", NULL } },
	{ "public.tar-archive",              "Tar archive",           { "public.archive", NULL },                           { "tar", NULL },                  { "application/x-tar", NULL } },
};

static void seedTypes(void)
{
	for (size_t i = 0; i < sizeof(kSeeds) / sizeof(kSeeds[0]); i++) {
		const AGSeed *s = &kSeeds[i];
		AGUTType *t = ensureTypeC(s->ident);
		if (t->description == NULL) t->description = AGStr(s->desc);
		for (size_t j = 0; s->conforms[j] != NULL; j++) { CFStringRef c = AGStr(s->conforms[j]); addConforms(t, c); CFRelease(c); }
		for (size_t j = 0; s->exts[j] != NULL; j++)    { CFStringRef e = AGStr(s->exts[j]);     addTag(t, kUTTagClassFilenameExtension, e); CFRelease(e); }
		for (size_t j = 0; s->mimes[j] != NULL; j++)   { CFStringRef m = AGStr(s->mimes[j]);    addTag(t, kUTTagClassMIMEType, m); CFRelease(m); }
	}
}

void _AGUTTypeInitialize(void)
{
	if (g_seeded) return;
	g_seeded = true;
	if (g_tagIndex == NULL)
		g_tagIndex = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	seedTypes();
}

void _AGUTTypeReset(void)
{
	for (size_t i = 0; i < g_typeCount; i++) {
		AGUTType *t = g_types[i];
		CFRelease(t->identifier);
		if (t->description != NULL) CFRelease(t->description);
		CFRelease(t->conformsTo);
		CFRelease(t->tags);
		if (t->declaringBundle != NULL) CFRelease(t->declaringBundle);
		free(t);
	}
	g_typeCount = 0;
	if (g_tagIndex != NULL) { CFRelease(g_tagIndex); g_tagIndex = NULL; }
	g_seeded = false;
	_AGUTTypeInitialize();
}

/* ============================================================================================ *
 * CONFORMANCE AND DESCRIPTION                                                                         *
 * ============================================================================================ */
static bool conformsDepth(CFStringRef uti, CFStringRef super, int depth)
{
	if (depth > 32) return false;			/* a cycle, tolerated rather than hung on */
	if (CFEqual(uti, super)) return true;
	AGUTType *t = findType(uti);
	if (t == NULL) return false;
	for (CFIndex i = 0; i < CFArrayGetCount(t->conformsTo); i++)
		if (conformsDepth(CFArrayGetValueAtIndex(t->conformsTo, i), super, depth + 1)) return true;
	return false;
}

bool _AGUTTypeConforms(CFStringRef uti, CFStringRef super)
{
	if (uti == NULL || super == NULL) return false;
	return conformsDepth(uti, super, 0);
}

CFStringRef _AGUTTypeCopyDescription(CFStringRef uti)
{
	AGUTType *t = findType(uti);
	if (t != NULL && t->description != NULL) return (CFStringRef)CFRetain(t->description);
	return (CFStringRef)CFRetain(uti);
}

/* Apple's `dyn.` fallback. THE ENCODING IS THIS TREE'S — Apple's is private — so it is a stated choice:
 * seven base-32 characters of an FNV-1a hash over the tag class, a separator and the tag. It is stable
 * within a run and across runs for the same input, which is all a caller can depend on. */
static CFStringRef dynamicIdentifier(CFStringRef tagClass, CFStringRef tag)
{
	char cls[256], tg[256];
	CFStringGetCString(tagClass, cls, sizeof cls, kCFStringEncodingUTF8);
	CFStringGetCString(tag, tg, sizeof tg, kCFStringEncodingUTF8);
	uint32_t h = 2166136261u;
	for (const char *p = cls; *p; p++) { h ^= (unsigned char)*p; h *= 16777619u; }
	h ^= 0x1f; h *= 16777619u;
	for (const char *p = tg; *p; p++)  { h ^= (unsigned char)*p; h *= 16777619u; }
	static const char *alpha = "0123456789abcdefghjkmnpqrstvwxyz";
	char body[8];
	for (int i = 6; i >= 0; i--) body[6 - i] = alpha[(h >> (i * 5)) & 31];
	body[7] = '\0';
	char full[16];
	snprintf(full, sizeof full, "dyn.%s", body);
	return AGStr(full);
}

static CFStringRef preferredIdentifier(CFStringRef tagClass, CFStringRef tag, CFStringRef conformingTo)
{
	if (tagClass == NULL || tag == NULL) return NULL;
	CFStringRef lower = AGLower(tag);
	CFMutableArrayRef ids = NULL;
	CFDictionaryRef byTag = (CFDictionaryRef)CFDictionaryGetValue(g_tagIndex, tagClass);
	if (byTag != NULL) ids = (CFMutableArrayRef)CFDictionaryGetValue(byTag, lower);
	CFStringRef chosen = NULL;
	if (ids != NULL) {
		for (CFIndex i = 0; i < CFArrayGetCount(ids); i++) {
			CFStringRef id = CFArrayGetValueAtIndex(ids, i);
			if (conformingTo == NULL || _AGUTTypeConforms(id, conformingTo)) { chosen = (CFStringRef)CFRetain(id); break; }
		}
	}
	CFRelease(lower);
	if (chosen != NULL) return chosen;
	return dynamicIdentifier(tagClass, tag);
}

static CFStringRef copyPreferredTag(CFStringRef uti, CFStringRef tagClass)
{
	AGUTType *t = findType(uti);
	if (t == NULL) return NULL;
	CFArrayRef arr = (CFArrayRef)CFDictionaryGetValue(t->tags, tagClass);
	if (arr != NULL && CFArrayGetCount(arr) > 0) return (CFStringRef)CFRetain(CFArrayGetValueAtIndex(arr, 0));
	for (CFIndex i = 0; i < CFArrayGetCount(t->conformsTo); i++) {
		CFStringRef r = copyPreferredTag(CFArrayGetValueAtIndex(t->conformsTo, i), tagClass);
		if (r != NULL) return r;
	}
	return NULL;
}

/* ============================================================================================ *
 * THE PUBLIC DOORS                                                                                    *
 * ============================================================================================ */
CFStringRef UTTypeCreatePreferredIdentifierForTag(CFStringRef inTagClass, CFStringRef inTag, CFStringRef inConformingToUTI)
{
	_AGLSInitialize();
	return preferredIdentifier(inTagClass, inTag, inConformingToUTI);
}

CFArrayRef UTTypeCreateAllIdentifiersForTag(CFStringRef inTagClass, CFStringRef inTag, CFStringRef inConformingToUTI)
{
	_AGLSInitialize();
	CFMutableArrayRef out = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
	if (inTagClass == NULL || inTag == NULL) return out;
	CFStringRef lower = AGLower(inTag);
	CFDictionaryRef byTag = (CFDictionaryRef)CFDictionaryGetValue(g_tagIndex, inTagClass);
	CFMutableArrayRef ids = (byTag != NULL) ? (CFMutableArrayRef)CFDictionaryGetValue(byTag, lower) : NULL;
	if (ids != NULL) {
		for (CFIndex i = 0; i < CFArrayGetCount(ids); i++) {
			CFStringRef id = CFArrayGetValueAtIndex(ids, i);
			if (inConformingToUTI == NULL || _AGUTTypeConforms(id, inConformingToUTI)) CFArrayAppendValue(out, id);
		}
	}
	CFRelease(lower);
	if (CFArrayGetCount(out) == 0) { CFStringRef d = dynamicIdentifier(inTagClass, inTag); CFArrayAppendValue(out, d); CFRelease(d); }
	return out;
}

CFStringRef UTTypeCopyPreferredTagWithClass(CFStringRef inUTI, CFStringRef inTagClass)
{
	_AGLSInitialize();
	if (inUTI == NULL || inTagClass == NULL) return NULL;
	return copyPreferredTag(inUTI, inTagClass);
}

Boolean UTTypeConformsTo(CFStringRef inUTI, CFStringRef inConformsToUTI)
{
	_AGLSInitialize();
	return _AGUTTypeConforms(inUTI, inConformsToUTI) ? true : false;
}

Boolean UTTypeEqual(CFStringRef inUTI1, CFStringRef inUTI2)
{
	if (inUTI1 == NULL || inUTI2 == NULL) return false;
	return CFEqual(inUTI1, inUTI2) ? true : false;
}

CFDictionaryRef UTTypeCopyDeclaration(CFStringRef inUTI)
{
	_AGLSInitialize();
	AGUTType *t = findType(inUTI);
	if (t == NULL) return NULL;
	CFMutableDictionaryRef d = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CFDictionarySetValue(d, CFSTR("UTTypeIdentifier"), t->identifier);
	if (t->description != NULL) CFDictionarySetValue(d, CFSTR("UTTypeDescription"), t->description);
	CFDictionarySetValue(d, CFSTR("UTTypeConformsTo"), t->conformsTo);
	CFDictionarySetValue(d, CFSTR("UTTypeTagSpecification"), t->tags);
	return d;
}

CFStringRef UTTypeCopyDescription(CFStringRef inUTI)
{
	_AGLSInitialize();
	return _AGUTTypeCopyDescription(inUTI);
}

CFURLRef UTTypeCopyDeclaringBundleURL(CFStringRef inUTI)
{
	_AGLSInitialize();
	AGUTType *t = findType(inUTI);
	if (t == NULL || t->declaringBundle == NULL) return NULL;
	return (CFURLRef)CFRetain(t->declaringBundle);
}

/* ============================================================================================ *
 * THE TYPE OF AN ITEM                                                                                 *
 * ============================================================================================ */
CFStringRef _AGUTTypeForURL(CFURLRef url)
{
	if (url == NULL) return (CFStringRef)CFRetain(kUTTypeData);

	bool isDir = false;
	UInt8 path[4096];
	if (CFURLGetFileSystemRepresentation(url, true, path, sizeof path)) {
		struct stat st;
		if (stat((const char *)path, &st) == 0 && S_ISDIR(st.st_mode)) isDir = true;
	}

	CFStringRef ext = CFURLCopyPathExtension(url);
	if (isDir) {
		if (ext != NULL) {
			if (CFStringCompare(ext, CFSTR("app"), kCFCompareCaseInsensitive) == kCFCompareEqualTo) {
				CFRelease(ext);
				return (CFStringRef)CFRetain(kUTTypeApplicationBundle);
			}
			CFRelease(ext);
		}
		return (CFStringRef)CFRetain(kUTTypeFolder);
	}
	if (ext != NULL) {
		CFStringRef uti = UTTypeCreatePreferredIdentifierForTag(kUTTagClassFilenameExtension, ext, NULL);
		CFRelease(ext);
		return uti;
	}
	return (CFStringRef)CFRetain(kUTTypeData);
}
