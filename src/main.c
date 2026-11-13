#include "mac-toolbox.h"
#include "editor.h"
#include "settings.h"
#include "text-files.h"
#include "workspace.h"
#if TARGET_API_MAC_CARBON
#include <CarbonEvents.h>
#include <CFURL.h>
#include <Navigation.h>
#else
#include <InputSprocket.h>
#include <StandardFile.h>
#endif

#include <Devices.h>
#include <AppleEvents.h>
#include <Dialogs.h>
#include <Drag.h>
#include <Events.h>
#include <Fonts.h>
#include <Memory.h>
#include <Menus.h>
#include <OSUtils.h>
#include <Quickdraw.h>
#include <Resources.h>
#include <Scrap.h>
#include <TextEdit.h>
#include <TextUtils.h>
#include <NumberFormatting.h>
#include <Sound.h>
#include <Windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef LINEWREN_SMOKE_TEST
#include "classic-test.h"
#endif

enum {
    kAppleMenu = 128,
    kFileMenu = 129,
    kEditMenu = 130,
    kViewMenu = 131,
    kFontMenu = 133,
    kSizeMenu = 134,
    kWindowMenu = 137,
    kRecentMenu = 138
};

enum { kFindItem = 12, kFindNextItem = 13, kFindPreviousItem = 14, kReplaceItem = 15 };
static Str255 gFindText;
static Str255 gReplaceText;
enum { kFindRow = 24 };
static struct {
    WindowRef window;
    Boolean expanded, replaceField, selectAll;
    unsigned caret, count, ordinal;
} gFind;
static void CloseFind(void);
static Rect FindBounds(WindowRef window);

enum {
    kAboutItem = 1,
    kNewItem = 1,
    kOpenItem = 2,
    kOpenRecentItem = 3,
    kQuickOpenItem = 4,
    kOpenFolderItem = 5,
    kCloseItem = 7,
    kSaveItem = 8,
    kSaveAsItem = 9,
    kQuitItem = 11
};

#if TARGET_API_MAC_CARBON
enum { kMinimizeItem = 1, kZoomItem = 2 };
#else
enum { kZoomItem = 1 };
#endif
enum { kFirstDocumentItem = kZoomItem + 2 };
static MenuRef gWindowMenu, gRecentMenu;
static OSStatus ZoomDocument(WindowRef window, short part);
static Rect gNextWindowRect;
static Boolean gRunning = true;
static AppSettings gSettings;

enum { kQuickResultLimit = 8, kQuickRowHeight = 18, kQuickFieldHeight = 28 };
enum { kPanelInset = 6, kPanelWidth = 360 };
static struct {
    Boolean visible;
    WindowRef window;
    Str63 query;
    FSSpec files[kQuickResultLimit];
    unsigned count;
    unsigned selected;
} gQuickOpen;
static void DismissQuickOpen(void);
static DragTrackingHandlerUPP gTrackDrop;
static DragReceiveHandlerUPP gReceiveDrop;
static struct {
    DragRef drag;
    WindowRef source;
    Rect caret;
} gTextDrag;

enum {
    kLightTheme = 1, kDarkTheme = 2, kSidebarItem = 4, kFocusItem = 5,
    kTypewriterItem = 6, kFirstCounterItem = 8, kOutlineItem = 12
};
static Boolean gSidebarVisible = true;
static Boolean gFocusMode;

static const EditorTheme kThemes[] = {
    {
        .page = {0xf0f0, 0xf0f0, 0xf0f0},
        .text = {0x3838, 0x3838, 0x3838},
        .marker = {0x8585, 0x8585, 0x8585},
        .secondary = {0x6666, 0x6666, 0x6666},
        .caret = {0x2424, 0x6464, 0xc9c9},
        .panel = {0xe7e7, 0xe7e7, 0xe7e7},
        .divider = {0xcdcd, 0xcdcd, 0xcdcd}
    }, {
        .page = {0x1919, 0x1919, 0x1919},
        .text = {0xbdbd, 0xbdbd, 0xbdbd},
        .marker = {0x7474, 0x7474, 0x7474},
        .secondary = {0x8c8c, 0x8c8c, 0x8c8c},
        .caret = {0x6161, 0x9494, 0xe0e0},
        .panel = {0x2222, 0x2222, 0x2222},
        .divider = {0x3838, 0x3838, 0x3838}
    }
};
static short gTheme = kLightTheme;

static void UpdateThemeMenu(void)
{
    MenuRef menu = GetMenu(kViewMenu);
    CheckMenuItem(menu, kLightTheme, gTheme == kLightTheme);
    CheckMenuItem(menu, kDarkTheme, gTheme == kDarkTheme);
    CheckMenuItem(menu, kSidebarItem, gSidebarVisible);
    CheckMenuItem(menu, kFocusItem, gFocusMode);
    CheckMenuItem(menu, kTypewriterItem, gSettings.typewriter);
    CheckMenuItem(menu, kOutlineItem, gSettings.outlineVisible);
    for(short i = kCounterWords; i <= kCounterHidden; ++i)
        CheckMenuItem(menu, kFirstCounterItem + i, gSettings.counter == i);
}

enum { kLargerItem = 1, kSmallerItem = 2, kFirstSizeItem = 4 };
static const short kSizes[] = {10, 12, 13, 14, 16, 18, 20, 24};
static EditorTypography gTypography;
static MenuRef gFontMenu, gSizeMenu;
static short gFontItem;

static short FindFontItem(ConstStr255Param name)
{
    Str255 label;
    for(short item = 1; item <= CountMenuItems(gFontMenu); ++item) {
        GetMenuItemText(gFontMenu, item, label);
        if(EqualString(name, label, false, true))
            return item;
    }
    return 0;
}

static short SizeItem(short points)
{
    for(unsigned i = 0; i < sizeof(kSizes) / sizeof(*kSizes); ++i) {
        if(kSizes[i] == points)
            return kFirstSizeItem + i;
    }
    return 0;
}

static void UpdateTypographyMenus(void)
{
    for(short item = 1, count = CountMenuItems(gFontMenu); item <= count; ++item)
        CheckMenuItem(gFontMenu, item, item == gFontItem);
    short size = SizeItem(gTypography.points);
    short last = CountMenuItems(gSizeMenu);
    for(short item = kFirstSizeItem; item <= last; ++item)
        CheckMenuItem(gSizeMenu, item, item == size);
    if(size < last)
        EnableMenuItem(gSizeMenu, kLargerItem);
    else
        DisableMenuItem(gSizeMenu, kLargerItem);
    if(size > kFirstSizeItem)
        EnableMenuItem(gSizeMenu, kSmallerItem);
    else
        DisableMenuItem(gSizeMenu, kSmallerItem);
}

static short FeatureFont(ConstStr255Param name, short featured)
{
    short item = FindFontItem(name);
    if(item == 0)
        return featured;
    DeleteMenuItem(gFontMenu, item);
    InsertMenuItem(gFontMenu, "\pX", featured);
    SetMenuItemText(gFontMenu, ++featured, name);
    return featured;
}

static Boolean InitTypographyMenus(void)
{
    gTypography = EditorDefaultTypography();
    gFontMenu = NewMenu(kFontMenu, "\pFont");
    gSizeMenu = NewMenu(kSizeMenu, "\pSize");
    if(gFontMenu == NULL || gSizeMenu == NULL)
        return false;

    AppendResMenu(gFontMenu, 'FOND');
    static const ConstStr255Param favorites[] = {
        "\pVerdana", "\pGeneva", "\pChicago", "\pCharcoal", "\pArial", "\pHelvetica",
        "\pGeorgia", "\pNew York", "\pTimes", "\pMonaco", "\pCourier"
    };
    short featured = 0;
    for(unsigned i = 0; i < sizeof(favorites) / sizeof(*favorites); ++i)
        featured = FeatureFont(favorites[i], featured);
    if(featured != 0 && featured < CountMenuItems(gFontMenu))
        InsertMenuItem(gFontMenu, "\p-", featured);

    Str255 name;
    GetFontName(gTypography.family, name);
    gFontItem = FindFontItem(name);
    if(gFontItem == 0 && name[0] != 0) {
        InsertMenuItem(gFontMenu, "\pX", 0);
        SetMenuItemText(gFontMenu, 1, name);
        gFontItem = 1;
    }
    AppendMenu(gSizeMenu, "\pLarger/+;Smaller/-;-");
    for(unsigned i = 0; i < sizeof(kSizes) / sizeof(*kSizes); ++i) {
        Str255 label;
        NumToString(kSizes[i], label);
        AppendMenu(gSizeMenu, label);
    }
    UpdateTypographyMenus();
    InsertMenu(gFontMenu, 0);
    InsertMenu(gSizeMenu, 0);
    DrawMenuBar();
    return true;
}

static Editor *GetDocument(WindowRef window)
{
    if(window == NULL || GetWindowKind(window) < 0)
        return NULL;
    return (Editor *)GetWRefCon(window);
}

static WindowRef FirstWindow(void)
{
#if TARGET_API_MAC_CARBON
    return GetWindowList();
#else
    return FrontWindow();
#endif
}

static Boolean InitDocumentMenus(void)
{
    gWindowMenu = GetMenu(kWindowMenu);
    gRecentMenu = NewMenu(kRecentMenu, "\pOpen Recent");
    if(gWindowMenu == NULL || gRecentMenu == NULL)
        return false;
#if TARGET_API_MAC_CARBON
    InsertMenuItem(gWindowMenu, "\pMinimize/M", 0);
#endif
    InsertMenu(gWindowMenu, 0);
    InsertMenu(gRecentMenu, hierMenu);
    SetMenuItemHierarchicalID(GetMenu(kFileMenu), kOpenRecentItem, kRecentMenu);
    SetMenuItemModifiers(GetMenu(kFileMenu), kSaveAsItem, kMenuShiftModifier);
    SetMenuItemModifiers(GetMenu(kEditMenu), kRedoItem, kMenuShiftModifier);
    SetMenuItemModifiers(GetMenu(kEditMenu), kFindPreviousItem, kMenuShiftModifier);
    return true;
}

static void AppendMenuLabel(MenuRef menu, ConstStr255Param label)
{
    AppendMenu(menu, "\pX");
    SetMenuItemText(menu, CountMenuItems(menu), label);
}

static void RefreshDocumentMenus(void)
{
    while(CountMenuItems(gRecentMenu) > 0)
        DeleteMenuItem(gRecentMenu, CountMenuItems(gRecentMenu));
    for(unsigned i = 0; i < gSettings.recentCount; ++i)
        AppendMenuLabel(gRecentMenu, gSettings.recent[i].name);
    if(gSettings.recentCount > 0)
        AppendMenu(gRecentMenu, "\p-;Clear Menu");

    while(CountMenuItems(gWindowMenu) >= kFirstDocumentItem)
        DeleteMenuItem(gWindowMenu, CountMenuItems(gWindowMenu));
    for(WindowRef window = FirstWindow(); window != NULL; window = GetNextWindow(window)) {
        if(GetDocument(window) != NULL) {
            Str255 title;
            GetWTitle(window, title);
            AppendMenuLabel(gWindowMenu, title);
            CheckMenuItem(gWindowMenu, CountMenuItems(gWindowMenu), window == FrontWindow());
        }
    }
}

static void SelectDocument(WindowRef window)
{
    if(IsWindowCollapsed(window))
        CollapseWindow(window, false);
    SelectWindow(window);
}

enum { kOutlineWidth = 148, kOutlineFirstRow = 4, kOutlineRowHeight = 16, kOutlinePager = 22 };

static Rect EditorBounds(WindowRef window)
{
    Rect bounds = WindowCanvas(window);
    if(gSidebarVisible && !gFocusMode)
        bounds.left += kSidebarWidth;
    if(!gFocusMode)
        bounds.bottom -= 23;
    bounds.left += 22;
    bounds.right -= 2;
    bounds.top += 17;
    bounds.bottom -= 17;
    return bounds;
}

static Rect OutlineBounds(Rect view)
{
    view.right -= kEditorScrollbarWidth;
    short width = view.right - view.left - 180;
    if(!gSettings.outlineVisible || gFocusMode || width < 94 ||
       view.bottom - view.top < kOutlineFirstRow + kOutlineRowHeight + kOutlinePager)
        return (Rect){0};
    view.left = view.right - (width < kOutlineWidth ? width : kOutlineWidth);
    return view;
}

static void LayoutEditor(WindowRef window)
{
    Rect view = EditorBounds(window);
    Rect outline = OutlineBounds(view);
    EditorResize(GetDocument(window), &view, &outline);
}

static void InvalidateFooter(WindowRef window)
{
    if(gFocusMode)
        return;
    Rect footer = WindowCanvas(window);
    footer.top = footer.bottom - 23;
    InvalidateRect(&footer);
}

static void ApplyViewLayout(void)
{
    GrafPtr port;
    GetPort(&port);
    for(WindowRef window = FrontWindow(); window != NULL; window = GetNextWindow(window)) {
        Editor *editor = GetDocument(window);
        if(editor != NULL) {
            SetPortWindowPort(window);
            LayoutEditor(window);
            WorkspaceRefreshSidebar(EditorWorkspace(editor), window);
            InvalidateWindow(window);
        }
    }
    SetPort(port);
    UpdateThemeMenu();
}

static WindowRef NewDocument(OSStatus *outStatus)
{
    WindowRef window;
    Editor *editor;
    Rect view;
    OSStatus status;
    Str255 title = "\pUntitled";

    Rect screen = MainScreenBounds();
    if(gNextWindowRect.right > screen.right - 8 || gNextWindowRect.bottom > screen.bottom - 8)
        SetRect(&gNextWindowRect, 48, 64, 560, 400);

#if TARGET_API_MAC_CARBON
    status = CreateNewWindow(kDocumentWindowClass, kWindowStandardDocumentAttributes,
                             &gNextWindowRect, &window);
    if(status != noErr) {
        if(outStatus != NULL)
            *outStatus = status;
        return NULL;
    }
    SetWTitle(window, title);
#else
    window = NewCWindow(NULL, &gNextWindowRect, title, false,
                        zoomDocProc, (WindowPtr)-1, true, 0);
    if(window == NULL) {
        if(outStatus != NULL)
            *outStatus = memFullErr;
        return NULL;
    }
#endif

    SetPortWindowPort(window);
    view = EditorBounds(window);
    editor = EditorNew(window, &view, &status);
    if(outStatus != NULL)
        *outStatus = status;
    if(editor == NULL) {
        DisposeWindow(window);
        return NULL;
    }

    SetWRefCon(window, (long)editor);
    EditorSetTheme(editor, &kThemes[gTheme - 1]);
    WorkspaceRestoreFolders(EditorWorkspace(editor), window,
                            gSettings.roots, gSettings.rootCount);
    EditorSetTypography(editor, gTypography);
    EditorSetTypewriter(editor, gSettings.typewriter);
    Rect outline = OutlineBounds(view);
    if(!EmptyRect(&outline))
        EditorResize(editor, &view, &outline);
    status = memFullErr;
    if(gTrackDrop != NULL && gReceiveDrop != NULL) {
        status = InstallTrackingHandler(gTrackDrop, window, NULL);
        if(status == noErr) {
            status = InstallReceiveHandler(gReceiveDrop, window, NULL);
            if(status != noErr)
                RemoveTrackingHandler(gTrackDrop, window);
        }
    }
    if(outStatus != NULL)
        *outStatus = status;
    ShowWindow(window);
    EditorActivate(editor, true);

    OffsetRect(&gNextWindowRect, 18, 18);
    return window;
}

static void ApplySettings(void)
{
    gTheme = gSettings.theme == kDarkTheme ? kDarkTheme : kLightTheme;
    gSidebarVisible = gSettings.sidebarVisible;
    gNextWindowRect = gSettings.window;
    if(gNextWindowRect.right - gNextWindowRect.left < 400 ||
       gNextWindowRect.bottom - gNextWindowRect.top < 260)
        SetRect(&gNextWindowRect, 48, 64, 560, 400);

    short family, savedFont = FindFontItem(gSettings.font);
    GetFNum(gSettings.font, &family);
    if(savedFont != 0 && EditorFontAvailable(family)) {
        gTypography.family = family;
        gFontItem = savedFont;
    } else {
        Str255 name;
        GetFontName(gTypography.family, name);
        gFontItem = FindFontItem(name);
    }
    if(SizeItem(gSettings.points) != 0)
        gTypography.points = gSettings.points;
    if(gSettings.lineSpacing >= -8 && gSettings.lineSpacing <= 8)
        gTypography.lineSpacing = gSettings.lineSpacing;
    UpdateTypographyMenus();
    UpdateThemeMenu();
}

static void CaptureWindowSettings(WindowRef window)
{
    Editor *editor = GetDocument(window);
    if(editor == NULL)
        return;
    Point origin = {0, 0};
    SetPortWindowPort(window);
    LocalToGlobal(&origin);
    SetRect(&gSettings.window, origin.h, origin.v,
            origin.h + WindowCanvas(window).right, origin.v + WindowCanvas(window).bottom);
    gSettings.rootCount = WorkspaceCopyFolders(EditorWorkspace(editor), gSettings.roots,
                                                kSettingsRootLimit);
}

static void SaveSettings(void)
{
    gSettings.theme = gTheme;
    gSettings.sidebarVisible = gSidebarVisible;
    gSettings.points = gTypography.points;
    gSettings.lineSpacing = gTypography.lineSpacing;
    Str255 font;
    GetFontName(gTypography.family, font);
    gSettings.font[0] = font[0] < sizeof(gSettings.font) ? font[0] : 0;
    memcpy(gSettings.font + 1, font + 1, gSettings.font[0]);
    CaptureWindowSettings(FrontWindow());
    SettingsSave(&gSettings);
}

static void CloseDocument(WindowRef window)
{
    Editor *editor;

    if(window == NULL)
        return;
    editor = GetDocument(window);
    if(editor != NULL) {
        if(gTrackDrop != NULL)
            RemoveTrackingHandler(gTrackDrop, window);
        if(gReceiveDrop != NULL)
            RemoveReceiveHandler(gReceiveDrop, window);
        if(gQuickOpen.visible && gQuickOpen.window == window)
            DismissQuickOpen();
        if(gFind.window == window)
            CloseFind();
        CaptureWindowSettings(window);
        SetPortWindowPort(window);
        WorkspaceFinishDocument(EditorWorkspace(editor));
        EditorDelete(editor);
    }
    DisposeWindow(window);
}

static Boolean CanClose(WindowRef window)
{
    Editor *editor = GetDocument(window);
    if(editor != NULL) {
        SelectWindow(window);
        return WorkspaceCanClose(editor, window);
    }
    return true;
}

static void RememberDocument(Editor *editor)
{
    const FSSpec *file = WorkspaceCurrentFile(EditorWorkspace(editor));
    if(file != NULL)
        SettingsRememberFile(&gSettings, file);
}

static OSErr OpenDocument(WindowRef source, const FSSpec *file)
{
    CloseFind();
    for(WindowRef window = FirstWindow(); window != NULL; window = GetNextWindow(window)) {
        Editor *editor = GetDocument(window);
        if(editor != NULL && WorkspaceHasFile(EditorWorkspace(editor), file)) {
            SelectDocument(window);
            RememberDocument(editor);
            return noErr;
        }
    }
    WindowRef previous = FrontWindow();
    Boolean newWindow = source == NULL;
    if(newWindow)
        source = NewDocument(NULL);
    Editor *editor = GetDocument(source);
    if(editor == NULL)
        return memFullErr;
    OSErr error = WorkspaceOpen(editor, source, file);
    if(error == noErr) {
        RememberDocument(editor);
    } else {
        if(newWindow && GetDocument(previous) != NULL) {
            CloseDocument(source);
            source = previous;
            editor = GetDocument(source);
        }
        if(error != userCanceledErr)
            WorkspaceOpenError(EditorWorkspace(editor), source, error);
    }
    return error;
}

static void ChooseDocument(WindowRef window)
{
    FSSpec file;
#if TARGET_API_MAC_CARBON
    NavDialogOptions options;
    NavReplyRecord reply = {0};
    OSErr error = NavGetDefaultDialogOptions(&options);
    if(error == noErr) {
        options.dialogOptionFlags &= ~kNavAllowMultipleFiles;
        error = NavGetFile(NULL, &reply, &options, NULL, NULL, NULL, NULL, NULL);
    }
    if(!reply.validRecord)
        return;
    AEKeyword keyword;
    DescType type;
    Size length;
    if(error == noErr)
        error = AEGetNthPtr(&reply.selection, 1, typeFSS, &keyword, &type,
                           &file, sizeof(file), &length);
    NavDisposeReply(&reply);
    if(error != noErr)
        return;
#else
    StandardFileReply reply;
    StandardGetFile(NULL, 0, NULL, &reply);
    if(!reply.sfGood)
        return;
    file = reply.sfFile;
#endif
    OpenDocument(GetDocument(window) != NULL ? window : NULL, &file);
}

static Rect QuickOpenBounds(WindowRef window)
{
    Rect view = EditorBounds(window), bounds;
    Rect outline = OutlineBounds(view);
    if(!EmptyRect(&outline))
        view.right = outline.left;
    short width = view.right - view.left - 2 * kPanelInset;
    if(width > kPanelWidth)
        width = kPanelWidth;
    bounds.left = view.left + (view.right - view.left - width) / 2;
    bounds.right = bounds.left + width;
    bounds.top = view.top + kPanelInset;
    bounds.bottom = bounds.top + kQuickFieldHeight +
                    (gQuickOpen.count == 0 ? kQuickRowHeight :
                                             gQuickOpen.count * kQuickRowHeight) + 6;
    return bounds;
}

enum { kDropFile = 1, kDropFolder = 2, kDropText = 4, kDropFileURL = 'furl' };

static Boolean HasDropFlavor(DragRef drag, DragItemRef item, FlavorType flavor)
{
    FlavorFlags flags;
    return GetFlavorFlags(drag, item, flavor, &flags) == noErr && !(flags & flavorSenderOnly);
}

static Handle ReadDropData(DragRef drag, DragItemRef item, FlavorType flavor, Size limit)
{
    Size size;
    if(GetFlavorDataSize(drag, item, flavor, &size) != noErr || size <= 0 || size > limit)
        return NULL;
    Handle data = NewHandle(size);
    if(data == NULL)
        return NULL;
    Size read = size;
    HLock(data);
    OSErr error = GetFlavorData(drag, item, flavor, *data, &read, 0);
    HUnlock(data);
    if(error == noErr && read == size)
        return data;
    DisposeHandle(data);
    return NULL;
}

static OSErr ReadDropFile(DragRef drag, DragItemRef item, FSSpec *file)
{
    HFSFlavor hfs;
    Size size = sizeof(hfs);
    if(GetFlavorData(drag, item, kDragFlavorTypeHFS, &hfs, &size, 0) == noErr &&
       size == sizeof(hfs) && hfs.fileSpec.name[0] < sizeof(hfs.fileSpec.name)) {
        *file = hfs.fileSpec;
        return noErr;
    }
#if TARGET_API_MAC_CARBON
    Handle data = ReadDropData(drag, item, kDropFileURL, kTextFileLimit);
    if(data != NULL) {
        HLock(data);
        CFURLRef url = CFURLCreateWithBytes(NULL, (const UInt8 *)*data, GetHandleSize(data),
                                           kCFStringEncodingUTF8, NULL);
        DisposeHandle(data);
        if(url != NULL) {
            FSRef ref;
            Boolean found = CFURLGetFSRef(url, &ref);
            CFRelease(url);
            if(found)
                return FSGetCatalogInfo(&ref, kFSCatInfoNone, NULL, NULL, file, NULL);
        }
    }
#endif
    return dragNotAcceptedErr;
}

static unsigned DropItemKind(DragRef drag, DragItemRef item, FSSpec *file, long *directory,
                             FlavorType *textFlavor)
{
    if(HasDropFlavor(drag, item, kDragFlavorTypeHFS) ||
       HasDropFlavor(drag, item, kDropFileURL)) {
        if(ReadDropFile(drag, item, file) == noErr &&
           WorkspaceLocateFolder(file, directory) == noErr)
            return *directory == 0 ? kDropFile : kDropFolder;
        return 0;
    }
    if(HasDropFlavor(drag, item, kDragFlavorTypePromiseHFS))
        return 0;
    const FlavorType textFlavors[] = {'utxt', 'utf8', 'TEXT'};
    for(unsigned i = 0; i < sizeof(textFlavors) / sizeof(textFlavors[0]); ++i) {
        if(HasDropFlavor(drag, item, textFlavors[i])) {
            *textFlavor = textFlavors[i];
            return kDropText;
        }
    }
    return 0;
}

static Rect DropTarget(WindowRef window, Point point, unsigned kinds)
{
    Rect canvas = WindowCanvas(window);
    if(!gFocusMode)
        canvas.bottom -= 23;
    if(gSidebarVisible && !gFocusMode) {
        Rect sidebar = canvas;
        sidebar.right = kSidebarWidth;
        if(PtInRect(point, &sidebar))
            return kinds & (kDropFile | kDropFolder) ? sidebar : (Rect){0};
        canvas.left = sidebar.right;
    }
    if((kinds & kDropFile) && PtInRect(point, &canvas))
        return canvas;
    if(!(kinds & kDropText))
        return (Rect){0};
    Rect view = EditorBounds(window);
    Rect outline = OutlineBounds(view);
    view.right = EmptyRect(&outline) ? view.right - kEditorScrollbarWidth : outline.left;
    Rect find = gFind.window == window ? FindBounds(window) : (Rect){0};
    Rect quick = gQuickOpen.window == window ? QuickOpenBounds(window) : (Rect){0};
    if(PtInRect(point, &view) && !PtInRect(point, &find) && !PtInRect(point, &quick))
        return view;
    return (Rect){0};
}

/* A null point inspects the items for tracking feedback without accepting them. */
static unsigned ProcessDrop(DragRef drag, WindowRef window, const Point *point)
{
    UInt16 count;
    if(CountDragItems(drag, &count) != noErr)
        return 0;
    unsigned accepted = 0;
    Editor *editor = GetDocument(window);
    for(unsigned i = 1; i <= count; ++i) {
        DragItemRef item;
        if(GetDragItemReferenceNumber(drag, i, &item) != noErr)
            continue;
        FSSpec file;
        long directory;
        FlavorType flavor;
        unsigned kind = DropItemKind(drag, item, &file, &directory, &flavor);
        if(kind == 0 || (kind == kDropText && count != 1))
            continue;
        if(point == NULL) {
            accepted |= kind;
            continue;
        }
        Rect target = DropTarget(window, *point, kind);
        if(EmptyRect(&target))
            continue;
        OSErr error;
        if(kind == kDropFile) {
            error = OpenDocument(NULL, &file);
        } else if(kind == kDropFolder) {
            error = WorkspaceAttachFolder(EditorWorkspace(editor), window, file.vRefNum, directory);
            if(error == noErr)
                SelectDocument(window);
            else
                WorkspaceOpenError(EditorWorkspace(editor), window, error);
        } else {
            Handle data = ReadDropData(drag, item, flavor, 2 * kTextFileLimit);
            if(data == NULL)
                continue;
            SetPortWindowPort(window);
            Boolean inserted = EditorDropText(editor, *point, data, flavor);
            DisposeHandle(data);
            if(inserted)
                SelectDocument(window);
            error = inserted ? noErr : dragNotAcceptedErr;
        }
        if(error == noErr)
            accepted |= kind;
    }
    return accepted;
}

static void ShowTextDropCaret(Rect caret)
{
    if(EqualRect(&gTextDrag.caret, &caret))
        return;
    if(!EmptyRect(&gTextDrag.caret))
        InvertRect(&gTextDrag.caret);
    if(!EmptyRect(&caret))
        InvertRect(&caret);
    gTextDrag.caret = caret;
#if TARGET_API_MAC_CARBON
    GrafPtr port;
    GetPort(&port);
    QDFlushPortBuffer((CGrafPtr)port, NULL);
#endif
}

static OSErr TrackTextDrag(WindowRef window, const EventRecord *event, RgnHandle region, DragRef drag)
{
    Handle text = EditorCopySelectedText(GetDocument(window));
    if(text == NULL)
        return memFullErr;
    HLock(text);
    OSErr error = AddDragItemFlavor(drag, 1, 'utxt', *text, GetHandleSize(text), 0);
    DisposeHandle(text);
    if(error != noErr)
        return error;
    Point origin = {0, 0};
    LocalToGlobal(&origin);
    OffsetRgn(region, origin.h, origin.v);
    gTextDrag.drag = drag;
    gTextDrag.source = window;
    error = TrackDrag(drag, event, region);
    SetPortWindowPort(window);
    ShowTextDropCaret((Rect){0});
    gTextDrag.drag = NULL;
    gTextDrag.source = NULL;
    return error;
}

static Boolean TrackSelectedText(WindowRef window, const EventRecord *event)
{
    if(event->modifiers & (shiftKey | cmdKey | controlKey))
        return false;
    Point point = event->where;
    GlobalToLocal(&point);
    Editor *editor = GetDocument(window);
    RgnHandle region = EditorTextDragRegion(editor, point);
    if(region == NULL)
        return false;
    Boolean tracking = WaitMouseMoved(event->where);
    if(tracking) {
        DragRef drag;
        if(NewDrag(&drag) == noErr) {
            TrackTextDrag(window, event, region, drag);
            DisposeDrag(drag);
        }
    }
    DisposeRgn(region);
    return tracking;
}

static OSErr TrackDrop(DragTrackingMessage message, WindowRef window, void *refcon, DragRef drag)
{
    (void)refcon;
    static unsigned kinds;
    static Rect highlight;
    if(message == kDragTrackingEnterHandler)
        highlight = (Rect){0};
    if(window == NULL || (message != kDragTrackingEnterWindow && message != kDragTrackingInWindow &&
                          message != kDragTrackingLeaveWindow))
        return noErr;
    GrafPtr port;
    GetPort(&port);
    SetPortWindowPort(window);
    if(gTextDrag.drag == drag && gTextDrag.source == window) {
        Point point;
        Rect caret = {0};
        if(message != kDragTrackingLeaveWindow && GetDragMouse(drag, &point, NULL) == noErr) {
            GlobalToLocal(&point);
            Rect target = DropTarget(window, point, kDropText);
            if(!EmptyRect(&target)) {
                SInt16 modifiers = 0;
                GetDragModifiers(drag, &modifiers, NULL, NULL);
                caret = EditorTextDropCaret(GetDocument(window), point, (modifiers & optionKey) != 0);
            }
        }
        ShowTextDropCaret(caret);
        SetPort(port);
        return noErr;
    }
    Rect target = {0};
    if(message != kDragTrackingLeaveWindow) {
        if(message == kDragTrackingEnterWindow)
            kinds = ProcessDrop(drag, window, NULL);
        Point point;
        if(GetDragMouse(drag, &point, NULL) == noErr) {
            GlobalToLocal(&point);
            target = DropTarget(window, point, kinds);
        }
    }
    if(!EqualRect(&target, &highlight)) {
        if(!EmptyRect(&highlight))
            HideDragHilite(drag);
        highlight = (Rect){0};
        if(!EmptyRect(&target)) {
            RgnHandle region = NewRgn();
            if(region != NULL) {
                RectRgn(region, &target);
                if(ShowDragHilite(drag, region, true) == noErr)
                    highlight = target;
                DisposeRgn(region);
            }
        }
    }
    SetPort(port);
    return noErr;
}

static OSErr ReceiveDrop(WindowRef window, void *refcon, DragRef drag)
{
    (void)refcon;
    Point point;
    if(GetDragMouse(drag, &point, NULL) != noErr)
        return dragNotAcceptedErr;
    GrafPtr port;
    GetPort(&port);
    SetPortWindowPort(window);
    GlobalToLocal(&point);
    Boolean accepted;
    if(gTextDrag.drag == drag && gTextDrag.source == window) {
        ShowTextDropCaret((Rect){0});
        Rect target = DropTarget(window, point, kDropText);
        SInt16 modifiers = 0;
        GetDragModifiers(drag, NULL, NULL, &modifiers);
        accepted = !EmptyRect(&target) &&
                   EditorDropSelection(GetDocument(window), point, (modifiers & optionKey) != 0);
    } else {
        accepted = ProcessDrop(drag, window, &point) != 0;
    }
    SetPort(port);
    return accepted != 0 ? noErr : dragNotAcceptedErr;
}

static void RefreshQuickOpen(void)
{
    Editor *editor = GetDocument(gQuickOpen.window);
    if(editor == NULL)
        return;
    gQuickOpen.count = gQuickOpen.query[0] == 0 ? 0 :
        WorkspaceSearch(EditorWorkspace(editor), gQuickOpen.query,
                        gQuickOpen.files, kQuickResultLimit);
    if(gQuickOpen.count == 0)
        gQuickOpen.selected = 0;
    else if(gQuickOpen.selected >= gQuickOpen.count)
        gQuickOpen.selected = gQuickOpen.count - 1;
    SetPortWindowPort(gQuickOpen.window);
    InvalidateWindow(gQuickOpen.window);
}

static void DismissQuickOpen(void)
{
    if(!gQuickOpen.visible)
        return;
    WindowRef window = gQuickOpen.window;
    gQuickOpen.visible = false;
    gQuickOpen.window = NULL;
    Editor *editor = GetDocument(window);
    if(editor != NULL) {
        SetPortWindowPort(window);
        EditorActivate(editor, window == FrontWindow());
        InvalidateWindow(window);
    }
}

static void ShowQuickOpen(WindowRef window)
{
    Editor *editor = GetDocument(window);
    if(editor == NULL)
        return;
    if(gQuickOpen.visible) {
        DismissQuickOpen();
        return;
    }
    memset(&gQuickOpen, 0, sizeof(gQuickOpen));
    gQuickOpen.visible = true;
    gQuickOpen.window = window;
    SetPortWindowPort(window);
    EditorActivate(editor, false);
    RefreshQuickOpen();
}

static void OpenQuickSelection(void)
{
    if(!gQuickOpen.visible || gQuickOpen.count == 0)
        return;
    WindowRef window = gQuickOpen.window;
    FSSpec file = gQuickOpen.files[gQuickOpen.selected];
    DismissQuickOpen();
    OpenDocument(window, &file);
}

static void ShowAboutBox(void)
{
#if TARGET_API_MAC_CARBON
    short button;
    Str255 title;
    title[0] = snprintf((char *)title + 1, sizeof(title) - 1,
                        "Linewren %s", LINEWREN_VERSION);
    StandardAlert(kAlertNoteAlert, title,
                  "\pA quiet place to write.\r\rMarkdown and plain text for PowerPC Mac OS X.",
                  NULL, &button);
#else
    WindowRef window = GetNewWindow(128, NULL, (WindowPtr)-1);
    Handle text;
    Rect bounds;
    EventRecord event;

    if(window == NULL)
        return;

    MoveWindow(window,
               MainScreenBounds().right / 2 - WindowCanvas(window).right / 2,
               MainScreenBounds().bottom / 2 - WindowCanvas(window).bottom / 2,
               false);
    ShowWindow(window);
    SetPortWindowPort(window);
    text = GetResource('TEXT', 128);
    if(text != NULL) {
        HLock(text);
        bounds = WindowCanvas(window);
        InsetRect(&bounds, 14, 14);
        TETextBox(*text, GetHandleSize(text), &bounds, teJustLeft);
        HUnlock(text);
    }

    while(!WaitNextEvent(mDownMask | keyDownMask, &event, 6, NULL))
        ;
    DisposeWindow(window);
#endif
}

static void UpdateMenus(void)
{
    MenuRef fileMenu = GetMenu(kFileMenu);
    MenuRef editMenu = GetMenu(kEditMenu);
    WindowRef window = FrontWindow();
    Editor *editor = GetDocument(window);
    Boolean hasEditor = editor != NULL;

    if(gSettings.recentCount > 0)
        EnableMenuItem(fileMenu, kOpenRecentItem);
    else
        DisableMenuItem(fileMenu, kOpenRecentItem);

    if(hasEditor && !IsWindowCollapsed(window)) {
        EnableMenuItem(gWindowMenu, kZoomItem);
#if TARGET_API_MAC_CARBON
        EnableMenuItem(gWindowMenu, kMinimizeItem);
#endif
    } else {
        DisableMenuItem(gWindowMenu, kZoomItem);
#if TARGET_API_MAC_CARBON
        DisableMenuItem(gWindowMenu, kMinimizeItem);
#endif
    }

    if(hasEditor)
        EnableMenuItem(fileMenu, kCloseItem);
    else
        DisableMenuItem(fileMenu, kCloseItem);

    if(hasEditor) {
        EnableMenuItem(fileMenu, kOpenFolderItem);
        EnableMenuItem(fileMenu, kSaveItem);
        EnableMenuItem(fileMenu, kSaveAsItem);
        EnableMenuItem(fileMenu, kQuickOpenItem);
    } else {
        DisableMenuItem(fileMenu, kOpenFolderItem);
        DisableMenuItem(fileMenu, kSaveItem);
        DisableMenuItem(fileMenu, kSaveAsItem);
        DisableMenuItem(fileMenu, kQuickOpenItem);
    }
    EnableMenuItem(fileMenu, kOpenItem);

    if(hasEditor) {
        EnableMenuItem(editMenu, kCutItem);
        EnableMenuItem(editMenu, kCopyItem);
        EnableMenuItem(editMenu, kPasteItem);
        EnableMenuItem(editMenu, kClearItem);
        EnableMenuItem(editMenu, kSelectAllItem);
    } else {
        DisableMenuItem(editMenu, kCutItem);
        DisableMenuItem(editMenu, kCopyItem);
        DisableMenuItem(editMenu, kPasteItem);
        DisableMenuItem(editMenu, kClearItem);
        DisableMenuItem(editMenu, kSelectAllItem);
    }
    if(hasEditor && EditorCanUndo(editor))
        EnableMenuItem(editMenu, kUndoItem);
    else
        DisableMenuItem(editMenu, kUndoItem);
    if(hasEditor && EditorCanRedo(editor))
        EnableMenuItem(editMenu, kRedoItem);
    else
        DisableMenuItem(editMenu, kRedoItem);
    static const short markdownItems[] = {
        kBoldItem, kItalicItem, kLinkItem, kHeadingItem, kQuoteItem, kListItem, kTaskItem
    };
    for(unsigned i = 0; i < sizeof(markdownItems) / sizeof(*markdownItems); ++i) {
        if(hasEditor && EditorIsMarkdown(editor))
            EnableMenuItem(editMenu, markdownItems[i]);
        else
            DisableMenuItem(editMenu, markdownItems[i]);
    }
    for(short item = kFindItem; item <= kReplaceItem; ++item) {
        if(hasEditor)
            EnableMenuItem(editMenu, item);
        else
            DisableMenuItem(editMenu, item);
    }
}

static void DoEditCommand(short item)
{
    WindowRef window = FrontWindow();
    Editor *editor;

    if(window == NULL)
        return;
    if(GetWindowKind(window) < 0) {
#if !TARGET_API_MAC_CARBON
        if(item != kRedoItem)
            SystemEdit(item - 1);
#endif
        return;
    }

    editor = GetDocument(window);
    if(editor == NULL)
        return;

    SetPortWindowPort(window);
    EditorCommand(editor, item);
    InvalidateFooter(window);
}

static void CloseFind(void)
{
    WindowRef window = gFind.window;
    if(window != NULL) {
        Rect bounds = FindBounds(window);
        gFind.window = NULL;
        SetPortWindowPort(window);
        InvalidateRect(&bounds);
    }
}

static void RunFind(WindowRef window, short direction)
{
    SetPortWindowPort(window);
    gFind.count = EditorSearch(GetDocument(window), gFindText, direction, &gFind.ordinal);
    Rect bounds = FindBounds(window);
    InvalidateRect(&bounds);
}

static void FocusFindField(Boolean replace)
{
    gFind.replaceField = replace;
    gFind.caret = replace ? gReplaceText[0] : gFindText[0];
    gFind.selectAll = gFind.caret != 0;
}

static void ShowFind(WindowRef window, Boolean replace)
{
    if(gFind.window != window)
        CloseFind();
    if(gFind.window == NULL)
        gFind.expanded = false;
    gFind.window = window;
    gFind.expanded |= replace;
    FocusFindField(replace);
    RunFind(window, 0);
}

static void ReplaceFound(Boolean all)
{
    if(gFind.count == 0)
        return;
    WindowRef window = gFind.window;
    SetPortWindowPort(window);
    EditorReplaceText(GetDocument(window), gFindText, gReplaceText, all);
    RunFind(window, 0);
}

static Rect FindBounds(WindowRef window)
{
    Rect view = EditorBounds(window);
    short right = view.right - kEditorScrollbarWidth - kPanelInset;
    short left = right - kPanelWidth;
    if(left < view.left - 12)
        left = view.left - 12;
    short top = view.top + kPanelInset;
    Rect bounds = {top, left, top + (gFind.expanded ? 2 * kFindRow : kFindRow), right};
    return bounds;
}

static Rect FindField(Rect bounds, Boolean replace)
{
    short top = bounds.top + (replace ? kFindRow : 0);
    Rect field = {top + 3, bounds.left + 20, top + kFindRow - 3, bounds.right - 127};
    return field;
}

static void DrawPanel(Rect bounds, const EditorTheme *theme)
{
    RGBBackColor(&theme->panel);
    EraseRect(&bounds);
    RGBForeColor(&theme->divider);
    FrameRect(&bounds);
    TextFont(0);
    TextSize(11);
    TextFace(normal);
    TextMode(srcCopy);
}

static void DrawPanelField(Rect field, ConstStr255Param text, ConstStr255Param hint,
                           unsigned caret, Boolean focused, Boolean selected,
                           const EditorTheme *theme)
{
    static const RGBColor focus = {0x6666, 0x8888, 0xbaba};
    RGBBackColor(&theme->page);
    EraseRect(&field);
    RGBForeColor(focused ? &focus : &theme->divider);
    FrameRect(&field);
    short width = field.right - field.left - 10;
    unsigned start = 0, end = text[0];
    caret = focused ? caret : 0;
    while(start < caret && TextWidth(text + 1, start, caret - start) > width)
        ++start;
    while(end > caret && TextWidth(text + 1, start, end - start) > width)
        --end;
    short x = field.left + 5, baseline = field.bottom - 5;
    RGBForeColor(text[0] ? &theme->text : &theme->secondary);
    MoveTo(x, baseline);
    if(text[0] == 0)
        DrawString(hint);
    else
        DrawText(text + 1, start, end - start);
    if(focused && selected) {
        Rect selection = {field.top + 2, x, field.bottom - 2,
                          x + TextWidth(text + 1, start, end - start)};
        InvertRect(&selection);
    } else if(focused) {
        x += text[0] == 0 ? -2 : TextWidth(text + 1, start, caret - start);
        Rect cursor = {field.top + 3, x, field.bottom - 3, x + 2};
        RGBForeColor(&theme->caret);
        PaintRect(&cursor);
    }
}

static void DrawFind(WindowRef window, const EditorTheme *theme)
{
    Rect bounds = FindBounds(window);
    DrawPanel(bounds, theme);
    DrawPanelField(FindField(bounds, false), gFindText, "\pFind",
                   gFind.caret, !gFind.replaceField, gFind.selectAll, theme);
    if(gFind.expanded)
        DrawPanelField(FindField(bounds, true), gReplaceText, "\pReplace",
                       gFind.caret, gFind.replaceField, gFind.selectAll, theme);
    RGBBackColor(&theme->panel);
    RGBForeColor(&theme->text);
    short baseline = bounds.top + 16;
    MoveTo(bounds.left + 6, baseline);
    DrawString(gFind.expanded ? "\pv" : "\p>");
    Str31 status = "\p";
    if(gFind.count)
        status[0] = snprintf((char *)status + 1, sizeof(status) - 1, "%u/%u",
                             gFind.ordinal, gFind.count);
    else if(gFindText[0])
        memcpy(status, "\pNo matches", 11);
    MoveTo(bounds.right - 121, baseline);
    DrawString(status);
    for(short i = 0; i < 2; ++i) {
        short x = bounds.right - 45 + i * 18;
        short tip = baseline - (i ? 1 : 9);
        short tail = baseline - (i ? 9 : 1);
        MoveTo(x, tail);
        LineTo(x, tip);
        MoveTo(x - 3, tip + (i ? -3 : 3));
        LineTo(x, tip);
        LineTo(x + 3, tip + (i ? -3 : 3));
    }
    MoveTo(bounds.right - 13, baseline - 8);
    LineTo(bounds.right - 6, baseline - 1);
    MoveTo(bounds.right - 6, baseline - 8);
    LineTo(bounds.right - 13, baseline - 1);
    if(gFind.expanded) {
        MoveTo(bounds.right - 118, baseline + kFindRow);
        DrawString("\pReplace");
        MoveTo(bounds.right - 59, baseline + kFindRow);
        DrawString("\pReplace All");
    }
    RGBBackColor(&theme->page);
    TextMode(srcOr);
}

static void ClickFind(Rect bounds, Point point)
{
    Boolean secondRow = point.v >= bounds.top + kFindRow;
    WindowRef window = gFind.window;
    if(point.h < bounds.left + 18) {
        gFind.expanded = !gFind.expanded;
        FocusFindField(gFind.expanded);
        InvalidateRect(&bounds);
        Rect updated = FindBounds(window);
        InvalidateRect(&updated);
    } else if(!secondRow && point.h >= bounds.right - 18) {
        CloseFind();
    } else if(!secondRow && point.h >= bounds.right - 54) {
        RunFind(window, point.h < bounds.right - 36 ? -1 : 1);
    } else if(secondRow && point.h >= bounds.right - 123) {
        ReplaceFound(point.h >= bounds.right - 64);
    } else {
        Rect field = FindField(bounds, secondRow);
        if(!PtInRect(point, &field))
            return;
        FocusFindField(secondRow);
        gFind.selectAll = false;
        InvalidateRect(&bounds);
    }
}

static void EditFindText(StringPtr text, const unsigned char *insert, unsigned length,
                         unsigned remove)
{
    unsigned start = gFind.caret;
    if(gFind.selectAll) {
        start = 0;
        remove = text[0];
    } else {
        start -= remove;
    }
    unsigned remaining = text[0] - start - remove;
    if(length > 255 - start - remaining)
        length = 255 - start - remaining;
    memmove(text + 1 + start + length, text + 1 + start + remove, remaining);
    if(length)
        memcpy(text + 1 + start, insert, length);
    text[0] = start + length + remaining;
    gFind.caret = start + length;
    gFind.selectAll = false;
    RunFind(gFind.window, 0);
}

static Boolean FindKey(const EventRecord *event)
{
    WindowRef window = gFind.window;
    if(window == NULL || window != FrontWindow())
        return false;
    SetPortWindowPort(window);
    unsigned char key = event->message & charCodeMask;
    StringPtr text = gFind.replaceField ? gReplaceText : gFindText;
    if(event->modifiers & cmdKey) {
        if(key == 'a' || key == 'A') {
            FocusFindField(gFind.replaceField);
        } else if(key == 'v' || key == 'V') {
            if(ReadScrap(NULL, 'TEXT') > 0) {
                Handle scrap = NewHandle(0);
                if(scrap != NULL) {
                    long size = ReadScrap(scrap, 'TEXT');
                    HLock(scrap);
                    const unsigned char *bytes = (const unsigned char *)*scrap;
                    unsigned length = 0;
                    while((long)length < size && length < 255 && bytes[length] >= ' ')
                        ++length;
                    if(length)
                        EditFindText(text, bytes, length, 0);
                    DisposeHandle(scrap);
                }
            }
        } else if(key == 'g' || key == 'G') {
            RunFind(window, (event->modifiers & shiftKey) ? -1 : 1);
        } else {
            return false;
        }
    } else if(key == 0x1b) {
        CloseFind();
        return true;
    } else if(key == '\t' && gFind.expanded) {
        FocusFindField(!gFind.replaceField);
    } else if(key == '\r' || key == 3) {
        if(gFind.replaceField && !(event->modifiers & shiftKey))
            ReplaceFound(false);
        else
            RunFind(window, (event->modifiers & shiftKey) ? -1 : 1);
    } else if(key == 0x1e || key == 0x1f) {
        RunFind(window, key == 0x1e ? -1 : 1);
    } else if(key == 0x1c || key == 0x1d) {
        if(gFind.selectAll)
            gFind.caret = key == 0x1c ? 0 : text[0];
        else if(key == 0x1c && gFind.caret > 0)
            --gFind.caret;
        else if(key == 0x1d && gFind.caret < text[0])
            ++gFind.caret;
        gFind.selectAll = false;
    } else if(key == 8) {
        EditFindText(text, NULL, 0, gFind.caret != 0);
    } else if(key >= ' ' && key != 127) {
        EditFindText(text, &key, 1, 0);
    }
    Rect bounds = FindBounds(window);
    InvalidateRect(&bounds);
    return true;
}

static void DoTypographyCommand(short menu, short item)
{
    EditorTypography typography = gTypography;
    short fontItem = gFontItem;
    if(menu == kFontMenu) {
        if(item < 1 || item > CountMenuItems(gFontMenu) || item == gFontItem)
            return;
        Str255 name, resolved;
        GetMenuItemText(gFontMenu, item, name);
        GetFNum(name, &typography.family);
        if(typography.family == 0) {
            GetFontName(0, resolved);
            if(!EqualString(name, resolved, false, true)) {
                SysBeep(1);
                return;
            }
        }
        if(!EditorFontAvailable(typography.family)) {
            SysBeep(1);
            return;
        }
        fontItem = item;
    } else {
        short size = SizeItem(gTypography.points);
        if(item == kLargerItem)
            item = size + 1;
        else if(item == kSmallerItem)
            item = size - 1;
        if(item < kFirstSizeItem || item > CountMenuItems(gSizeMenu) || item == size)
            return;
        typography.points = kSizes[item - kFirstSizeItem];
    }

    GrafPtr port;
    GetPort(&port);
    for(WindowRef window = FrontWindow(); window != NULL; window = GetNextWindow(window)) {
        Editor *editor = GetDocument(window);
        if(editor != NULL) {
            SetPortWindowPort(window);
            EditorSetTypography(editor, typography);
        }
    }
    SetPort(port);
    gFontItem = fontItem;
    gTypography = typography;
    UpdateTypographyMenus();
}

static void DoMenuCommand(long command)
{
    short menu = (unsigned long)command >> 16;
    short item = command & 0xffff;
    WindowRef window;

    if(gQuickOpen.visible && !(menu == kFileMenu && item == kQuickOpenItem))
        DismissQuickOpen();
    if(command != 0 && !(menu == kEditMenu && item >= kFindItem && item <= kReplaceItem))
        CloseFind();

    if(menu == kAppleMenu) {
        if(item == kAboutItem)
            ShowAboutBox();
#if !TARGET_API_MAC_CARBON
        else {
            Str255 name;
            GetMenuItemText(GetMenu(kAppleMenu), item, name);
            OpenDeskAcc(name);
        }
#endif
    } else if(menu == kFileMenu) {
        switch(item) {
            case kNewItem: NewDocument(NULL); break;
            case kCloseItem:
                window = FrontWindow();
                if(window != NULL) {
#if !TARGET_API_MAC_CARBON
                    if(GetWindowKind(window) < 0)
                        CloseDeskAcc(GetWindowKind(window));
                    else
#endif
                    if(CanClose(window))
                        CloseDocument(window);
                }
                break;
            case kQuitItem:
                gRunning = false;
                for(window = FrontWindow(); window != NULL;) {
                    WindowRef next = GetNextWindow(window);
                    if(!CanClose(window)) {
                        gRunning = true;
                        break;
                    }
                    window = next;
                }
                break;
            case kOpenFolderItem:
            case kOpenItem:
            case kQuickOpenItem:
            case kSaveItem:
            case kSaveAsItem: {
                window = FrontWindow();
                Editor *editor = GetDocument(window);
                if(item == kOpenItem) {
                    ChooseDocument(window);
                } else if(editor != NULL) {
                    if(item == kQuickOpenItem)
                        ShowQuickOpen(window);
                    else if(item == kOpenFolderItem)
                        WorkspaceChooseFolder(EditorWorkspace(editor), window);
                    else if(WorkspaceSave(editor, window, item == kSaveAsItem))
                        RememberDocument(editor);
                }
                break;
            }
        }
    } else if(menu == kEditMenu) {
        WindowRef front = FrontWindow();
        Editor *editor = GetDocument(front);
        if(editor != NULL && (item == kFindItem || item == kReplaceItem)) {
            if(item == kFindItem && gFind.window != front)
                EditorFindSelection(editor, gFindText);
            ShowFind(front, item == kReplaceItem);
        } else if(editor != NULL && (item == kFindNextItem || item == kFindPreviousItem))
            RunFind(front, item == kFindPreviousItem ? -1 : 1);
        else
            DoEditCommand(item);
        if(editor != NULL) {
            SetPortWindowPort(front);
            InvalidateFooter(front);
        }
    } else if(menu == kRecentMenu) {
        if(item >= 1 && (unsigned)item <= gSettings.recentCount) {
            FSSpec file = gSettings.recent[item - 1];
            OSErr error = OpenDocument(NULL, &file);
            if(error == fnfErr || error == dirNFErr || error == nsvErr) {
                --gSettings.recentCount;
                memmove(gSettings.recent + item - 1, gSettings.recent + item,
                        (gSettings.recentCount - (item - 1)) * sizeof(file));
            }
        } else if((unsigned)item == gSettings.recentCount + 2) {
            gSettings.recentCount = 0;
        }
    } else if(menu == kWindowMenu) {
        window = FrontWindow();
        if(item == kZoomItem && GetDocument(window) != NULL && !IsWindowCollapsed(window)) {
#if TARGET_API_MAC_CARBON
            ZoomDocument(window, inZoomOut);
#else
            SetPortWindowPort(window);
            Rect current = WindowCanvas(window), standard = {0};
            Point origin = {0, 0};
            LocalToGlobal(&origin);
            OffsetRect(&current, origin.h, origin.v);
            GetWindowStandardState(window, &standard);
            ZoomDocument(window, EqualRect(&current, &standard) ? inZoomIn : inZoomOut);
#endif
#if TARGET_API_MAC_CARBON
        } else if(item == kMinimizeItem && GetDocument(window) != NULL) {
            CollapseWindow(window, true);
#endif
        } else if(item >= kFirstDocumentItem) {
            short remaining = item - kFirstDocumentItem;
            for(window = FirstWindow(); window != NULL; window = GetNextWindow(window)) {
                if(GetDocument(window) != NULL && remaining-- == 0) {
                    SelectDocument(window);
                    break;
                }
            }
        }
    } else if(menu == kFontMenu || menu == kSizeMenu) {
        DoTypographyCommand(menu, item);
    } else if(menu == kViewMenu && (item == kSidebarItem || item == kFocusItem ||
                                   item == kOutlineItem)) {
        if(item == kSidebarItem)
            gSidebarVisible = !gSidebarVisible;
        else if(item == kFocusItem)
            gFocusMode = !gFocusMode;
        else
            gSettings.outlineVisible = !gSettings.outlineVisible;
        ApplyViewLayout();
    } else if(menu == kViewMenu && item == kTypewriterItem) {
        gSettings.typewriter = !gSettings.typewriter;
        GrafPtr port;
        GetPort(&port);
        for(window = FrontWindow(); window != NULL; window = GetNextWindow(window)) {
            Editor *editor = GetDocument(window);
            if(editor != NULL) {
                SetPortWindowPort(window);
                EditorSetTypewriter(editor, gSettings.typewriter);
            }
        }
        SetPort(port);
        UpdateThemeMenu();
    } else if(menu == kViewMenu && item >= kFirstCounterItem &&
              item <= kFirstCounterItem + kCounterHidden) {
        gSettings.counter = item - kFirstCounterItem;
        GrafPtr port;
        GetPort(&port);
        for(window = FrontWindow(); window != NULL; window = GetNextWindow(window)) {
            if(GetDocument(window) != NULL) {
                SetPortWindowPort(window);
                InvalidateFooter(window);
            }
        }
        SetPort(port);
        UpdateThemeMenu();
    } else if(menu == kViewMenu && (item == kLightTheme || item == kDarkTheme) && item != gTheme) {
        GrafPtr port;
        GetPort(&port);
        gTheme = item;
        UpdateThemeMenu();
        for(window = FrontWindow(); window != NULL; window = GetNextWindow(window)) {
            Editor *editor = GetDocument(window);
            if(editor != NULL) {
                SetPortWindowPort(window);
                EditorSetTheme(editor, &kThemes[gTheme - 1]);
                InvalidateWindow(window);
            }
        }
        SetPort(port);
    }
    HiliteMenu(0);
}

static void FooterCounter(Editor *editor, Str255 label)
{
    label[0] = 0;
    if(gFocusMode || gSettings.counter == kCounterHidden)
        return;
    Boolean characters = gSettings.counter == kCounterCharacters;
    size_t count = characters ? EditorCharacterCount(editor) : EditorWordCount(editor);
    label[0] = snprintf((char *)label + 1, 255, "%lu %s%s", (unsigned long)count,
                        characters ? "character" : "word", count == 1 ? "" : "s");
}

static unsigned OutlineRows(Rect bounds)
{
    short height = bounds.bottom - bounds.top - kOutlineFirstRow - kOutlinePager;
    return height > 0 ? height / kOutlineRowHeight : 0;
}

static void DrawOutline(Editor *editor, Rect bounds, const EditorTheme *theme)
{
    RGBBackColor(&theme->page);
    EraseRect(&bounds);

    TextFont(WorkspaceLabelFont());
    TextSize(10);
    TextFace(normal);
    TextMode(srcCopy);

    unsigned rows = OutlineRows(bounds);
    unsigned count = EditorOutlineCount(editor);
    unsigned first = EditorOutlinePage(editor, rows, 0);
    unsigned current = EditorOutlineCurrent(editor);
    if(count == 0) {
        RGBForeColor(&theme->secondary);
        MoveTo(bounds.left + 10, bounds.top + kOutlineFirstRow + 11);
        DrawString("\pNo headings");
    }
    for(unsigned row = 0; row < rows && first + row < count; ++row) {
        Str255 label;
        unsigned level = 1;
        if(!EditorOutlineLabel(editor, first + row, label, &level))
            continue;
        short top = bounds.top + kOutlineFirstRow + row * kOutlineRowHeight;
        short x = bounds.left + 10 + (level - 1) * 5;
        Boolean active = first + row == current;
        RGBForeColor(active ? &theme->text : &theme->secondary);
        if(active) {
            Rect tick = {top + 4, bounds.left + 4, top + 12, bounds.left + 6};
            PaintRect(&tick);
        }
        TruncString(bounds.right - x - 8, label, smTruncEnd);
        MoveTo(x, top + 11);
        DrawString(label);
    }
    RGBForeColor(&theme->secondary);
    if(first != 0) {
        MoveTo(bounds.left + 9, bounds.bottom - 8);
        DrawString("\pPrevious");
    }
    if(first + rows < count) {
        MoveTo(bounds.right - 33, bounds.bottom - 8);
        DrawString("\pNext");
    }
    TextMode(srcOr);
    RGBBackColor(&theme->page);
    RGBForeColor(&theme->text);
}

static void ClickOutline(Editor *editor, Rect bounds, Point point)
{
    unsigned rows = OutlineRows(bounds);
    if(point.v >= bounds.bottom - kOutlinePager) {
        EditorOutlinePage(editor, rows, point.h < (bounds.left + bounds.right) / 2 ? -1 : 1);
        InvalidateRect(&bounds);
    } else if(point.v >= bounds.top + kOutlineFirstRow) {
        unsigned row = (point.v - bounds.top - kOutlineFirstRow) / kOutlineRowHeight;
        if(row < rows)
            EditorJumpToHeading(editor, EditorOutlinePage(editor, rows, 0) + row);
    }
}

static void DrawDocument(WindowRef window)
{
    Editor *editor = GetDocument(window);
    SetPortWindowPort(window);
    BeginUpdate(window);
    Rect canvas = WindowCanvas(window);
    const EditorTheme *theme = &kThemes[gTheme - 1];
    if(editor != NULL)
        RGBBackColor(&theme->page);
    if(editor != NULL) {
        Rect view = EditorBounds(window);
        short contentLeft = gSidebarVisible && !gFocusMode ? kSidebarWidth + 1 : 0;
        short contentBottom = canvas.bottom - (gFocusMode ? 0 : 23);
        Rect top = {0, contentLeft, view.top, canvas.right};
        Rect left = {view.top, contentLeft, view.bottom, view.left};
        Rect right = {view.top, view.right, view.bottom, canvas.right};
        Rect bottom = {view.bottom, contentLeft, contentBottom, canvas.right};
        EraseRect(&top);
        EraseRect(&left);
        EraseRect(&right);
        EraseRect(&bottom);
    } else
        EraseRect(&canvas);
    if(editor != NULL)
        EditorDraw(editor);
    if(editor != NULL) {
        Str255 counter;
        FooterCounter(editor, counter);
        WorkspaceDraw(EditorWorkspace(editor), window, theme, counter,
                      gSidebarVisible && !gFocusMode, !gFocusMode);
        Rect outline = OutlineBounds(EditorBounds(window));
        if(!EmptyRect(&outline) && EditorOutlineReady(editor))
            DrawOutline(editor, outline, theme);
        RGBBackColor(&theme->page);
        RGBForeColor(&theme->divider);
    }
    if(editor != NULL) {
        short right = canvas.right, bottom = canvas.bottom;
#if TARGET_API_MAC_CARBON
        Pattern gray;
        PenPat(GetQDGlobalsGray(&gray));
#else
        PenPat(&qd.gray);
#endif
        for(short offset = 5; offset <= 13; offset += 4) {
            MoveTo(right - offset, bottom - 2);
            LineTo(right - 2, bottom - offset);
        }
        PenNormal();
    }
    if(editor != NULL)
        RGBForeColor(&theme->text);
    if(editor != NULL && gQuickOpen.visible && gQuickOpen.window == window) {
        Rect bounds = QuickOpenBounds(window);
        DrawPanel(bounds, theme);
        Rect field = {bounds.top + 4, bounds.left + kPanelInset,
                      bounds.top + kQuickFieldHeight - 4, bounds.right - kPanelInset};
        DrawPanelField(field, gQuickOpen.query, "\pQuick Open",
                       gQuickOpen.query[0], true, false, theme);
        for(unsigned i = 0; i < (gQuickOpen.count == 0 ? 1 : gQuickOpen.count); ++i) {
            Rect row = {bounds.top + kQuickFieldHeight + i * kQuickRowHeight,
                        bounds.left + 1,
                        bounds.top + kQuickFieldHeight + (i + 1) * kQuickRowHeight,
                        bounds.right - 1};
            if(gQuickOpen.count != 0 && i == gQuickOpen.selected) {
                RGBForeColor(&theme->text);
                PaintRect(&row);
                RGBBackColor(&theme->text);
                RGBForeColor(&theme->page);
            } else {
                RGBBackColor(&theme->panel);
                RGBForeColor(&theme->text);
            }
            static const Str63 prompt = "\pType a file name";
            static const Str63 noMatches = "\pNo matching files";
            ConstStr255Param name = gQuickOpen.count != 0 ? gQuickOpen.files[i].name :
                gQuickOpen.query[0] == 0 ? prompt : noMatches;
            Str255 label;
            memcpy(label, name, name[0] + 1);
            TruncString(bounds.right - bounds.left - 18, label, smTruncMiddle);
            MoveTo(bounds.left + 9, row.top + 13);
            DrawString(label);
        }
        RGBBackColor(&theme->page);
        RGBForeColor(&theme->text);
        TextMode(srcOr);
    }
    if(editor != NULL && gFind.window == window)
        DrawFind(window, theme);
    EndUpdate(window);
#if TARGET_API_MAC_CARBON
    QDFlushPortBuffer(GetWindowPort(window), NULL);
#endif
}

static void RelayoutDocument(WindowRef window)
{
    SetPortWindowPort(window);
    LayoutEditor(window);
    WorkspaceRefreshSidebar(EditorWorkspace(GetDocument(window)), window);
    InvalidateWindow(window);
}

static void SizeDocument(WindowRef window, short width, short height)
{
    SizeWindow(window, width, height, true);
    RelayoutDocument(window);
}

static OSStatus ZoomDocument(WindowRef window, short part)
{
#if TARGET_API_MAC_CARBON
    (void)part;
    Rect current, target;
    OSStatus status = GetWindowBounds(window, kWindowContentRgn, &current);
    if(status != noErr)
        return status;
    Boolean restore = GetWindowProperty(window, 'Lnwr', 'zoom', sizeof(target),
                                        NULL, &target) == noErr;
    if(!restore) {
        Rect screen = MainScreenBounds();
        GetWindowGreatestAreaDevice(window, kWindowStructureRgn, NULL, &screen);
        Point ideal = {screen.bottom - screen.top, screen.right - screen.left};
        IsWindowInStandardState(window, &ideal, &target);
        status = SetWindowProperty(window, 'Lnwr', 'zoom', sizeof(current), &current);
        if(status != noErr)
            return status;
    }
    status = SetWindowBounds(window, kWindowContentRgn, &target);
    if((restore && status == noErr) || (!restore && status != noErr))
        RemoveWindowProperty(window, 'Lnwr', 'zoom');
    if(status != noErr)
        return status;
#else
    ZoomWindow(window, part, true);
#endif
    RelayoutDocument(window);
    DrawDocument(window);
    return noErr;
}

static void ActivateDocument(WindowRef window, Boolean active)
{
    Editor *editor = GetDocument(window);
    if(editor == NULL)
        return;
    SetPortWindowPort(window);
    EditorActivate(editor, active && !(gQuickOpen.visible && gQuickOpen.window == window));
    if(active)
        WorkspaceCheckExternal(EditorWorkspace(editor), window, true);
}

static void HandleMouseDown(EventRecord *event)
{
    WindowRef window;
    short part = FindWindow(event->where, &window);
    Editor *editor;

    switch(part) {
        case inMenuBar:
            RefreshDocumentMenus();
            UpdateMenus();
            DoMenuCommand(MenuSelect(event->where));
            break;
        case inGoAway:
            if(TrackGoAway(window, event->where) && CanClose(window))
                CloseDocument(window);
            break;
        case inZoomIn:
        case inZoomOut:
            if(GetDocument(window) != NULL && TrackBox(window, event->where, part))
                ZoomDocument(window, part);
            break;
        case inDrag: {
            Rect screen = MainScreenBounds();
            DragWindow(window, event->where, &screen);
            break;
        }
        case inGrow:
            if(GetDocument(window) != NULL) {
                if(window != FrontWindow())
                    SelectWindow(window);
                Rect screen = MainScreenBounds();
                Rect limits = {330, 400, screen.bottom - screen.top - GetMBarHeight(),
                               screen.right - screen.left};
                unsigned long size = GrowWindow(window, event->where, &limits);
                if(size != 0)
                    SizeDocument(window, (short)(size & 0xffff), (short)(size >> 16));
            }
            break;
        case inContent:
            if(window != FrontWindow()) {
                CloseFind();
                if(gQuickOpen.visible)
                    DismissQuickOpen();
                SelectWindow(window);
            } else {
                editor = GetDocument(window);
                if(editor != NULL) {
                    SetPortWindowPort(window);
                    Point point = event->where;
                    FSSpec file;
                    GlobalToLocal(&point);
                    if(gFind.window == window) {
                        Rect bounds = FindBounds(window);
                        if(PtInRect(point, &bounds))
                            ClickFind(bounds, point);
                        else
                            CloseFind();
                        break;
                    }
                    if(gQuickOpen.visible) {
                        Rect bounds = QuickOpenBounds(window);
                        if(gQuickOpen.window == window && PtInRect(point, &bounds)) {
                            if(point.v >= bounds.top + kQuickFieldHeight &&
                               gQuickOpen.count != 0) {
                                unsigned row = (point.v - bounds.top - kQuickFieldHeight) /
                                               kQuickRowHeight;
                                if(row < gQuickOpen.count) {
                                    gQuickOpen.selected = row;
                                    OpenQuickSelection();
                                }
                            }
                        } else {
                            DismissQuickOpen();
                        }
                        break;
                    }
                    Boolean contextual = (event->modifiers & controlKey) != 0;
#if TARGET_API_MAC_CARBON
                    contextual |= IsShowContextualMenuClick(event);
#endif
                    if(gSidebarVisible && !gFocusMode &&
                       WorkspaceClick(editor, window, point, contextual, &file)) {
                        if(file.name[0] != 0)
                            OpenDocument(window, &file);
                        break;
                    }
                    Rect canvas = WindowCanvas(window);
                    if(!gFocusMode && point.v >= canvas.bottom - 23) {
                        if(!contextual && gSettings.counter != kCounterHidden &&
                           point.h >= canvas.right - 149 && point.h < canvas.right - 18) {
                            short next = gSettings.counter == kCounterWords
                                ? kCounterCharacters : kCounterWords;
                            DoMenuCommand(((long)kViewMenu << 16) | (kFirstCounterItem + next));
                        }
                        break;
                    }
                    Rect outline = OutlineBounds(EditorBounds(window));
                    if(PtInRect(point, &outline)) {
                        ClickOutline(editor, outline, point);
                        break;
                    }
                    if(contextual) {
                        if(EditorContextMenu(editor, point, gFindText))
                            ShowFind(window, false);
                        InvalidateFooter(window);
                    } else if(!TrackSelectedText(window, event)) {
                        EditorClick(editor, event);
                    }
                }
            }
            break;
#if !TARGET_API_MAC_CARBON
        case inSysWindow:
            SystemClick(event, window);
            break;
#endif
    }
}

static Boolean gForeground = true;
#if !TARGET_API_MAC_CARBON
enum { kMaxWheelDevices = 8 };
static ISpElementReference gWheels[kMaxWheelDevices];
static UInt32 gWheelCount;
static Boolean gWheelStarted;

static void StartWheelInput(void)
{
    gWheelStarted = ISpStartup() == noErr;
    if(!gWheelStarted)
        return;

    ISpDeviceReference mice[kMaxWheelDevices];
    UInt32 count;
    if(ISpDevices_ExtractByClass(kISpDeviceClass_Mouse, kMaxWheelDevices, &count, mice) != noErr)
        return;

    for(UInt32 i = 0; i < count; ++i) {
        ISpElementListReference elements;
        ISpElementReference wheel;
        UInt32 found;
        if(ISpDevice_GetElementList(mice[i], &elements) == noErr &&
           ISpElementList_ExtractByLabel(elements, kISpElementLabel_Delta_Z, 1, &found, &wheel) == noErr &&
           found != 0 && ISpDevices_Activate(1, &mice[i]) == noErr)
            gWheels[gWheelCount++] = wheel;
    }
}
#endif

static void ScrollWheelAt(WindowRef window, Point mouse, short steps)
{
    Editor *editor = GetDocument(window);
    if(editor == NULL)
        return;
    Rect view = EditorBounds(window);
    Rect outline = OutlineBounds(view);
    if(steps != 0 && PtInRect(mouse, &outline)) {
        EditorOutlinePage(editor, OutlineRows(outline), -steps);
        InvalidateRect(&outline);
        return;
    }
    /* Wheel-down is negative. Document rows grow down. */
    if(PtInRect(mouse, &view))
        EditorScrollLines(editor, -steps * 3);
}

#if TARGET_API_MAC_CARBON
static pascal OSStatus CarbonWheel(EventHandlerCallRef next, EventRef event, void *data)
{
    (void)next;
    (void)data;
    EventMouseWheelAxis axis;
    SInt32 delta;
    WindowRef window = FrontWindow();
    if(!gForeground || GetDocument(window) == NULL ||
       GetEventParameter(event, kEventParamMouseWheelAxis, typeMouseWheelAxis,
                         NULL, sizeof(axis), NULL, &axis) != noErr ||
       axis != kEventMouseWheelAxisY ||
       GetEventParameter(event, kEventParamMouseWheelDelta, typeLongInteger,
                         NULL, sizeof(delta), NULL, &delta) != noErr)
        return eventNotHandledErr;
    SetPortWindowPort(window);
    Point mouse;
    GetMouse(&mouse);
    ScrollWheelAt(window, mouse, delta < -4 ? -4 : delta > 4 ? 4 : delta);
    return noErr;
}

static void StartWheelInput(void)
{
    EventTypeSpec wheel = {kEventClassMouse, kEventMouseWheelMoved};
    InstallApplicationEventHandler(NewEventHandlerUPP(CarbonWheel), 1, &wheel, NULL, NULL);
}
#else
static void PollWheelInput(void)
{
    if(gWheelCount == 0)
        return;

    ISpTickle();
    short steps = 0;
    for(UInt32 i = 0; i < gWheelCount; ++i) {
        ISpElementEvent event;
        Boolean pending;
        while(ISpElement_GetNextEvent(gWheels[i], sizeof(event), &event, &pending) == noErr && pending) {
            SInt32 delta = (SInt32)event.data;
            if(delta > 0 && steps < 4)
                ++steps;
            else if(delta < 0 && steps > -4)
                --steps;
        }
    }
    if(steps == 0 || !gForeground)
        return;

    WindowRef window = FrontWindow();
    if(window == NULL)
        return;

    SetPortWindowPort(window);
    Point mouse;
    GetMouse(&mouse);
    ScrollWheelAt(window, mouse, steps);
}
#endif

static long CommandMenuSelection(const EventRecord *event)
{
    unsigned char key = event->message & charCodeMask;
    if((event->modifiers & (shiftKey | optionKey | controlKey)) == shiftKey &&
       GetDocument(FrontWindow()) != NULL) {
        switch(key) {
            case 'S': case 's': return ((long)kFileMenu << 16) | kSaveAsItem;
            case 'G': case 'g': return ((long)kEditMenu << 16) | kFindPreviousItem;
            case 'Z': case 'z': return ((long)kEditMenu << 16) | kRedoItem;
        }
    }
    return MenuKey(key);
}

static void HandleKeyDown(EventRecord *event)
{
    if(FindKey(event))
        return;
    WindowRef window;
    Editor *editor;
    char key = event->message & charCodeMask;

    window = FrontWindow();
    if(gQuickOpen.visible && gQuickOpen.window == window &&
       (event->modifiers & cmdKey) == 0) {
        SetPortWindowPort(window);
        unsigned char input = event->message & charCodeMask;
        if(input == 0x1b) {
            DismissQuickOpen();
        } else if(input == '\r' || input == 3) {
            OpenQuickSelection();
        } else if(input == 0x1e && gQuickOpen.selected > 0) {
            --gQuickOpen.selected;
            InvalidateWindow(window);
        } else if(input == 0x1f && gQuickOpen.selected + 1 < gQuickOpen.count) {
            ++gQuickOpen.selected;
            InvalidateWindow(window);
        } else if(input == 8 && gQuickOpen.query[0] != 0) {
            --gQuickOpen.query[0];
            gQuickOpen.selected = 0;
            RefreshQuickOpen();
        } else if(input >= 32 && input != 127 && gQuickOpen.query[0] < 63) {
            gQuickOpen.query[++gQuickOpen.query[0]] = input;
            gQuickOpen.selected = 0;
            RefreshQuickOpen();
        }
        return;
    }

    Boolean command = (event->modifiers & cmdKey) != 0;
    if(command) {
        Boolean navigation = key >= 0x1c && key <= 0x1f &&
                             (event->modifiers & (optionKey | controlKey)) == 0;
        if(navigation) {
            if(gQuickOpen.visible)
                DismissQuickOpen();
            window = FrontWindow();
            editor = GetDocument(window);
            if(editor != NULL) {
                SetPortWindowPort(window);
                EditorKey(editor, event);
            }
        } else {
            UpdateMenus();
            DoMenuCommand(CommandMenuSelection(event));
        }
        return;
    }

    editor = GetDocument(window);
    if(editor != NULL) {
        SetPortWindowPort(window);
        EditorKey(editor, event);
        InvalidateFooter(window);
    }
}

static void DispatchEvent(EventRecord *event)
{
    enum { kSuspendResumeMessage = 1, kResumeFlag = 1 };
    WindowRef window;

    switch(event->what) {
        case mouseDown: HandleMouseDown(event); break;
        case keyDown:
        case autoKey: HandleKeyDown(event); break;
        case updateEvt: DrawDocument((WindowRef)event->message); break;
        case activateEvt:
            window = (WindowRef)event->message;
            ActivateDocument(window, (event->modifiers & activeFlag) != 0);
            break;
        case osEvt:
            if((event->message >> 24) == kSuspendResumeMessage) {
                Boolean foreground = (event->message & kResumeFlag) != 0;
                gForeground = foreground;
                ActivateDocument(FrontWindow(), foreground);
            }
            break;
        case kHighLevelEvent: AEProcessAppleEvent(event); break;
    }
}

static pascal OSErr OpenDocuments(const AppleEvent *event, AppleEvent *reply, long refcon)
{
    CloseFind();
    (void)reply;
    (void)refcon;
    AEDescList documents;
    OSErr error = AEGetParamDesc(event, keyDirectObject, typeAEList, &documents);
    if(error != noErr)
        return error;
    long count = 0;
    error = AECountItems(&documents, &count);
    for(long i = 1; error == noErr && i <= count; ++i) {
        FSSpec file;
        AEKeyword keyword;
        DescType type;
        Size size;
        error = AEGetNthPtr(&documents, i, typeFSS, &keyword, &type,
                            &file, sizeof(file), &size);
        if(error == noErr) {
            WindowRef window = i == 1 ? FrontWindow() : NULL;
            Editor *editor = GetDocument(window);
            if(editor == NULL || WorkspaceIsRecovered(EditorWorkspace(editor)))
                window = NULL;
            OSErr openError = OpenDocument(window, &file);
            if(openError != userCanceledErr)
                error = openError;
        }
    }
    AEDisposeDesc(&documents);
    return error;
}

#if TARGET_API_MAC_CARBON
static pascal OSErr QuitApplication(const AppleEvent *event, AppleEvent *reply, long refcon)
{
    (void)event;
    (void)reply;
    (void)refcon;
    DoMenuCommand(((long)kFileMenu << 16) | kQuitItem);
    return gRunning ? userCanceledErr : noErr;
}
#endif

#ifdef LINEWREN_SMOKE_TEST

static char gATSUITiming[256];

static const char *SmokeEditorInput(WindowRef window, Editor *editor)
{
    static const UniChar lines[] = {'a', 'b', 'c', '\r', 'd', 'e', 'f', '\r', 'g', 'h', 'i', '\r'};
    enum { kRepeats = 12 };
    Handle source = NewHandle(sizeof(lines) * kRepeats);
    if(source == NULL)
        return "FAIL: allocate the editor input fixture";
    for(unsigned i = 0; i < kRepeats; ++i)
        memcpy(*source + i * sizeof(lines), lines, sizeof(lines));
    if(EditorTakeText(editor, source) != noErr) {
        DisposeHandle(source);
        return "FAIL: load the editor input fixture";
    }

    EventRecord key = {.what = keyDown, .message = 0x1f};
    DispatchEvent(&key);
    key.message = 0x1d;
    DispatchEvent(&key);
    if(!EditorSelectionAt(editor, 5, 5))
        return "FAIL: place the caret in the middle of the second line";

    key.modifiers = cmdKey | shiftKey;
    key.message = (0x7b << 8) | 0x1c;
    DispatchEvent(&key);
    if(!EditorSelectionAt(editor, 5, 4))
        return "FAIL: Command-Shift-Left did not select to the start of the line";
    key.message = (0x7c << 8) | 0x1d;
    DispatchEvent(&key);
    if(!EditorSelectionAt(editor, 5, 7))
        return "FAIL: Command-Shift-Right did not select to the end of the line";
    key.message = (0x7e << 8) | 0x1e;
    DispatchEvent(&key);
    if(!EditorSelectionAt(editor, 5, 0))
        return "FAIL: Command-Shift-Up did not select to the start of the document";
    key.message = (0x7d << 8) | 0x1f;
    DispatchEvent(&key);
    if(!EditorSelectionAt(editor, 5, sizeof(lines) * kRepeats / sizeof(UniChar)))
        return "FAIL: Command-Shift-Down did not select to the end of the document";

    key.modifiers = cmdKey;
    key.message = (0x7e << 8) | 0x1e;
    DispatchEvent(&key);
    key.modifiers = shiftKey;
    key.message = (0x7c << 8) | 0x1d;
    DispatchEvent(&key);
    Rect view = EditorBounds(window);
    Point mouse = {view.top + 1, view.left + 1};
    ScrollWheelAt(window, mouse, -1);
    if(EditorTopOffset(editor) != 12 || !EditorSelectionAt(editor, 0, 1))
        return "FAIL: wheel-down did not scroll from the top and preserve the selection";
    ScrollWheelAt(window, mouse, 1);
    if(EditorTopOffset(editor) != 0 || !EditorSelectionAt(editor, 0, 1))
        return "FAIL: wheel-up did not return to the top and preserve the selection";
    mouse.h = view.left - 1;
    ScrollWheelAt(window, mouse, -1);
    if(EditorTopOffset(editor) != 0)
        return "FAIL: wheel input outside the editor scrolled the document";
    mouse.h = view.left + 1;
    ScrollWheelAt(window, mouse, -1);
    unsigned top = EditorTopOffset(editor);
    key.modifiers = cmdKey;
    key.message = 'a';
    DispatchEvent(&key);
    if(top == 0 || EditorTopOffset(editor) != top ||
       !EditorSelectionAt(editor, 0, sizeof(lines) * kRepeats / sizeof(UniChar)))
        return "FAIL: Select All moved the viewport or missed text";
    return NULL;
}

static const char *SmokeTypographyMenus(Editor *editor)
{
    if(gTypography.family != EditorDefaultTypography().family ||
       !EditorTypographyMatches(editor, gTypography))
        return "FAIL: unavailable saved font did not fall back to the default";
    for(short item = 1, count = CountMenuItems(gFontMenu); item <= count; ++item) {
        short mark;
        GetItemMark(gFontMenu, item, &mark);
        if((mark != 0) != (item == gFontItem))
            return "FAIL: the Font menu did not mark exactly the active font";
    }
    EditorTypography initial = gTypography;
    short originalItem = gFontItem;
    short alternate = FindFontItem("\pGeneva");
    if(alternate == 0 || alternate == originalItem)
        alternate = FindFontItem("\pMonaco");
    if(originalItem == 0 || alternate == 0 || alternate == originalItem)
        return "FAIL: installed font families missing from the menu";

    EditorCommand(editor, kSelectAllItem);
    Handle before;
    if(EditorEncodeText(editor, &before) != noErr)
        return "FAIL: could not snapshot the document before switching typography";
    DoMenuCommand(((long)kFontMenu << 16) | alternate);
    DoMenuCommand(((long)kSizeMenu << 16) | SizeItem(24));
    Boolean selected = gFontItem == alternate && gTypography.family != initial.family &&
                       EditorTypographyMatches(editor, gTypography) && gTypography.points == 24;
    short mark;
    GetItemMark(gFontMenu, alternate, &mark);
    selected &= mark != 0;
    GetItemMark(gSizeMenu, SizeItem(24), &mark);
    selected &= mark != 0;

    EventRecord key = {.what = keyDown, .message = '-', .modifiers = cmdKey};
    DispatchEvent(&key);
    selected &= gTypography.points == 20 && EditorTypographyMatches(editor, gTypography);
    key.message = '+';
    DispatchEvent(&key);
    selected &= gTypography.points == 24 && EditorTypographyMatches(editor, gTypography);

    Handle after;
    Boolean sameText = EditorEncodeText(editor, &after) == noErr;
    if(sameText) {
        sameText = GetHandleSize(before) == GetHandleSize(after) &&
                   memcmp(*before, *after, GetHandleSize(before)) == 0;
        DisposeHandle(after);
    }
    DisposeHandle(before);
    Boolean editingState = EditorAllSelected(editor) && EditorCanUndo(editor);
    DoMenuCommand(((long)kFontMenu << 16) | originalItem);
    DoMenuCommand(((long)kSizeMenu << 16) | SizeItem(initial.points));
    if(!selected)
        return "FAIL: font, size, checkmarks, or size shortcuts did not apply";
    if(!sameText || !editingState)
        return "FAIL: switching typography changed the document or editing state";
    return NULL;
}

static Boolean SmokeSourceIs(Editor *editor, const char *expected)
{
    Handle bytes;
    if(EditorEncodeText(editor, &bytes) != noErr)
        return false;
    size_t length = strlen(expected);
    Boolean equal = GetHandleSize(bytes) == length && memcmp(*bytes, expected, length) == 0;
    DisposeHandle(bytes);
    return equal;
}

typedef struct {
    Point destination;
    SInt16 modifiers;
    unsigned samples;
} SmokeDragInput;

static OSErr SmokeDragMouse(Point *mouse, SInt16 *modifiers, void *refcon, DragRef drag)
{
    (void)drag;
    SmokeDragInput *input = refcon;
    *mouse = input->destination;
    *modifiers = input->modifiers | (input->samples++ >= 2 ? btnState : 0);
    return noErr;
}

static OSErr SmokeTrackSelection(WindowRef window, Editor *editor, Point start,
                                 Point destination, SInt16 modifiers)
{
    RgnHandle region = EditorTextDragRegion(editor, start);
    if(region == NULL)
        return dragNotAcceptedErr;
    LocalToGlobal(&start);
    LocalToGlobal(&destination);
    EventRecord event = {.what = mouseDown, .where = start, .when = TickCount(), .modifiers = modifiers};
    DragRef drag;
    OSErr status = NewDrag(&drag);
    if(status != noErr) {
        DisposeRgn(region);
        return status;
    }
    SmokeDragInput input = {.destination = destination, .modifiers = modifiers};
    DragInputUPP inputProc = NewDragInputUPP(SmokeDragMouse);
    status = inputProc != NULL ? SetDragInputProc(drag, inputProc, &input) : memFullErr;
    if(status == noErr)
        status = TrackTextDrag(window, &event, region, drag);
    DisposeDrag(drag);
    if(inputProc != NULL)
        DisposeDragInputUPP(inputProc);
    DisposeRgn(region);
    return status;
}

static const char *SmokeSelectionDrag(WindowRef window, Editor *editor)
{
    static const UniChar source[] = u"one two three";
    Handle text;
    if(PtrToHand(source, &text, sizeof(source) - sizeof(*source)) != noErr)
        return "FAIL: allocate the selection drag fixture";
    if(EditorTakeText(editor, text) != noErr) {
        DisposeHandle(text);
        return "FAIL: load the selection drag fixture";
    }
    unsigned ordinal;
    if(EditorSearch(editor, "\ptwo ", 1, &ordinal) != 1 || !EditorSelectionAt(editor, 4, 8))
        return "FAIL: select the drag fixture";
    DrawDocument(window);
    Rect view = EditorBounds(window);
    Point start = {view.top + 6, view.left};
    RgnHandle region = NULL;
    while(start.h < view.right && region == NULL) {
        start.h += 4;
        region = EditorTextDragRegion(editor, start);
    }
    if(region == NULL)
        return "FAIL: selected text did not have a draggable region";
    DisposeRgn(region);
    Point beginning = {view.top + 6, view.left + 1};
    if(SmokeTrackSelection(window, editor, start, beginning, 0) != noErr ||
       !SmokeSourceIs(editor, "two one three") || !EditorSelectionAt(editor, 0, 4))
        return "FAIL: native drag did not move the selected text to the pointer";
    EditorCommand(editor, kUndoItem);
    if(!SmokeSourceIs(editor, "one two three") || !EditorSelectionAt(editor, 4, 8))
        return "FAIL: Undo did not restore the moved selection";
    EditorCommand(editor, kRedoItem);
    if(!SmokeSourceIs(editor, "two one three") || !EditorSelectionAt(editor, 0, 4))
        return "FAIL: Redo did not restore the move";
    EditorCommand(editor, kUndoItem);
    if(EditorDropSelection(editor, start, false) || EditorCanUndo(editor) ||
       !SmokeSourceIs(editor, "one two three") || !EditorSelectionAt(editor, 4, 8))
        return "FAIL: dropping onto the original selection changed the document";
    Point end = {view.bottom - 4, view.left + 1};
    if(SmokeTrackSelection(window, editor, start, end, optionKey) != noErr ||
       !SmokeSourceIs(editor, "one two threetwo ") || !EditorSelectionAt(editor, 13, 17))
        return "FAIL: Option-drag did not copy the selection";
    EditorCommand(editor, kUndoItem);
    if(!SmokeSourceIs(editor, "one two three") || !EditorSelectionAt(editor, 4, 8))
        return "FAIL: Undo did not remove the copied selection";
    if(SmokeTrackSelection(window, editor, start, end, 0) != noErr ||
       !SmokeSourceIs(editor, "one threetwo ") || !EditorSelectionAt(editor, 9, 13))
        return "FAIL: dragging forward did not adjust the drop position";
    return NULL;
}

static const char *SmokeTextDrop(WindowRef window, Editor *editor)
{
    static const UniChar source[] = u"Document";
    static const UniChar unicode[] = u"\u00e9\r\n";
    static const char utf8[] = "\xc3\xa9\n";
    static const char roman[] = "\x8e\r";
    const struct { FlavorType flavor; const void *data; Size size; } cases[] = {
        {'utxt', unicode, sizeof(unicode) - sizeof(*unicode)},
        {'utf8', utf8, sizeof(utf8) - 1},
        {'TEXT', roman, sizeof(roman) - 1}
    };
    Handle text;
    if(PtrToHand(source, &text, sizeof(source) - sizeof(*source)) != noErr)
        return "FAIL: allocate the text drop fixture";
    if(EditorTakeText(editor, text) != noErr) {
        DisposeHandle(text);
        return "FAIL: load the text drop fixture";
    }
    EditorCommand(editor, kSelectAllItem);
    Rect view = EditorBounds(window);
    Point sidebar = {view.top + 1, 10};
    Point start = {view.top + 1, view.left + 1};
    for(unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        DragRef drag;
        if(NewDrag(&drag) != noErr)
            return "FAIL: create the text drag";
        Boolean accepted = AddDragItemFlavor(drag, 1, cases[i].flavor, cases[i].data,
                                             cases[i].size, 0) == noErr &&
                           ProcessDrop(drag, window, NULL) == kDropText &&
                           ProcessDrop(drag, window, &sidebar) == 0 &&
                           ProcessDrop(drag, window, &start) == kDropText;
        DisposeDrag(drag);
        if(!accepted || !SmokeSourceIs(editor, "\xc3\xa9\nDocument") ||
           !EditorSelectionAt(editor, 2, 2))
            return "FAIL: a text drop did not insert at the pointer";
        EditorCommand(editor, kUndoItem);
        if(!SmokeSourceIs(editor, "Document") || !EditorAllSelected(editor))
            return "FAIL: Undo did not restore text and selection after a drop";
    }
    return NULL;
}

static const char *SmokeFormatting(Editor *editor)
{
    static const UniChar sample[] = u"word";
    static const UniChar underscored[] = u" __word__ ";
    static const struct { char key; const char *source; unsigned prefix; } formats[] = {
        {'b', "**word**", 2}, {'i', "*word*", 1}
    };
    Handle source = NewHandle(sizeof(sample) - sizeof(*sample));
    if(source == NULL)
        return "FAIL: allocate the formatting fixture";
    memcpy(*source, sample, sizeof(sample) - sizeof(*sample));
    if(EditorTakeText(editor, source) != noErr) {
        DisposeHandle(source);
        return "FAIL: load the formatting fixture";
    }
    EditorCommand(editor, kSelectAllItem);
    EventRecord key = {.what = keyDown, .modifiers = cmdKey};
    for(unsigned i = 0; i < sizeof(formats) / sizeof(*formats); ++i) {
        key.message = formats[i].key;
        DispatchEvent(&key);
        if(!SmokeSourceIs(editor, formats[i].source) ||
           !EditorSelectionAt(editor, formats[i].prefix, formats[i].prefix + 4))
            return "FAIL: format shortcut did not wrap and reselect the Markdown text";
        DispatchEvent(&key);
        if(!SmokeSourceIs(editor, "word") || !EditorAllSelected(editor))
            return "FAIL: format shortcut did not remove the surrounding Markdown";
    }
    key.message = 'z';
    DispatchEvent(&key);
    if(!SmokeSourceIs(editor, "*word*") || !EditorSelectionAt(editor, 1, 5))
        return "FAIL: one Undo did not restore the formatting and selection";

    key.message = 'b';
    DispatchEvent(&key);
    if(!SmokeSourceIs(editor, "***word***"))
        return "FAIL: bold and italic did not combine";
    key.message = 'i';
    DispatchEvent(&key);
    if(!SmokeSourceIs(editor, "**word**"))
        return "FAIL: toggling italic removed the nested bold";

    source = NewHandle(sizeof(underscored) - sizeof(*underscored));
    if(source == NULL)
        return "FAIL: allocate the underscore formatting fixture";
    memcpy(*source, underscored, sizeof(underscored) - sizeof(*underscored));
    if(EditorTakeText(editor, source) != noErr) {
        DisposeHandle(source);
        return "FAIL: load the underscore formatting fixture";
    }
    EditorCommand(editor, kSelectAllItem);
    key.message = 'b';
    DispatchEvent(&key);
    if(!SmokeSourceIs(editor, " word ") || !EditorSelectionAt(editor, 1, 5))
        return "FAIL: format shortcut did not remove the selected underscore pair";

    key.message = 0x1d;
    key.modifiers = 0;
    DispatchEvent(&key);
    key.message = 'i';
    key.modifiers = cmdKey;
    DispatchEvent(&key);
    if(!SmokeSourceIs(editor, " word** ") || !EditorSelectionAt(editor, 6, 6))
        return "FAIL: format shortcut did not place the caret inside an empty pair";
    DispatchEvent(&key);
    if(!SmokeSourceIs(editor, " word ") || !EditorSelectionAt(editor, 5, 5))
        return "FAIL: format shortcut did not cancel the empty pair";
    return NULL;
}

static const char *SmokeViewModes(Editor *editor, WindowRef window)
{
    Rect initial = EditorBounds(window);
    EventRecord shortcut = {.what = keyDown, .modifiers = cmdKey, .message = '1'};
    DispatchEvent(&shortcut);
    Rect withoutSidebar = EditorBounds(window);
    if(gSidebarVisible || withoutSidebar.left != initial.left - kSidebarWidth ||
       withoutSidebar.bottom != initial.bottom)
        return "FAIL: hiding the sidebar did not expand the writing surface";
    DoMenuCommand(((long)kViewMenu << 16) | kFocusItem);
    Rect focused = EditorBounds(window);
    if(!gFocusMode || focused.left != withoutSidebar.left ||
       focused.bottom != withoutSidebar.bottom + 23)
        return "FAIL: Focus did not hide the footer";
    DoMenuCommand(((long)kViewMenu << 16) | kFocusItem);
    DoMenuCommand(((long)kViewMenu << 16) | kSidebarItem);
    Rect restored = EditorBounds(window);
    if(gFocusMode || !gSidebarVisible || !EqualRect(&initial, &restored) ||
       editor != GetDocument(window))
        return "FAIL: leaving Focus did not restore the original layout";
    Str255 label;
    DoMenuCommand(((long)kViewMenu << 16) | (kFirstCounterItem + kCounterCharacters));
    FooterCounter(editor, label);
    if(!EqualString(label, "\p144 characters", true, true))
        return "FAIL: View did not switch to the character count";
    DoMenuCommand(((long)kViewMenu << 16) | (kFirstCounterItem + kCounterHidden));
    FooterCounter(editor, label);
    if(label[0] != 0)
        return "FAIL: View did not hide the counter";
    DoMenuCommand(((long)kViewMenu << 16) | kFirstCounterItem);
    Rect canvas = WindowCanvas(window);
    EventRecord click = {.what = mouseDown, .where = {canvas.bottom - 10, canvas.right - 30}};
    SetPortWindowPort(window);
    LocalToGlobal(&click.where);
    DispatchEvent(&click);
    if(gSettings.counter != kCounterCharacters)
        return "FAIL: clicking the word count did not switch to characters";
    DispatchEvent(&click);
    if(gSettings.counter != kCounterWords)
        return "FAIL: clicking the character count did not switch to words";
    return NULL;
}

static const char *SmokeMarkdownCommands(Editor *editor)
{
    static const UniChar word[] = u"note";
    Handle source = NewHandle(sizeof(word) - sizeof(*word));
    if(source == NULL)
        return "FAIL: allocate the Markdown command fixture";
    memcpy(*source, word, sizeof(word) - sizeof(*word));
    if(EditorTakeText(editor, source) != noErr) {
        DisposeHandle(source);
        return "FAIL: load the Markdown command fixture";
    }
    EditorCommand(editor, kSelectAllItem);
    static const char *const expected[] = {"# note", "> note", "- note", "- [ ] note"};
    for(short item = kHeadingItem; item <= kTaskItem; ++item) {
        DoMenuCommand(((long)kEditMenu << 16) | item);
        if(!SmokeSourceIs(editor, expected[item - kHeadingItem]))
            return "FAIL: Markdown block command did not apply its prefix";
        EditorCommand(editor, kUndoItem);
        if(!SmokeSourceIs(editor, "note") || !EditorAllSelected(editor))
            return "FAIL: Undo did not restore the source and selection";
    }
    EventRecord link = {.what = keyDown, .modifiers = cmdKey, .message = 'k'};
    DispatchEvent(&link);
    if(!SmokeSourceIs(editor, "[note]()") || !EditorSelectionAt(editor, 7, 7))
        return "FAIL: Command-K did not insert the link and focus its destination";
    return NULL;
}

static Boolean SmokeColorAt(short x, short y, const RGBColor *expected)
{
    RGBColor pixel;
    GetCPixel(x, y, &pixel);
    return abs(pixel.red - expected->red) < 0x800 &&
           abs(pixel.green - expected->green) < 0x800 &&
           abs(pixel.blue - expected->blue) < 0x800;
}

static const char *SmokeOutline(Editor *editor, WindowRef window)
{
    static const UniChar fixture[] = u"# First\r\rBody\r\r## Second\r";
    Handle source = NewHandle(sizeof(fixture) - sizeof(*fixture));
    if(source == NULL)
        return "FAIL: allocate the outline fixture";
    memcpy(*source, fixture, sizeof(fixture) - sizeof(*fixture));
    if(EditorTakeText(editor, source) != noErr) {
        DisposeHandle(source);
        return "FAIL: load the outline fixture";
    }
    Rect original = EditorBounds(window);
    Rect hidden = OutlineBounds(original);
    if(!EmptyRect(&hidden) || EditorOutlineCount(editor) != 0)
        return "FAIL: the optional outline was visible by default";

    EventRecord shortcut = {.what = keyDown, .modifiers = cmdKey, .message = '2'};
    DispatchEvent(&shortcut);
    Rect view = EditorBounds(window);
    Rect bounds = OutlineBounds(view);
    if(EmptyRect(&bounds) || !EqualRect(&view, &original) ||
       bounds.left <= view.left || bounds.right != view.right - kEditorScrollbarWidth ||
       EditorOutlineCount(editor) != 2)
        return "FAIL: Command-2 did not place the outline inside the document view";
    Str255 label;
    unsigned level;
    if(!EditorOutlineLabel(editor, 1, label, &level) ||
       !EqualString(label, "\pSecond", true, true) || level != 2)
        return "FAIL: the outline did not show the second heading label";
    if(EditorOutlinePage(editor, 1, 1) != 1 || EditorOutlinePage(editor, 1, -1) != 0)
        return "FAIL: the outline did not page through its headings";
    DrawDocument(window);
    SetPortWindowPort(window);
    RGBColor page, rail;
    GetCPixel(bounds.left - 2, bounds.top + 2, &page);
    GetCPixel(bounds.left, bounds.top + 2, &rail);
    if(page.red != rail.red || page.green != rail.green || page.blue != rail.blue)
        return "FAIL: the outline drew a separate background or divider";
    for(short mode = kLightTheme; mode <= kDarkTheme; ++mode) {
        DoMenuCommand(((long)kViewMenu << 16) | mode);
        InvalidateWindow(window);
        DrawDocument(window);
        const EditorTheme *theme = &kThemes[mode - 1];
        Boolean labelInk = false, caretInk = false;
        short secondRow = bounds.top + kOutlineFirstRow + kOutlineRowHeight;
        for(short y = secondRow; y < secondRow + kOutlineRowHeight && !labelInk; ++y) {
            for(short x = bounds.left + 15; x < bounds.left + 65 && !labelInk; ++x)
                labelInk = SmokeColorAt(x, y, &theme->secondary);
        }
        for(short y = view.top; y < view.top + 2 * kOutlineRowHeight && !caretInk; ++y) {
            caretInk = SmokeColorAt(view.left, y, &theme->caret) &&
                       SmokeColorAt(view.left + 1, y, &theme->caret) &&
                       !SmokeColorAt(view.left + 2, y, &theme->caret);
        }
        if(!labelInk || !caretInk)
            return "FAIL: the theme did not draw the secondary label and two-pixel caret";
    }
    DoMenuCommand(((long)kViewMenu << 16) | kLightTheme);
    EventRecord click = {.what = mouseDown,
                         .where = {bounds.top + kOutlineFirstRow + kOutlineRowHeight + 3,
                                   bounds.left + 16}};
    LocalToGlobal(&click.where);
    DispatchEvent(&click);
    if(!EditorSelectionAt(editor, 18, 18) || EditorOutlineCurrent(editor) != 1)
        return "FAIL: clicking an outline row did not jump to its heading";

    DrawDocument(window);
    EventRecord edit = {.what = keyDown, .message = 'N'};
    DispatchEvent(&edit);
    InvalidateRect(&bounds);
    DrawDocument(window);
    short tick = bounds.top + kOutlineFirstRow + kOutlineRowHeight + 5;
    if(!SmokeColorAt(bounds.left + 4, tick, &kThemes[kLightTheme - 1].text))
        return "FAIL: an edit erased the outline before its headings were ready";
    unsigned long now;
    Delay(12, &now);
    EditorIdle(editor);
    if(!EditorOutlineLabel(editor, 1, label, &level) ||
       !EqualString(label, "\pNSecond", true, true))
        return "FAIL: editing a heading did not refresh the outline";
    DoMenuCommand(((long)kViewMenu << 16) | kFocusItem);
    hidden = OutlineBounds(EditorBounds(window));
    if(!EmptyRect(&hidden) || EditorOutlineCount(editor) != 0 || !gSettings.outlineVisible)
        return "FAIL: Focus did not hide the outline while retaining its preference";
    DoMenuCommand(((long)kViewMenu << 16) | kFocusItem);
    if(EditorOutlineCount(editor) != 2)
        return "FAIL: the outline did not return after leaving Focus";
    DispatchEvent(&shortcut);
    return NULL;
}

static const char *SmokeEditHistory(Editor *editor)
{
    Handle empty = NewHandle(0);
    if(empty == NULL)
        return "FAIL: allocate the edit history fixture";
    if(EditorTakeText(editor, empty) != noErr) {
        DisposeHandle(empty);
        return "FAIL: load the edit history fixture";
    }
    EventRecord key = {.what = keyDown};
    for(const char *p = "abc"; *p; ++p) {
        key.message = *p;
        DispatchEvent(&key);
    }
    key.modifiers = cmdKey;
    key.message = 'z';
    DispatchEvent(&key);
    if(!EditorIsEmpty(editor) || !EditorCanRedo(editor) || EditorCanUndo(editor))
        return "FAIL: typing was not grouped into a single Undo";
    key.modifiers = cmdKey | shiftKey;
    key.message = 'Z';
    DispatchEvent(&key);
    if(!SmokeSourceIs(editor, "abc") || !EditorSelectionAt(editor, 3, 3))
        return "FAIL: Shift-Command-Z did not restore the typed text and caret";

    key.modifiers = 0;
    key.message = 'd';
    DispatchEvent(&key);
    key.message = 0x1c;
    DispatchEvent(&key);
    key.message = 'x';
    DispatchEvent(&key);
    EditorCommand(editor, kUndoItem);
    if(!SmokeSourceIs(editor, "abcd") || !EditorSelectionAt(editor, 3, 3))
        return "FAIL: moving the caret did not end the typing group";
    key.message = 'y';
    DispatchEvent(&key);
    if(EditorCanRedo(editor) || !SmokeSourceIs(editor, "abcyd"))
        return "FAIL: a new edit did not invalidate Redo";

    static const UniChar loaded[] = u"Loaded text";
    Handle source = NewHandle(sizeof(loaded) - sizeof(*loaded));
    if(source == NULL)
        return "FAIL: allocate the first-edit fixture";
    memcpy(*source, loaded, sizeof(loaded) - sizeof(*loaded));
    if(EditorTakeText(editor, source) != noErr) {
        DisposeHandle(source);
        return "FAIL: load the first-edit fixture";
    }
    key.message = 'a';
    DispatchEvent(&key);
    if(!SmokeSourceIs(editor, "aLoaded text") || !EditorSelectionAt(editor, 1, 1))
        return "FAIL: the first typed character did not grow the loaded document";
    EditorCommand(editor, kUndoItem);
    if(!SmokeSourceIs(editor, "Loaded text"))
        return "FAIL: undo did not restore the loaded document";
    return NULL;
}

static const char *SmokeFind(Editor *editor, WindowRef window)
{
    static const UniChar fixture[] = u"One two one";
    Handle source = NewHandle(sizeof(fixture) - sizeof(*fixture));
    if(source == NULL)
        return "FAIL: allocate the Find panel fixture";
    memcpy(*source, fixture, sizeof(fixture) - sizeof(*fixture));
    if(EditorTakeText(editor, source) != noErr) {
        DisposeHandle(source);
        return "FAIL: load the Find panel fixture";
    }
    Rect original = EditorBounds(window);
    EventRecord selectWord = {.what = keyDown, .modifiers = optionKey | shiftKey, .message = 0x1d};
    DispatchEvent(&selectWord);
    EventRecord key = {.what = keyDown, .modifiers = cmdKey, .message = 'f'};
    DispatchEvent(&key);
    if(gFind.window != window || gFind.expanded ||
       !EqualString(gFindText, "\pOne", true, true))
        return "FAIL: Command-F did not open the Find panel";
    Rect view = EditorBounds(window);
    Rect overlay = FindBounds(window);
    if(!EqualRect(&original, &view) || overlay.top < view.top || overlay.top >= view.bottom)
        return "FAIL: Find did not float over the original document view";
    key.modifiers = 0;
    for(const char *p = "one"; *p; ++p) {
        key.message = *p;
        DispatchEvent(&key);
    }
    DrawDocument(window);
    if(gFind.count != 2 || gFind.ordinal != 1 || !EditorSelectionAt(editor, 0, 3))
        return "FAIL: Find panel did not select and count live matches";
    key.message = '\r';
    DispatchEvent(&key);
    if(gFind.ordinal != 2 || !EditorSelectionAt(editor, 8, 11))
        return "FAIL: Return did not advance the Find match";
    DispatchEvent(&key);
    if(gFind.ordinal != 1)
        return "FAIL: Find did not wrap";
    key.modifiers = shiftKey;
    DispatchEvent(&key);
    if(gFind.ordinal != 2)
        return "FAIL: Shift-Return did not find the previous match";
    CloseFind();
    key.modifiers = cmdKey | shiftKey;
    key.message = 'g';
    DispatchEvent(&key);
    if(!EditorSelectionAt(editor, 0, 3))
        return "FAIL: Command-Shift-G did not find the previous match with Find closed";
    ShowFind(window, false);
    RunFind(window, 1);

    Rect bounds = FindBounds(window);
    EventRecord click = {.what = mouseDown, .where = {bounds.top + 10, bounds.left + 8}};
    SetPortWindowPort(window);
    LocalToGlobal(&click.where);
    DispatchEvent(&click);
    if(!gFind.expanded || !gFind.replaceField)
        return "FAIL: Find disclosure did not open the Replace field";
    view = EditorBounds(window);
    if(!EqualRect(&original, &view))
        return "FAIL: the Replace row resized the document view";
    key.modifiers = 0;
    key.message = 'x';
    DispatchEvent(&key);
    key.message = '\r';
    DispatchEvent(&key);
    if(gFind.count != 1 || !SmokeSourceIs(editor, "One two x"))
        return "FAIL: Replace did not update the document and match count";
    key.message = 0x1b;
    DispatchEvent(&key);
    Rect restored = EditorBounds(window);
    if(gFind.window != NULL || !EqualRect(&original, &restored))
        return "FAIL: Escape did not restore the document view";
    return NULL;
}

static void SmokeQuickQuery(const char *query)
{
    EventRecord key = {.what = keyDown, .modifiers = cmdKey, .message = 'p'};
    DispatchEvent(&key);
    key.modifiers = 0;
    while(*query != '\0') {
        key.message = (unsigned char)*query++;
        DispatchEvent(&key);
    }
}

static const char *SmokeQuickOpenPanel(WindowRef window)
{
    SmokeQuickQuery("");
    if(!gQuickOpen.visible || gQuickOpen.window != window || gQuickOpen.query[0] != 0)
        return "FAIL: Quick Open palette did not appear";
    DrawDocument(window);
    Rect bounds = QuickOpenBounds(window);
    RGBColor pixel;
    GetCPixel(bounds.right - 10, bounds.top + 8, &pixel);
    if(pixel.red < 0xaaaa)
        return "FAIL: Quick Open prompt background was painted with the text color";
    EventRecord key = {.what = keyDown, .message = 'n'};
    DispatchEvent(&key);
    if(!gQuickOpen.visible || gQuickOpen.query[0] != 1 || gQuickOpen.query[1] != 'n')
        return "FAIL: Quick Open palette did not accept a query";
    key.message = 8;
    DispatchEvent(&key);
    if(gQuickOpen.query[0] != 0)
        return "FAIL: Quick Open palette did not erase its query";
    key.message = 0x1b;
    DispatchEvent(&key);
    if(gQuickOpen.visible)
        return "FAIL: Quick Open palette did not dismiss with Escape";
    return NULL;
}

static const char *SmokeFinderDrop(WindowRef window, const FSSpec *note, const FSSpec *plain)
{
    Workspace *workspace = EditorWorkspace(GetDocument(window));
    DragRef drag;
    if(NewDrag(&drag) != noErr)
        return "FAIL: create a Finder drag";
    HFSFlavor folder = {0}, file = {.fileSpec = *plain};
    Rect view = EditorBounds(window);
    Point document = {view.top + 1, view.left + 1};
    Point sidebar = {view.top + 1, 10};
    FSSpec roots[kSettingsRootLimit];
    unsigned before = WorkspaceCopyFolders(workspace, roots, kSettingsRootLimit);
    Boolean attached = FSMakeFSSpec(note->vRefNum, note->parID, "\p", &folder.fileSpec) == noErr &&
                       AddDragItemFlavor(drag, 1, kDragFlavorTypeHFS, &folder, sizeof(folder), 0) == noErr &&
                       ProcessDrop(drag, window, NULL) == kDropFolder &&
                       ProcessDrop(drag, window, &document) == 0 &&
                       ProcessDrop(drag, window, &sidebar) == kDropFolder &&
                       ProcessDrop(drag, window, &sidebar) == kDropFolder;
    DisposeDrag(drag);
    if(!attached || WorkspaceCopyFolders(workspace, roots, kSettingsRootLimit) != before + 1)
        return "FAIL: dropping a folder did not attach it once, only in the sidebar";
    if(NewDrag(&drag) != noErr)
        return "FAIL: create a file drag";
    Boolean opened = AddDragItemFlavor(drag, 1, kDragFlavorTypeHFS, &file, sizeof(file), 0) == noErr &&
                     AddDragItemFlavor(drag, 1, 'TEXT', "Plain notes.txt", 15, 0) == noErr &&
                     ProcessDrop(drag, window, &document) == kDropFile;
    DisposeDrag(drag);
    WindowRef dropped = FrontWindow();
    if(!opened || dropped == window || GetDocument(dropped) == NULL ||
       !WorkspaceHasFile(EditorWorkspace(GetDocument(dropped)), plain) ||
       !WorkspaceHasFile(workspace, note))
        return "FAIL: dropping a file did not open it in its own window";
    CloseDocument(dropped);
    SelectDocument(window);
    return NULL;
}

static const char *SmokeQuickOpen(WindowRef window, const FSSpec *note, const FSSpec *plain)
{
    Workspace *workspace = EditorWorkspace(GetDocument(window));
    SmokeQuickQuery("Plain notes");
    if(gQuickOpen.count != 1 || !EqualString(gQuickOpen.files[0].name, plain->name, true, true))
        return "FAIL: Quick Open did not filter to the plain-text fixture";
    DrawDocument(window);
    Rect bounds = QuickOpenBounds(window);
    RGBColor pixel;
    Boolean labelVisible = false;
    for(short y = bounds.top + kQuickFieldHeight + 2;
        y < bounds.top + kQuickFieldHeight + 15 && !labelVisible; ++y) {
        for(short x = bounds.left + 9; x < bounds.left + 90 && !labelVisible; ++x) {
            GetCPixel(x, y, &pixel);
            labelVisible = pixel.red > 0xaaaa;
        }
    }
    if(!labelVisible)
        return "FAIL: Quick Open selected filename was invisible";
    EventRecord key = {.what = keyDown, .message = '\r'};
    DispatchEvent(&key);
    if(gQuickOpen.visible || !WorkspaceHasFile(workspace, plain))
        return "FAIL: Return did not open the selected Quick Open file";

    SmokeQuickQuery("First draft");
    if(gQuickOpen.count == 0)
        return "FAIL: Quick Open did not find the Markdown fixture";
    bounds = QuickOpenBounds(window);
    EventRecord click = {.what = mouseDown,
                         .where = {bounds.top + kQuickFieldHeight + 5, bounds.left + 12}};
    LocalToGlobal(&click.where);
    DispatchEvent(&click);
    if(gQuickOpen.visible || !WorkspaceHasFile(workspace, note))
        return "FAIL: clicking a Quick Open result did not open the file";
    RefreshDocumentMenus();
    Str255 label;
    GetMenuItemText(gRecentMenu, 1, label);
    if(gSettings.recentCount != 2 || !EqualString(label, note->name, true, true))
        return "FAIL: Open Recent did not list the files in most-recent order";
    if(!SettingsSmokeRecent(&gSettings))
        return "FAIL: Open Recent did not survive a settings save and reload";
    DoMenuCommand(((long)kRecentMenu << 16) | 1);
    if(FrontWindow() != window || gSettings.recentCount != 2)
        return "FAIL: Open Recent duplicated a document that was already open";
    DoMenuCommand(((long)kRecentMenu << 16) | 2);
    WindowRef recent = FrontWindow();
    if(recent == window || GetDocument(recent) == NULL ||
       !WorkspaceHasFile(EditorWorkspace(GetDocument(recent)), plain) ||
       !EqualString(gSettings.recent[0].name, plain->name, true, true))
        return "FAIL: Open Recent did not reopen the selected file";
    CloseDocument(recent);
    SelectWindow(window);
    DoMenuCommand(((long)kRecentMenu << 16) | (gSettings.recentCount + 2));
    if(gSettings.recentCount != 0)
        return "FAIL: Clear Menu did not clear the recent files";
    return SmokeFinderDrop(window, note, plain);
}

static const char *SmokeDocumentMenus(WindowRef window)
{
    MenuID submenu;
    if(GetMenuItemHierarchicalID(GetMenu(kFileMenu), kOpenRecentItem, &submenu) != noErr ||
       submenu != kRecentMenu)
        return "FAIL: File menu did not attach the Open Recent submenu";
    EventRecord key = {.what = keyDown, .modifiers = cmdKey | shiftKey, .message = 'S'};
    UpdateMenus();
    UInt8 modifiers;
    if(GetMenuItemModifiers(GetMenu(kFileMenu), kSaveAsItem, &modifiers) != noErr ||
       modifiers != kMenuShiftModifier ||
       CommandMenuSelection(&key) != (((long)kFileMenu << 16) | kSaveAsItem))
        return "FAIL: Command-Shift-S did not select Save As";
    key.message = 'G';
    if(GetMenuItemModifiers(GetMenu(kEditMenu), kFindPreviousItem, &modifiers) != noErr ||
       modifiers != kMenuShiftModifier ||
       CommandMenuSelection(&key) != (((long)kEditMenu << 16) | kFindPreviousItem))
        return "FAIL: Command-Shift-G did not select Find Previous";
    HiliteMenu(0);

    WindowRef second = NewDocument(NULL);
    if(second == NULL)
        return "FAIL: create the second Window menu fixture";
    RefreshDocumentMenus();
    short mark;
    GetItemMark(gWindowMenu, kFirstDocumentItem, &mark);
    if(CountMenuItems(gWindowMenu) != kFirstDocumentItem + 1 || mark == 0)
        return "FAIL: Window did not list the two documents and mark the active one";
    DoMenuCommand(((long)kWindowMenu << 16) | (kFirstDocumentItem + 1));
    if(FrontWindow() != window)
        return "FAIL: Window did not select the requested document";
    CloseDocument(second);
    Rect normal = WindowCanvas(window);
    DoMenuCommand(((long)kWindowMenu << 16) | kZoomItem);
    Rect zoomed = WindowCanvas(window);
    DoMenuCommand(((long)kWindowMenu << 16) | kZoomItem);
    Rect restored = WindowCanvas(window);
    if(EqualRect(&normal, &zoomed) || !EqualRect(&normal, &restored))
        return "FAIL: Window Zoom did not enlarge and restore the document";
    return NULL;
}


static const char *SmokeTest(WindowRef window)
{
    Editor *editor = GetDocument(window);
    if(editor == NULL || FrontWindow() != window)
        return "FAIL: could not open the editor window";

    if(CountMenuItems(GetMenu(kEditMenu)) != kTaskItem ||
       CountMenuItems(GetMenu(kFileMenu)) != kQuitItem)
        return "FAIL: the File or Edit menu did not load all commands";

#if TARGET_API_MAC_CARBON
    WindowAttributes attributes;
    if(GetWindowAttributes(window, &attributes) != noErr ||
       (attributes & kWindowFullZoomAttribute) != kWindowFullZoomAttribute)
        return "FAIL: the Carbon document window lacked its zoom button";
    short zoomPart = inZoomOut;
#else
    SetPortWindowPort(window);
    Point box = {-9, WindowCanvas(window).right - 8};
    LocalToGlobal(&box);
    short zoomPart = 0;
    for(short x = 0; x < 48; ++x, --box.h) {
        WindowRef hit;
        short part = FindWindow(box, &hit);
        if(hit == window && part == inZoomOut) {
            zoomPart = part;
            break;
        }
    }
    if(zoomPart == 0)
        return "FAIL: the Classic document window lacked its zoom box";
#endif
    Rect normal = WindowCanvas(window);
    if(ZoomDocument(window, zoomPart) != noErr)
        return "FAIL: document window zoom failed";
    Rect zoomed = WindowCanvas(window);
    if(zoomed.right <= normal.right || zoomed.bottom <= normal.bottom ||
       ZoomDocument(window, inZoomIn) != noErr)
        return "FAIL: document window zoom did not enlarge or restore it";
    Rect restored = WindowCanvas(window);
    if(!EqualRect(&normal, &restored))
        return "FAIL: document window zoom did not restore the original size";

    const char *result = SmokeDocumentMenus(window);
    if(result != NULL)
        return result;
    result = SmokeTextDrop(window, editor);
    if(result != NULL)
        return result;
    result = SmokeSelectionDrag(window, editor);
    if(result != NULL)
        return result;
    result = SmokeEditorInput(window, editor);
    if(result != NULL)
        return result;
    result = SmokeViewModes(editor, window);
    if(result != NULL)
        return result;
    result = SmokeEditHistory(editor);
    if(result != NULL)
        return result;
    result = SmokeOutline(editor, window);
    if(result != NULL)
        return result;
    WorkspaceSmokeReset(EditorWorkspace(editor));
    result = EditorSmokeTest(editor, gATSUITiming, sizeof(gATSUITiming));
    if(strcmp(result, "LINEWREN_ATSUI_SMOKE_OK") != 0)
        return result;
    result = SmokeTypographyMenus(editor);
    if(result != NULL)
        return result;
    result = SmokeFormatting(editor);
    if(result != NULL)
        return result;
    result = SmokeMarkdownCommands(editor);
    if(result != NULL)
        return result;
    result = SmokeFind(editor, window);
    if(result != NULL)
        return result;
    result = SmokeQuickOpenPanel(window);
    if(result != NULL)
        return result;
    window = NewDocument(NULL);
    editor = GetDocument(window);
    if(editor == NULL)
        return "FAIL: could not create the recovery test window";
    result = WorkspaceSmokeRecovery(editor, window);
    CloseDocument(window);
    if(result != NULL)
        return result;
    window = NewDocument(NULL);
    editor = GetDocument(window);
    if(editor == NULL)
        return "FAIL: could not create the Markdown workspace";
    result = WorkspaceSmokeTest(editor, window, SmokeQuickOpen);
    return strcmp(result, "LINEWREN_WORKSPACE_SMOKE_OK") == 0 ? "LINEWREN_ATSUI_SMOKE_OK" : result;
}
#endif

#ifndef LINEWREN_SMOKE_TEST
static void RestoreDrafts(WindowRef first)
{
    short index = 0;
    FSSpec file;
    WindowRef window = first;
    while(WorkspaceNextRecovery(&index, &file)) {
        if(window == NULL)
            window = NewDocument(NULL);
        Editor *editor = GetDocument(window);
        if(editor == NULL)
            break;
        OSErr error = WorkspaceRestoreRecovery(editor, window, &file);
        if(error == noErr)
            window = NULL;
        else
            WorkspaceOpenError(EditorWorkspace(editor), window, error);
    }
}
#endif

int main(void)
{
    EventRecord event;
#ifdef LINEWREN_SMOKE_TEST
    const char *smokeResult;
    OSStatus windowStatus = noErr;
#endif

#if !TARGET_API_MAC_CARBON
    /* Give ATSUI and large UTF-16 documents the full application heap. */
    MaxApplZone();
    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
#endif
    InitCursor();
#ifdef LINEWREN_SMOKE_TEST
    SettingsDefaults(&gSettings);
#else
    SettingsLoad(&gSettings);
#endif

    SetMenuBar(GetNewMBar(128));
    InsertMenu(GetMenu(kViewMenu), 0);
    if(!InitDocumentMenus())
        return 1;
    UpdateThemeMenu();
#if !TARGET_API_MAC_CARBON
    AppendResMenu(GetMenu(kAppleMenu), 'DRVR');
#endif
    DrawMenuBar();

    gTrackDrop = NewDragTrackingHandlerUPP(TrackDrop);
    gReceiveDrop = NewDragReceiveHandlerUPP(ReceiveDrop);
    AEEventHandlerUPP openDocuments = NewAEEventHandlerUPP(OpenDocuments);
    if(openDocuments != NULL)
        AEInstallEventHandler(kCoreEventClass, kAEOpenDocuments, openDocuments, 0, false);
#if TARGET_API_MAC_CARBON
    AEEventHandlerUPP quitApplication = NewAEEventHandlerUPP(QuitApplication);
    if(quitApplication != NULL)
        AEInstallEventHandler(kCoreEventClass, kAEQuitApplication, quitApplication, 0, false);
#endif

    gNextWindowRect = gSettings.window;
#ifdef LINEWREN_SMOKE_TEST
    Boolean editorReady = EditorInit();
    if(editorReady && !InitTypographyMenus()) {
        EditorShutdown();
        editorReady = false;
    }
    if(editorReady) {
        static const Str63 unavailableFont = "\pUnavailable Linewren Test Font";
        memcpy(gSettings.font, unavailableFont, sizeof(unavailableFont));
        ApplySettings();
    }
    smokeResult = editorReady ? SmokeTest(NewDocument(&windowStatus)) : "FAIL: text engine initialization";
    gRunning = false;
#else
    if(!EditorInit())
        return 1;
    if(!InitTypographyMenus()) {
        EditorShutdown();
        return 1;
    }
    ApplySettings();
    WindowRef first = NewDocument(NULL);
    if(first == NULL)
        gRunning = false;
    else {
        RestoreDrafts(first);
        StartWheelInput();
    }
#endif

    while(gRunning) {
        if(WaitNextEvent(everyEvent, &event, 6, NULL))
            DispatchEvent(&event);
        else {
            WindowRef window = FrontWindow();
            Editor *editor = GetDocument(window);
            if(editor != NULL) {
                SetPortWindowPort(window);
                EditorIdle(editor);
                if(gForeground)
                    WorkspaceCheckExternal(EditorWorkspace(editor), window, false);
            }
        }
#if !TARGET_API_MAC_CARBON
        if(gRunning)
            PollWheelInput();
#endif
        for(WindowRef window = FrontWindow(); gRunning && window != NULL; window = GetNextWindow(window)) {
            Editor *editor = GetDocument(window);
            if(editor != NULL)
                WorkspaceIdle(editor, window);
        }
    }

#ifndef LINEWREN_SMOKE_TEST
    SaveSettings();
#endif
    while(FrontWindow() != NULL) {
        WindowRef window = FrontWindow();
#if !TARGET_API_MAC_CARBON
        if(GetWindowKind(window) < 0)
            CloseDeskAcc(GetWindowKind(window));
        else
#endif
            CloseDocument(window);
    }
    if(gTrackDrop != NULL)
        DisposeDragTrackingHandlerUPP(gTrackDrop);
    if(gReceiveDrop != NULL)
        DisposeDragReceiveHandlerUPP(gReceiveDrop);
    if(openDocuments != NULL) {
        AERemoveEventHandler(kCoreEventClass, kAEOpenDocuments, openDocuments, false);
        DisposeAEEventHandlerUPP(openDocuments);
    }
#if TARGET_API_MAC_CARBON
    if(quitApplication != NULL) {
        AERemoveEventHandler(kCoreEventClass, kAEQuitApplication, quitApplication, false);
        DisposeAEEventHandlerUPP(quitApplication);
    }
#endif
#ifdef LINEWREN_SMOKE_TEST
    if(editorReady)
        EditorShutdown();
    ClassicTestReport report;
    if(!ClassicTestBegin(&report, 1))
        return 1;
    const char *expected =
        "LINEWREN_ATSUI_SMOKE_OK";
    char diagnostic[256];
    if(windowStatus != noErr) {
        sprintf(diagnostic, "%s (OSStatus %ld, free heap %ld bytes)",
                smokeResult, (long)windowStatus, (long)FreeMem());
    } else {
        strcpy(diagnostic, smokeResult);
    }
    ClassicTestResult(&report, windowStatus == noErr && strcmp(smokeResult, expected) == 0,
                      "Linewren application smoke suite", diagnostic);
    char *heap = strchr(gATSUITiming, '\n');
    if(heap != NULL) {
        *heap++ = '\0';
        fprintf(report.file, "# %s\n# %s\n", gATSUITiming, heap);
    }
    return ClassicTestEnd(&report);
#else
#if !TARGET_API_MAC_CARBON
    if(gWheelStarted)
        ISpShutdown();
#endif
    EditorShutdown();
#endif
    return 0;
}
