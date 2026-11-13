#include "settings.h"

#include <Aliases.h>
#include <Folders.h>
#include <Memory.h>
#include <TextUtils.h>
#include <string.h>

enum { kSettingsMagic = 'LnwP', kSettingsVersion = 3 };
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

void SettingsRememberFile(AppSettings *settings, const FSSpec *file)
{
    FSSpec recent = *file;
    unsigned i;
    for(i = 0; i < settings->recentCount; ++i) {
        const FSSpec *entry = &settings->recent[i];
        if(entry->vRefNum == recent.vRefNum && entry->parID == recent.parID &&
           EqualString(entry->name, recent.name, false, true))
            break;
    }
    if(i == settings->recentCount && i < kRecentFileLimit)
        ++settings->recentCount;
    if(i == kRecentFileLimit)
        --i;
    memmove(settings->recent + 1, settings->recent, i * sizeof(recent));
    settings->recent[0] = recent;
}

static OSErr ReadAliases(short ref, unsigned count, FSSpec *files,
                         unsigned limit, unsigned *resolved)
{
    for(unsigned i = 0; i < count; ++i) {
        unsigned long size;
        OSErr error = ReadExact(ref, &size, sizeof(size));
        if(error != noErr)
            return error;
        if(size == 0 || size > 64 * 1024)
            return paramErr;
        AliasHandle alias = (AliasHandle)NewHandle(size);
        if(alias == NULL)
            return memFullErr;
        HLock((Handle)alias);
        error = ReadExact(ref, *alias, size);
        HUnlock((Handle)alias);
        if(error == noErr && *resolved < limit) {
            Boolean changed;
            if(ResolveAlias(NULL, alias, &files[*resolved], &changed) == noErr)
                ++*resolved;
        }
        DisposeHandle((Handle)alias);
        if(error != noErr)
            return error;
    }
    return noErr;
}

static OSErr WriteAlias(short ref, AliasHandle alias)
{
    unsigned long size = GetHandleSize((Handle)alias);
    OSErr error = WriteExact(ref, &size, sizeof(size));
    if(error == noErr) {
        HLock((Handle)alias);
        error = WriteExact(ref, *alias, size);
        HUnlock((Handle)alias);
    }
    return error;
}

static OSErr WriteRecentFiles(short ref, const AppSettings *settings)
{
    AliasHandle aliases[kRecentFileLimit];
    unsigned short count = 0;
    for(unsigned i = 0; i < settings->recentCount; ++i) {
        AliasHandle alias = NULL;
        if(NewAliasMinimal(&settings->recent[i], &alias) == noErr)
            aliases[count++] = alias;
        else if(alias != NULL)
            DisposeHandle((Handle)alias);
    }
    OSErr error = WriteExact(ref, &count, sizeof(count));
    for(unsigned i = 0; i < count; ++i) {
        if(error == noErr)
            error = WriteAlias(ref, aliases[i]);
        DisposeHandle((Handle)aliases[i]);
    }
    return error;
}

static OSErr LoadSettingsFile(const FSSpec *file, AppSettings *settings)
{
    short ref;
    OSErr error = FSpOpenDF(file, fsRdPerm, &ref);
    if(error != noErr)
        return error;

    SettingsHeader header;
    error = ReadExact(ref, &header, sizeof(header));
    if(error == noErr && (header.magic != kSettingsMagic ||
                          header.version < 1 || header.version > kSettingsVersion ||
                          header.font[0] > 63))
        error = paramErr;
    if(error == noErr) {
        settings->theme = header.theme;
        settings->points = header.points;
        settings->lineSpacing = header.lineSpacing;
        settings->sidebarVisible = (header.viewFlags & kSidebarFlag) != 0;
        if(header.version >= 2) {
            settings->typewriter = (header.viewFlags & kTypewriterFlag) != 0;
            settings->outlineVisible = (header.viewFlags & kOutlineFlag) != 0;
            unsigned counter = (header.viewFlags >> kCounterShift) & 3;
            if(counter <= kCounterHidden)
                settings->counter = counter;
        }
        settings->window = header.window;
        memcpy(settings->font, header.font, sizeof(settings->font));
        error = ReadAliases(ref, header.rootCount, settings->roots, kSettingsRootLimit,
                            &settings->rootCount);
        if(error == noErr && header.version >= 3) {
            unsigned short count;
            error = ReadExact(ref, &count, sizeof(count));
            if(error == noErr)
                error = ReadAliases(ref, count, settings->recent, kRecentFileLimit,
                                    &settings->recentCount);
        }
    }
    OSErr closeError = FSClose(ref);
    return error == noErr ? closeError : error;
}

OSErr SettingsLoad(AppSettings *settings)
{
    SettingsDefaults(settings);
    FSSpec file;
    OSErr error = PreferencesFile(&file);
    return error == noErr ? LoadSettingsFile(&file, settings) : error;
}

static OSErr SaveSettingsFile(const FSSpec *file, const AppSettings *settings)
{
    short ref;
    OSErr error = FSpOpenDF(file, fsWrPerm, &ref);
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
        if(error == noErr)
            error = WriteAlias(ref, alias);
        if(alias != NULL)
            DisposeHandle((Handle)alias);
    }
    if(error == noErr)
        error = WriteRecentFiles(ref, settings);
    OSErr closeError = FSClose(ref);
    if(error == noErr)
        error = closeError;
    if(error == noErr)
        error = FlushVol(NULL, file->vRefNum);
    return error;
}

OSErr SettingsSave(const AppSettings *settings)
{
    FSSpec file;
    OSErr error = PreferencesFile(&file);
    if(error == fnfErr)
        error = FSpCreate(&file, 'Lnwr', 'pref', smSystemScript);
    return error == noErr ? SaveSettingsFile(&file, settings) : error;
}

#ifdef LINEWREN_SMOKE_TEST
Boolean SettingsSmokeRecent(const AppSettings *settings)
{
    const FSSpec *recent = &settings->recent[0];
    FSSpec fixture;
    if(FSMakeFSSpec(recent->vRefNum, recent->parID, "\pLinewren Recent Test", &fixture) != fnfErr ||
       FSpCreate(&fixture, 'Lnwr', 'pref', smSystemScript) != noErr)
        return false;
    AppSettings loaded;
    SettingsDefaults(&loaded);
    Boolean matches = SaveSettingsFile(&fixture, settings) == noErr &&
                      LoadSettingsFile(&fixture, &loaded) == noErr &&
                      loaded.recentCount == settings->recentCount;
    for(unsigned i = 0; matches && i < loaded.recentCount; ++i) {
        matches = loaded.recent[i].parID == settings->recent[i].parID &&
                  EqualString(loaded.recent[i].name, settings->recent[i].name, true, true);
    }
    FSpDelete(&fixture);
    return matches;
}
#endif
