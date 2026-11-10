#ifndef LINEWREN_MAC_TOOLBOX_H
#define LINEWREN_MAC_TOOLBOX_H

#include <Quickdraw.h>
#include <Controls.h>
#include <Menus.h>
#include <Scrap.h>
#include <Windows.h>

static inline Rect WindowCanvas(WindowRef window)
{
    Rect bounds;
    GetWindowPortBounds(window, &bounds);
    return bounds;
}

static inline Boolean ControlVisible(ControlRef control)
{
#if TARGET_API_MAC_CARBON
    return IsControlVisible(control);
#else
    return (*control)->contrlVis != 0;
#endif
}

#if !TARGET_API_MAC_CARBON
#define EnableMenuItem(menu, item) EnableItem(menu, item)
#define DisableMenuItem(menu, item) DisableItem(menu, item)
#endif

static inline void InvalidateRect(const Rect *bounds)
{
#if TARGET_API_MAC_CARBON
    GrafPtr port;
    GetPort(&port);
    WindowRef window = GetWindowFromPort((CGrafPtr)port);
    if(window != NULL)
        InvalWindowRect(window, bounds);
#else
    InvalRect(bounds);
#endif
}

static inline void InvalidateWindow(WindowRef window)
{
    Rect bounds = WindowCanvas(window);
#if TARGET_API_MAC_CARBON
    InvalWindowRect(window, &bounds);
#else
    InvalRect(&bounds);
#endif
}

static inline Rect MainScreenBounds(void)
{
#if TARGET_API_MAC_CARBON
    BitMap screen;
    GetQDGlobalsScreenBits(&screen);
    return screen.bounds;
#else
    return qd.screenBits.bounds;
#endif
}

static inline long ReadScrap(Handle data, ResType type)
{
#if TARGET_API_MAC_CARBON
    ScrapRef scrap;
    Size size;
    if(GetCurrentScrap(&scrap) != noErr ||
       GetScrapFlavorSize(scrap, type, &size) != noErr)
        return 0;
    if(data != NULL && size > 0) {
        SetHandleSize(data, size);
        if(MemError() != noErr)
            return 0;
        HLock(data);
        OSStatus error = GetScrapFlavorData(scrap, type, &size, *data);
        HUnlock(data);
        if(error != noErr)
            return 0;
    }
    return size;
#else
    long offset;
    return GetScrap(data, type, &offset);
#endif
}

#endif
