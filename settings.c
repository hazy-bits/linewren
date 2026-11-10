#include "settings.h"

#include <Aliases.h>
#include <Folders.h>
#include <Memory.h>
#include <string.h>

enum { kSettingsMagic = 'LnwP', kSettingsVersion = 2 };
enum { kSidebarFlag = 1, kTypewriterFlag = 2, kCounterShift = 2, kOutlineFlag = 16 };

typedef struct {
    unsigned long magic;
    unsigned short version;
    short theme;
    short points;
    signed char lineSpacing;
    unsigned char viewFlags;
    Rect window;
    Str63 font;
    unsigned short rootCount;
} SettingsHeader;

static OSErr PreferencesFile(FSSpec *file)
{
    short volume;
    long directory;
    OSErr error = FindFolder(kOnSystemDisk, kPreferencesFolderType, kCreateFolder,
                             &volume, &directory);
    return error == noErr
        ? FSMakeFSSpec(volume, directory, "\pLinewren Preferences", file)
        : error;
}

void SettingsDefaults(AppSettings *settings)
{
    memset(settings, 0, sizeof(*settings));
    settings->theme = 1;
    settings->points = 13;
    settings->lineSpacing = -2;
    settings->sidebarVisible = true;
    SetRect(&settings->window, 48, 64, 560, 400);
    memcpy(settings->font, "\pVerdana", 8);
}

static OSErr ReadExact(short ref, void *bytes, long length)
{
    long count = length;
    OSErr error = FSRead(ref, &count, bytes);
    return error == noErr && count == length ? noErr : error == noErr ? eofErr : error;
}

static OSErr WriteExact(short ref, const void *bytes, long length)
{
    long count = length;
    OSErr error = FSWrite(ref, &count, bytes);
    return error == noErr && count == length ? noErr : error == noErr ? writErr : error;
}

OSErr SettingsLoad(AppSettings *settings)
{
    SettingsDefaults(settings);
    FSSpec file;
    OSErr error = PreferencesFile(&file);
    if(error != noErr)
        return error;
    short ref;
    error = FSpOpenDF(&file, fsRdPerm, &ref);
    if(error != noErr)
        return error;

    SettingsHeader header;
    error = ReadExact(ref, &header, sizeof(header));
    if(error == noErr && (header.magic != kSettingsMagic ||
                          (header.version != 1 && header.version != kSettingsVersion) ||
                          header.font[0] > 63))
        error = paramErr;
    if(error == noErr) {
        settings->theme = header.theme;
        settings->points = header.points;
        settings->lineSpacing = header.lineSpacing;
        settings->sidebarVisible = (header.viewFlags & kSidebarFlag) != 0;
        if(header.version == kSettingsVersion) {
            settings->typewriter = (header.viewFlags & kTypewriterFlag) != 0;
            settings->outlineVisible = (header.viewFlags & kOutlineFlag) != 0;
            unsigned counter = (header.viewFlags >> kCounterShift) & 3;
            if(counter <= kCounterHidden)
                settings->counter = counter;
        }
        settings->window = header.window;
        memcpy(settings->font, header.font, sizeof(settings->font));
        unsigned count = header.rootCount > kSettingsRootLimit
            ? kSettingsRootLimit : header.rootCount;
        for(unsigned i = 0; i < header.rootCount && error == noErr; ++i) {
            unsigned long size;
            error = ReadExact(ref, &size, sizeof(size));
            if(error == noErr && (size == 0 || size > 64 * 1024))
                error = paramErr;
            Handle alias = error == noErr ? NewHandle(size) : NULL;
            if(error == noErr && alias == NULL)
                error = memFullErr;
            if(error == noErr) {
                HLock(alias);
                error = ReadExact(ref, *alias, size);
                HUnlock(alias);
            }
            if(error == noErr && i < count) {
                Boolean changed;
                FSSpec root;
                if(ResolveAlias(NULL, (AliasHandle)alias, &root, &changed) == noErr)
                    settings->roots[settings->rootCount++] = root;
            }
            if(alias != NULL)
                DisposeHandle(alias);
        }
    }
    OSErr closeError = FSClose(ref);
    return error == noErr ? closeError : error;
}

OSErr SettingsSave(const AppSettings *settings)
{
    FSSpec file;
    OSErr error = PreferencesFile(&file);
    if(error == fnfErr)
        error = FSpCreate(&file, 'Lnwr', 'pref', smSystemScript);
    if(error != noErr)
        return error;
    short ref;
    error = FSpOpenDF(&file, fsWrPerm, &ref);
    if(error != noErr)
        return error;
    error = SetEOF(ref, 0);

    unsigned char viewFlags = (settings->sidebarVisible ? kSidebarFlag : 0) |
                              (settings->typewriter ? kTypewriterFlag : 0) |
                              (settings->outlineVisible ? kOutlineFlag : 0) |
                              (settings->counter << kCounterShift);
    SettingsHeader header = {
        kSettingsMagic, kSettingsVersion, settings->theme, settings->points,
        settings->lineSpacing, viewFlags, settings->window, {0},
        settings->rootCount
    };
    memcpy(header.font, settings->font, sizeof(header.font));
    if(error == noErr)
        error = WriteExact(ref, &header, sizeof(header));
    for(unsigned i = 0; i < settings->rootCount && error == noErr; ++i) {
        AliasHandle alias = NULL;
        error = NewAliasMinimal(&settings->roots[i], &alias);
        if(error == noErr) {
            unsigned long size = GetHandleSize((Handle)alias);
            error = WriteExact(ref, &size, sizeof(size));
            if(error == noErr) {
                HLock((Handle)alias);
                error = WriteExact(ref, *alias, size);
                HUnlock((Handle)alias);
            }
        }
        if(alias != NULL)
            DisposeHandle((Handle)alias);
    }
    OSErr closeError = FSClose(ref);
    if(error == noErr)
        error = closeError;
    if(error == noErr)
        error = FlushVol(NULL, file.vRefNum);
    return error;
}
