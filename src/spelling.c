#include "spelling.h"

#include <CFBundle.h>
#include <CFPreferences.h>
#include <CFURL.h>
#include <stdlib.h>

typedef void *Object;
typedef void *Selector;
typedef struct { UInt32 location, length; } SpellRange;

/* Carbon's CFM import of CFBundleGetFunctionPointerForName supplies
   transition vectors for Mach-O functions and can resolve loaded symbols. */
static void *gMessage, *gRangeMessage;
static Selector (*gSelect)(const char *);
static Object gChecker, gPoolClass;
static CFBundleRef gAppKit;

static CFBundleRef Framework(CFStringRef path)
{
    CFURLRef url = CFURLCreateWithFileSystemPath(NULL, path, kCFURLPOSIXPathStyle, true);
    if(url == NULL)
        return NULL;
    CFBundleRef bundle = CFBundleCreate(NULL, url);
    CFRelease(url);
    return bundle;
}

static Object NewPool(void)
{
    return ((Object (*)(Object, Selector))gMessage)(gPoolClass, gSelect("new"));
}

static void ReleasePool(Object pool)
{
    if(pool != NULL)
        ((void (*)(Object, Selector))gMessage)(pool, gSelect("release"));
}

Boolean SpellingAvailable(void)
{
    if(gChecker != NULL)
        return true;
    static Boolean tried;
    if(tried)
        return false;
    tried = true;
    gAppKit = Framework(CFSTR("/System/Library/Frameworks/AppKit.framework"));
    if(gAppKit == NULL)
        return false;

    Boolean (*load)(void) = CFBundleGetFunctionPointerForName(gAppKit, CFSTR("NSApplicationLoad"));
    gMessage = CFBundleGetFunctionPointerForName(gAppKit, CFSTR("objc_msgSend"));
    gRangeMessage = CFBundleGetFunctionPointerForName(gAppKit, CFSTR("objc_msgSend_stret"));
    gSelect = CFBundleGetFunctionPointerForName(gAppKit, CFSTR("sel_registerName"));
    Object (*getClass)(const char *) = CFBundleGetFunctionPointerForName(gAppKit, CFSTR("objc_getClass"));
    if(load == NULL || gMessage == NULL || gRangeMessage == NULL || gSelect == NULL ||
       getClass == NULL)
        return false;
    gPoolClass = getClass("NSAutoreleasePool");
    if(gPoolClass == NULL)
        return false;
    Object pool = NewPool();
    if(pool == NULL)
        return false;
    if(load()) {
        Object checkerClass = getClass("NSSpellChecker");
        if(checkerClass != NULL)
            gChecker = ((Object (*)(Object, Selector))gMessage)(checkerClass,
                                                                gSelect("sharedSpellChecker"));
    }
    ReleasePool(pool);
    return gChecker != NULL;
}

static CFStringRef CopyCheckerLanguage(void)
{
    if(!SpellingAvailable())
        return NULL;
    Object pool = NewPool();
    if(pool == NULL)
        return NULL;
    CFStringRef language = ((CFStringRef (*)(Object, Selector))gMessage)(gChecker, gSelect("language"));
    if(language != NULL)
        CFRetain(language);
    ReleasePool(pool);
    return language;
}

CFStringRef SpellingDefaultLanguage(void)
{
    static CFStringRef language;
    if(language == NULL)
        language = CopyCheckerLanguage();
    return language;
}

#if defined(LINEWREN_SMOKE_TEST)
#define SPELLING_PREFERENCES CFSTR("uk.hazybits.linewren.spelling-test")
#else
#define SPELLING_PREFERENCES CFSTR("uk.hazybits.linewren.spelling")
#endif

static CFStringRef CopyFileKey(const FSSpec *file)
{
    FSRef ref;
    if(file == NULL || FSpMakeFSRef(file, &ref) != noErr)
        return NULL;
    CFURLRef url = CFURLCreateFromFSRef(NULL, &ref);
    if(url == NULL)
        return NULL;
    CFStringRef path = CFURLCopyFileSystemPath(url, kCFURLPOSIXPathStyle);
    CFRelease(url);
    return path;
}

CFStringRef SpellingCopyFileLanguage(const FSSpec *file)
{
    CFStringRef key = CopyFileKey(file);
    if(key == NULL)
        return NULL;
    CFPropertyListRef value = CFPreferencesCopyAppValue(key, SPELLING_PREFERENCES);
    CFRelease(key);
    if(value != NULL && CFGetTypeID(value) != CFStringGetTypeID()) {
        CFRelease(value);
        return NULL;
    }
    return (CFStringRef)value;
}

Boolean SpellingSaveFileLanguage(const FSSpec *file, CFStringRef language)
{
    CFStringRef key = CopyFileKey(file);
    if(key == NULL)
        return false;
    CFPreferencesSetAppValue(key, language, SPELLING_PREFERENCES);
    CFRelease(key);
    return CFPreferencesAppSynchronize(SPELLING_PREFERENCES);
}

static Boolean SetCheckerLanguage(CFStringRef language)
{
    if(language == NULL || !SpellingAvailable())
        return false;
    Object pool = NewPool();
    if(pool == NULL)
        return false;
    signed char accepted = ((signed char (*)(Object, Selector, CFStringRef))gMessage)(
        gChecker, gSelect("setLanguage:"), language);
    ReleasePool(pool);
    return accepted != 0;
}

Boolean SpellingLanguageSupported(CFStringRef language)
{
    CFStringRef previous = CopyCheckerLanguage();
    if(previous == NULL)
        return false;
    Boolean accepted = CFEqual(previous, language);
    if(!accepted && SetCheckerLanguage(language)) {
        accepted = true;
        SetCheckerLanguage(previous);
    }
    CFRelease(previous);
    return accepted;
}

static void AppendServiceLanguages(CFMutableArrayRef languages, CFStringRef directory)
{
    CFURLRef url = CFURLCreateWithFileSystemPath(NULL, directory, kCFURLPOSIXPathStyle, true);
    if(url == NULL)
        return;
    CFArrayRef bundles = CFBundleCreateBundlesFromDirectory(NULL, url, CFSTR("service"));
    CFRelease(url);
    if(bundles == NULL)
        return;
    for(CFIndex i = 0; i < CFArrayGetCount(bundles); ++i) {
        CFBundleRef bundle = CFArrayGetValueAtIndex(bundles, i);
        CFTypeRef services = CFBundleGetValueForInfoDictionaryKey(bundle, CFSTR("NSServices"));
        if(services == NULL || CFGetTypeID(services) != CFArrayGetTypeID())
            continue;
        for(CFIndex j = 0; j < CFArrayGetCount(services); ++j) {
            CFTypeRef service = CFArrayGetValueAtIndex(services, j);
            if(CFGetTypeID(service) != CFDictionaryGetTypeID() ||
               !CFDictionaryContainsKey(service, CFSTR("NSSpellChecker")))
                continue;
            CFTypeRef names = CFDictionaryGetValue(service, CFSTR("NSLanguages"));
            if(names == NULL || CFGetTypeID(names) != CFArrayGetTypeID())
                continue;
            for(CFIndex k = 0; k < CFArrayGetCount(names); ++k) {
                CFTypeRef name = CFArrayGetValueAtIndex(names, k);
                if(CFGetTypeID(name) == CFStringGetTypeID() &&
                   !CFArrayContainsValue(languages, CFRangeMake(0, CFArrayGetCount(languages)), name) &&
                   SpellingLanguageSupported(name))
                    CFArrayAppendValue(languages, name);
            }
        }
    }
    CFRelease(bundles);
}

CFArrayRef SpellingCopyLanguages(void)
{
    if(!SpellingAvailable())
        return NULL;
    Object pool = NewPool();
    if(pool == NULL)
        return NULL;
    Selector available = gSelect("availableLanguages");
    CFArrayRef languages = NULL;
    if(((signed char (*)(Object, Selector, Selector))gMessage)(
           gChecker, gSelect("respondsToSelector:"), available)) {
        languages = ((CFArrayRef (*)(Object, Selector))gMessage)(gChecker, available);
        if(languages != NULL)
            CFRetain(languages);
    }
    ReleasePool(pool);
    if(languages != NULL)
        return languages;

    /* Tiger spells through registered services. Read their advertised
       languages rather than probing a list of unrelated locale codes. */
    CFStringRef current = CopyCheckerLanguage();
    if(current == NULL)
        return NULL;
    CFMutableArrayRef supported = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    if(supported != NULL) {
        CFArrayAppendValue(supported, current);
        AppendServiceLanguages(supported, CFSTR("/System/Library/Services"));
        AppendServiceLanguages(supported, CFSTR("/Library/Services"));
        const char *home = getenv("HOME");
        if(home != NULL) {
            CFStringRef path = CFStringCreateWithFormat(NULL, NULL, CFSTR("%s/Library/Services"), home);
            if(path != NULL) {
                AppendServiceLanguages(supported, path);
                CFRelease(path);
            }
        }
    }
    CFRelease(current);
    return supported;
}

OSStatus SpellingNext(CFStringRef text, CFIndex offset, CFStringRef language, CFRange *match)
{
    *match = CFRangeMake(0, 0);
    Object pool = NewPool();
    if(pool == NULL)
        return memFullErr;
    SpellRange found = {0};
    /* The 32-bit PowerPC Objective-C ABI passes the result address first. */
    ((void (*)(SpellRange *, Object, Selector, CFStringRef, SInt32, Object, Boolean, SInt32, SInt32 *))
        gRangeMessage)(&found, gChecker,
                        gSelect("checkSpellingOfString:startingAt:language:wrap:inSpellDocumentWithTag:wordCount:"),
                        text, offset, language, false, 0, NULL);
    ReleasePool(pool);
    UInt32 length = CFStringGetLength(text);
    if(found.length == 0)
        return noErr;
    if(found.location < (UInt32)offset || found.location >= length ||
       found.length > length - found.location)
        return paramErr;
    *match = CFRangeMake(found.location, found.length);
    return noErr;
}

CFArrayRef SpellingSuggestions(CFStringRef word, CFStringRef language)
{
    Object pool = NewPool();
    if(pool == NULL)
        return NULL;
    CFStringRef previous = CopyCheckerLanguage();
    Boolean change = language != NULL && previous != NULL && !CFEqual(language, previous);
    CFArrayRef guesses = NULL;
    if(language == NULL || (previous != NULL && (!change || SetCheckerLanguage(language))))
        guesses = ((CFArrayRef (*)(Object, Selector, CFStringRef))gMessage)(
            gChecker, gSelect("guessesForWord:"), word);
    if(guesses != NULL)
        CFRetain(guesses);
    if(change)
        SetCheckerLanguage(previous);
    if(previous != NULL)
        CFRelease(previous);
    ReleasePool(pool);
    return guesses;
}
