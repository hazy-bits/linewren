#ifndef LINEWREN_SETTINGS_H
#define LINEWREN_SETTINGS_H

#include <Files.h>
#include <Quickdraw.h>

enum { kSettingsRootLimit = 8 };
enum { kCounterWords, kCounterCharacters, kCounterHidden };

typedef struct {
    short theme;
    short points;
    signed char lineSpacing;
    Boolean sidebarVisible;
    Boolean typewriter;
    Boolean outlineVisible;
    unsigned char counter;
    Rect window;
    Str63 font;
    FSSpec roots[kSettingsRootLimit];
    unsigned rootCount;
} AppSettings;

void SettingsDefaults(AppSettings *settings);
OSErr SettingsLoad(AppSettings *settings);
OSErr SettingsSave(const AppSettings *settings);

#endif
