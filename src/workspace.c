#include "mac-toolbox.h"
#include "workspace.h"
#include "document.h"
#include "text-files.h"

#include <Dialogs.h>
#include <AERegistry.h>
#include <Aliases.h>
#include <AppleEvents.h>
#include <Finder.h>
#include <Fonts.h>
#include <Folders.h>
#include <Memory.h>
#include <Menus.h>
#include <Navigation.h>
#include <Processes.h>
#include <Quickdraw.h>
#include <Sound.h>
#include <StandardFile.h>
#include <TextUtils.h>
#include <stdio.h>
#include <string.h>

enum {
    kRows = 64, kRowHeight = 19, kFirstRow = 31, kPagerHeight = 24,
    kMaxDepth = 24, kAutosaveDelay = 75, kExternalCheckDelay = 180, kRecoveryDelay = 5 * 60,
    kRecoveryPrefix = 9
};

typedef struct SidebarFolder {
    struct SidebarFolder *next;
    short volume;
    long directory;
    Str63 name;
    Boolean expanded;
} SidebarFolder;

typedef enum { kSeparator, kFolderRow, kFileRow, kUnlisted } SidebarRowKind;

typedef struct {
    FSSpec file;
    long directory;
    short depth;
    SidebarRowKind kind;
    Boolean expanded;
} SidebarEntry;

typedef struct {
    unsigned long id, modified;
    long size;
} DiskStamp;

struct Workspace {
    FSSpec file;
    Handle diskBytes;
    DiskStamp diskStamp;
    Boolean diskStampKnown, diskChanged;
    unsigned long checkedAt;
    Boolean dirty;
    Boolean pending;
    unsigned long editedAt;
    FSSpec recovery;
    Boolean recoveryPending, recovered;
    unsigned long recoveryAt;
    Str255 status;
    SidebarFolder *roots;
    SidebarFolder *expanded;
    SidebarEntry rows[kRows];
    short rowCount;
    long firstRow;
    Boolean more;
};

static void NewFileInFolder(Editor *editor, WindowRef window, const SidebarEntry *folder);
static OSErr AskName(WindowRef window, ConstStr255Param initial, Boolean rename, Str255 name);

static void Status(Workspace *workspace, WindowRef window, ConstStr255Param message)
{
    Rect area = WindowCanvas(window);
    memcpy(workspace->status, message, message[0] + 1);
    area.top = area.bottom - 23;
    SetPortWindowPort(window);
    InvalidateRect(&area);
}

static void FileError(Workspace *workspace, WindowRef window, OSErr error, Boolean saving)
{
    Str255 message;
    if(error == fBsyErr && saving) {
        Status(workspace, window, "\pFile changed on disk. Use Save As to keep your edits.");
    } else if(error == kTextMalformedInputErr) {
        Status(workspace, window, "\pThis document must contain valid UTF-8 text.");
    } else if(error == memFullErr) {
        Status(workspace, window, "\pDocument too large or insufficient memory.");
    } else {
        int length = snprintf((char *)message + 1, sizeof(message) - 1,
                              "%s failed (error %d)", saving ? "Save" : "Open", error);
        message[0] = (unsigned char)length;
        Status(workspace, window, message);
    }
    SysBeep(1);
}

Workspace *WorkspaceNew(void)
{
    ConstStr255Param initial = "\pUntitled - use File > Save to start autosaving.";
    Workspace *workspace = (Workspace *)NewPtrClear(sizeof(Workspace));
    if(workspace != NULL)
        memcpy(workspace->status, initial, initial[0] + 1);
    return workspace;
}

void WorkspaceDelete(Workspace *workspace)
{
    if(workspace->diskBytes != NULL)
        DisposeHandle(workspace->diskBytes);
    SidebarFolder *lists[] = {workspace->roots, workspace->expanded};
    for(unsigned i = 0; i < 2; ++i) {
        for(SidebarFolder *folder = lists[i]; folder != NULL;) {
            SidebarFolder *next = folder->next;
            DisposePtr((Ptr)folder);
            folder = next;
        }
    }
    DisposePtr((Ptr)workspace);
}

static DocumentKind NameKind(ConstStr255Param name, OSType type)
{
    return DocumentKindForName(name + 1, name[0], type);
}

static OSErr ReadFileCatalog(const FSSpec *file, CInfoPBRec *info)
{
    *info = (CInfoPBRec){0};
    info->hFileInfo.ioNamePtr = (StringPtr)file->name;
    info->hFileInfo.ioVRefNum = file->vRefNum;
    info->hFileInfo.ioDirID = file->parID;
    return PBGetCatInfoSync(info);
}

static DocumentKind FileKind(const FSSpec *file)
{
    CInfoPBRec info;
    return ReadFileCatalog(file, &info) == noErr
        ? NameKind(file->name, info.hFileInfo.ioFlFndrInfo.fdType)
        : NameKind(file->name, 0);
}

static OSErr ReadDiskStamp(const FSSpec *file, DiskStamp *stamp)
{
    CInfoPBRec info;
    OSErr error = ReadFileCatalog(file, &info);
    if(error == noErr) {
        if(info.hFileInfo.ioFlAttrib & kioFlAttribDirMask)
            return fnfErr;
        *stamp = (DiskStamp){info.hFileInfo.ioDirID, info.hFileInfo.ioFlMdDat,
                             info.hFileInfo.ioFlLgLen};
    }
    return error;
}

static void ResetDiskCheck(Workspace *workspace)
{
    workspace->checkedAt = TickCount();
    workspace->diskStampKnown = ReadDiskStamp(&workspace->file, &workspace->diskStamp) == noErr;
    workspace->diskChanged = false;
}

static OSErr ReadFolderInfo(short volume, long directory, Str63 name)
{
    CInfoPBRec info = {0};
    info.dirInfo.ioNamePtr = name;
    info.dirInfo.ioVRefNum = volume;
    info.dirInfo.ioFDirIndex = -1;
    info.dirInfo.ioDrDirID = directory;
    return PBGetCatInfoSync(&info);
}

static Boolean FolderSpec(const SidebarFolder *folder, FSSpec *spec)
{
    CInfoPBRec info = {0};
    Str63 name;
    info.dirInfo.ioNamePtr = name;
    info.dirInfo.ioVRefNum = folder->volume;
    info.dirInfo.ioFDirIndex = -1;
    info.dirInfo.ioDrDirID = folder->directory;
    if(PBGetCatInfoSync(&info) != noErr)
        return false;
    spec->vRefNum = folder->volume;
    spec->parID = info.dirInfo.ioDrParID;
    memcpy(spec->name, name, name[0] + 1);
    return true;
}

static short VolumeReference(short reference)
{
#if TARGET_API_MAC_CARBON
    return reference; /* OS X FSSpecs already use volume references. */
#else
    WDPBRec info = {0};
    info.ioVRefNum = reference;
    return PBGetWDInfoSync(&info) == noErr ? info.ioWDVRefNum : reference;
#endif
}

static void InvalidateSidebar(WindowRef window)
{
    Rect area = WindowCanvas(window);
    area.right = kSidebarWidth;
    area.bottom -= 23;
    SetPortWindowPort(window);
    InvalidateRect(&area);
}

static SidebarFolder *FindSidebarFolder(SidebarFolder *folder, short volume, long directory)
{
    while(folder != NULL && (folder->volume != volume || folder->directory != directory))
        folder = folder->next;
    return folder;
}

static Boolean FolderExpanded(Workspace *workspace, short volume, long directory)
{
    SidebarFolder *root = FindSidebarFolder(workspace->roots, volume, directory);
    return root != NULL ? root->expanded :
           FindSidebarFolder(workspace->expanded, volume, directory) != NULL;
}

static short SidebarCapacity(WindowRef window)
{
    short rows = (WindowCanvas(window).bottom - 23 - kPagerHeight - kFirstRow) / kRowHeight;
    return rows > kRows ? kRows : rows > 0 ? rows : 1;
}

static short PagerTop(WindowRef window)
{
    return WindowCanvas(window).bottom - 23 - kPagerHeight;
}

static void AddRow(Workspace *workspace, const SidebarEntry *entry, long *skip, short capacity)
{
    if(*skip > 0)
        --*skip;
    else if(workspace->rowCount == capacity)
        workspace->more = true;
    else
        workspace->rows[workspace->rowCount++] = *entry;
}

/* Both the sidebar and Quick Open expose the same catalog entries. */
static OSErr ReadWorkspaceEntry(short volume, long directory, short index, SidebarEntry *entry)
{
    *entry = (SidebarEntry){.file = {.vRefNum = volume, .parID = directory}};
    CInfoPBRec info = {0};
    info.hFileInfo.ioNamePtr = entry->file.name;
    info.hFileInfo.ioVRefNum = volume;
    info.hFileInfo.ioDirID = directory;
    info.hFileInfo.ioFDirIndex = index;
    OSErr error = PBGetCatInfoSync(&info);
    if(error != noErr)
        return error;
    Boolean folder = (info.hFileInfo.ioFlAttrib & kioFlAttribDirMask) != 0;
    unsigned flags = folder ? info.dirInfo.ioDrUsrWds.frFlags : info.hFileInfo.ioFlFndrInfo.fdFlags;
    if((flags & (kIsInvisible | kIsAlias)) != 0 ||
       (!folder && NameKind(entry->file.name, info.hFileInfo.ioFlFndrInfo.fdType) ==
                   kDocumentUnsupported)) {
        entry->kind = kUnlisted;
    } else if(folder) {
        entry->kind = kFolderRow;
        entry->directory = info.dirInfo.ioDrDirID;
    } else {
        entry->kind = kFileRow;
    }
    return noErr;
}

/* Enumerate expanded branches only. Stop as soon as there is a next page. */
static void WalkFolder(Workspace *workspace, short volume, long directory, short depth,
                       long *skip, short capacity)
{
    for(long index = 1; index <= 32767 && !workspace->more; ++index) {
        SidebarEntry entry;
        if(ReadWorkspaceEntry(volume, directory, (short)index, &entry) != noErr)
            break;
        if(entry.kind == kUnlisted)
            continue;
        entry.depth = depth;
        if(entry.kind == kFolderRow) {
            if(FindSidebarFolder(workspace->roots, volume, entry.directory) != NULL)
                continue;
            entry.expanded = depth < kMaxDepth && FolderExpanded(workspace, volume, entry.directory);
        }
        AddRow(workspace, &entry, skip, capacity);
        if(entry.expanded && !workspace->more)
            WalkFolder(workspace, volume, entry.directory, depth + 1, skip, capacity);
    }
}

void WorkspaceRefreshSidebar(Workspace *workspace, WindowRef window)
{
    short capacity = SidebarCapacity(window);
    do {
        long skip = workspace->firstRow;
        workspace->rowCount = 0;
        workspace->more = false;
        for(SidebarFolder *root = workspace->roots; root != NULL && !workspace->more; root = root->next) {
            if(root != workspace->roots) {
                const SidebarEntry separator = {.kind = kSeparator};
                AddRow(workspace, &separator, &skip, capacity);
            }
            SidebarEntry entry = {
                .file = {.vRefNum = root->volume},
                .directory = root->directory, .kind = kFolderRow, .expanded = root->expanded
            };
            memcpy(entry.file.name, root->name, root->name[0] + 1);
            if(!workspace->more)
                AddRow(workspace, &entry, &skip, capacity);
            if(root->expanded && !workspace->more)
                WalkFolder(workspace, root->volume, root->directory, 1, &skip, capacity);
        }
        if(workspace->rowCount != 0 || workspace->firstRow == 0)
            break;
        workspace->firstRow = 0;
    } while(true);
    InvalidateSidebar(window);
}

OSErr WorkspaceAttachFolder(Workspace *workspace, WindowRef window, short volume, long directory)
{
    if(directory == 0)
        return fnfErr;
    volume = VolumeReference(volume);
    SidebarFolder *folder = FindSidebarFolder(workspace->roots, volume, directory);
    if(folder == NULL) {
        folder = (SidebarFolder *)NewPtrClear(sizeof(SidebarFolder));
        if(folder == NULL)
            return memFullErr;
        OSErr error = ReadFolderInfo(volume, directory, folder->name);
        if(error != noErr) {
            DisposePtr((Ptr)folder);
            return error;
        }
        folder->volume = volume;
        folder->directory = directory;
        folder->next = workspace->roots;
        workspace->roots = folder;
        workspace->firstRow = 0;
    }
    folder->expanded = true;
    WorkspaceRefreshSidebar(workspace, window);
    return noErr;
}

OSErr WorkspaceLocateFolder(const FSSpec *file, long *directory)
{
    if(file->name[0] == 0) {
        *directory = file->parID;
        return noErr;
    }
    CInfoPBRec info;
    OSErr error = ReadFileCatalog(file, &info);
    if(error == noErr)
        *directory = (info.hFileInfo.ioFlAttrib & kioFlAttribDirMask) ? info.dirInfo.ioDrDirID : 0;
    return error;
}

static OSErr ToggleFolder(Workspace *workspace, const SidebarEntry *entry)
{
    SidebarFolder *root = FindSidebarFolder(workspace->roots, entry->file.vRefNum, entry->directory);
    if(root != NULL) {
        root->expanded = !root->expanded;
        return noErr;
    }
    SidebarFolder **link = &workspace->expanded;
    while(*link != NULL && ((*link)->volume != entry->file.vRefNum ||
                            (*link)->directory != entry->directory))
        link = &(*link)->next;
    if(*link != NULL) {
        SidebarFolder *folder = *link;
        *link = folder->next;
        DisposePtr((Ptr)folder);
    } else {
        SidebarFolder *folder = (SidebarFolder *)NewPtrClear(sizeof(SidebarFolder));
        if(folder == NULL)
            return memFullErr;
        folder->volume = entry->file.vRefNum;
        folder->directory = entry->directory;
        *link = folder;
    }
    return noErr;
}

void WorkspaceChooseFolder(Workspace *workspace, WindowRef window)
{
    NavDialogOptions options;
    NavReplyRecord reply = {0};
    OSErr error = NavGetDefaultDialogOptions(&options);
    if(error == noErr) {
        options.dialogOptionFlags &= ~kNavAllowMultipleFiles;
        error = NavChooseFolder(NULL, &reply, &options, NULL, NULL, NULL);
    }
    if(error == noErr && reply.validRecord) {
        FSSpec folder;
        AEKeyword keyword;
        DescType type;
        Size length;
        error = AEGetNthPtr(&reply.selection, 1, typeFSS, &keyword, &type,
                           &folder, sizeof(folder), &length);
        if(error == noErr) {
            long directory;
            error = WorkspaceLocateFolder(&folder, &directory);
            if(error == noErr)
                error = WorkspaceAttachFolder(workspace, window, folder.vRefNum, directory);
        }
    }
    if(reply.validRecord)
        NavDisposeReply(&reply);
    if(error != noErr && error != userCanceledErr)
        FileError(workspace, window, error, false);
}

void WorkspaceRestoreFolders(Workspace *workspace, WindowRef window,
                             const FSSpec *folders, unsigned count)
{
    for(unsigned i = count; i-- != 0;) {
        long directory;
        if(WorkspaceLocateFolder(&folders[i], &directory) == noErr)
            WorkspaceAttachFolder(workspace, window, folders[i].vRefNum, directory);
    }
}

unsigned WorkspaceCopyFolders(Workspace *workspace, FSSpec *folders, unsigned capacity)
{
    unsigned count = 0;
    for(SidebarFolder *folder = workspace->roots; folder != NULL && count < capacity;
        folder = folder->next) {
        if(FolderSpec(folder, &folders[count]))
            ++count;
    }
    return count;
}

static void SearchFolder(short volume, long directory, short depth, ConstStr255Param query,
                         FSSpec *files, unsigned capacity, unsigned *count)
{
    if(depth > kMaxDepth || *count == capacity)
        return;
    for(long index = 1; index <= 32767 && *count < capacity; ++index) {
        SidebarEntry entry;
        if(ReadWorkspaceEntry(volume, directory, (short)index, &entry) != noErr)
            break;
        const FSSpec *file = &entry.file;
        if(entry.kind == kFolderRow) {
            SearchFolder(volume, entry.directory, depth + 1, query, files, capacity, count);
        } else if(entry.kind == kFileRow &&
                  DocumentNameMatches(file->name + 1, file->name[0], query + 1, query[0])) {
            Boolean duplicate = false;
            for(unsigned i = 0; i < *count; ++i)
                duplicate |= files[i].vRefNum == file->vRefNum &&
                             files[i].parID == file->parID &&
                             EqualString(files[i].name, file->name, false, true);
            if(duplicate)
                continue;
            files[(*count)++] = *file;
        }
    }
}

unsigned WorkspaceSearch(Workspace *workspace, ConstStr255Param query,
                         FSSpec *files, unsigned capacity)
{
    unsigned count = 0;
    for(SidebarFolder *root = workspace->roots; root != NULL && count < capacity;
        root = root->next)
        SearchFolder(root->volume, root->directory, 0, query, files, capacity, &count);
    return count;
}

static void DrawLabel(short x, short y, short width, ConstStr255Param text)
{
    Str255 label;
    memcpy(label, text, text[0] + 1);
    short left = (text[0] + 1) / 2, right = text[0] - left;
    while(left + right > 0 && StringWidth(label) > width) {
        if(left > right)
            --left;
        else
            --right;
        label[0] = left + right + 1;
        label[left + 1] = 0xc9; /* Mac Roman ellipsis */
        memcpy(label + left + 2, text + text[0] - right + 1, right);
    }
    MoveTo(x, y);
    DrawString(label);
}

short WorkspaceLabelFont(void)
{
    short font;
    GetFNum("\pVerdana", &font);
    if(font == 0)
        GetFNum("\pGeneva", &font);
    return font;
}

void WorkspaceDraw(Workspace *workspace, WindowRef window, const EditorTheme *theme,
                   ConstStr255Param counter, Boolean sidebarVisible, Boolean footerVisible)
{
    short width = WindowCanvas(window).right;
    short bottom = WindowCanvas(window).bottom;
    Rect sidebar = {0, 0, bottom - 23, kSidebarWidth};
    Rect footer = {bottom - 23, 0, bottom, width};
    RGBBackColor(&theme->panel);
    if(sidebarVisible)
        EraseRect(&sidebar);
    if(footerVisible)
        EraseRect(&footer);
    TextFont(WorkspaceLabelFont());
    TextSize(10);
    TextFace(normal);
    RGBForeColor(&theme->divider);
    if(sidebarVisible) {
        MoveTo(kSidebarWidth, 0);
        LineTo(kSidebarWidth, footerVisible ? bottom - 23 : bottom);
    }
    if(footerVisible) {
        MoveTo(0, bottom - 23);
        LineTo(width, bottom - 23);
        RGBForeColor(&theme->text);
        short counterWidth = StringWidth(counter);
        short counterLeft = width - 23 - counterWidth;
        DrawLabel(10, bottom - 8, counterLeft - 20, workspace->status);
        if(counter[0] != 0)
            DrawLabel(counterLeft, bottom - 8, counterWidth, counter);
    }

    if(!sidebarVisible)
        return;

    TextFace(bold);
    RGBForeColor(&theme->secondary);
    DrawLabel(12, 20, kSidebarWidth - 44, "\pDOCUMENTS");
    RGBForeColor(&theme->text);
    DrawLabel(kSidebarWidth - 24, 20, 16, "\p+");
    TextFace(normal);
    if(workspace->roots == NULL) {
        DrawLabel(12, kFirstRow + 13, kSidebarWidth - 24, "\pAdd a folder...");
        return;
    }

    TextSize(11);
    for(short i = 0; i < workspace->rowCount; ++i) {
        const SidebarEntry *entry = &workspace->rows[i];
        short top = kFirstRow + i * kRowHeight;
        if(entry->kind == kSeparator) {
            RGBForeColor(&theme->divider);
            MoveTo(12, top + kRowHeight / 2);
            LineTo(kSidebarWidth - 12, top + kRowHeight / 2);
            continue;
        }

        short indent = 12 + 11 * (entry->depth > 8 ? 8 : entry->depth);
        Boolean active = entry->kind == kFileRow && WorkspaceHasFile(workspace, &entry->file);
        if(active) {
            Rect highlight = {top + 1, 6, top + kRowHeight - 1, kSidebarWidth - 6};
            RGBForeColor(&theme->divider);
            PaintRoundRect(&highlight, 7, 7);
            RGBBackColor(&theme->divider);
        }
        RGBForeColor(&theme->text);
        if(entry->kind == kFolderRow) {
            short middle = top + kRowHeight / 2;
            if(entry->expanded) {
                MoveTo(indent, middle - 2);
                LineTo(indent + 3, middle + 1);
                LineTo(indent + 6, middle - 2);
            } else {
                MoveTo(indent + 1, middle - 3);
                LineTo(indent + 4, middle);
                LineTo(indent + 1, middle + 3);
            }
            if(entry->depth == 0)
                TextFace(bold);
        }
        DrawLabel(indent + 12, top + 13, kSidebarWidth - indent - 22, entry->file.name);
        TextFace(normal);
        RGBBackColor(&theme->panel);
    }
    TextSize(10);
    RGBForeColor(&theme->text);
    short baseline = PagerTop(window) + 16;
    if(workspace->firstRow > 0)
        DrawLabel(12, baseline, 65, "\p< Previous");
    if(workspace->more)
        DrawLabel(kSidebarWidth - 63, baseline, 51, "\pNext >");
}

static Editor *SidebarEditor(WindowRef window)
{
    return GetWindowKind(window) == userKind ? (Editor *)GetWRefCon(window) : NULL;
}

static Boolean InsideFolder(short volume, long directory, long ancestor)
{
    while(directory >= fsRtDirID) {
        if(directory == ancestor)
            return true;
        if(directory == fsRtDirID)
            return false;
        CInfoPBRec info = {0};
        info.dirInfo.ioVRefNum = volume;
        info.dirInfo.ioDrDirID = directory;
        info.dirInfo.ioFDirIndex = -1;
        if(PBGetCatInfoSync(&info) != noErr || info.dirInfo.ioDrParID == directory)
            return true; /* Keep the item if ancestry cannot be verified. */
        directory = info.dirInfo.ioDrParID;
    }
    return false;
}

static Boolean CanTrash(const SidebarEntry *entry, const FSSpec *file, WindowRef current)
{
    if(entry->kind == kFolderRow && entry->directory == fsRtDirID)
        return false;
    short volume = VolumeReference(file->vRefNum);
    for(WindowRef window = FrontWindow(); window != NULL; window = GetNextWindow(window)) {
        Editor *editor = SidebarEditor(window);
        if(editor == NULL)
            continue;
        Workspace *workspace = EditorWorkspace(editor);
        if(entry->kind == kFileRow) {
            if(window != current && WorkspaceHasFile(workspace, file))
                return false;
            continue;
        }
        if(workspace->diskBytes != NULL && workspace->file.vRefNum == volume &&
           InsideFolder(volume, workspace->file.parID, entry->directory))
            return false;
        for(SidebarFolder *root = workspace->roots; root != NULL; root = root->next)
            if(root->volume == volume && InsideFolder(volume, root->directory, entry->directory))
                return false;
    }
    return true;
}

static void RefreshSidebars(void)
{
    GrafPtr port;
    GetPort(&port);
    for(WindowRef window = FrontWindow(); window != NULL; window = GetNextWindow(window)) {
        Editor *editor = SidebarEditor(window);
        if(editor != NULL)
            WorkspaceRefreshSidebar(EditorWorkspace(editor), window);
    }
    SetPort(port);
}

static OSErr RenameEntry(const SidebarEntry *entry, const FSSpec *file, ConstStr255Param name)
{
    OSErr error = FSpRename(file, name);
    if(error != noErr)
        return error;
    FSSpec renamed = *file;
    renamed.vRefNum = VolumeReference(file->vRefNum);
    memcpy(renamed.name, name, name[0] + 1);
    GrafPtr port;
    GetPort(&port);
    for(WindowRef window = FrontWindow(); window != NULL; window = GetNextWindow(window)) {
        Editor *editor = SidebarEditor(window);
        if(editor == NULL)
            continue;
        Workspace *workspace = EditorWorkspace(editor);
        if(entry->kind == kFolderRow) {
            SidebarFolder *root = FindSidebarFolder(workspace->roots, renamed.vRefNum, entry->directory);
            if(root != NULL)
                memcpy(root->name, name, name[0] + 1);
        } else if(WorkspaceHasFile(workspace, file)) {
            workspace->file = renamed;
            SetPortWindowPort(window);
            SetWTitle(window, name);
            EditorSetMarkdown(editor, FileKind(&renamed) == kDocumentMarkdown);
        }
    }
    SetPort(port);
    RefreshSidebars();
    return noErr;
}

static OSErr TrashFolder(short sourceVolume, FSSpec *trash)
{
    short volume;
    long directory;
    OSErr error = FindFolder(VolumeReference(sourceVolume), kTrashFolderType,
                             kCreateFolder, &volume, &directory);
    return error == noErr ? FSMakeFSSpec(volume, directory, "\p", trash) : error;
}

static OSErr MoveToTrash(const FSSpec *file)
{
    FSSpec trash;
    OSErr error = TrashFolder(file->vRefNum, &trash);
    return error == noErr ? FSpCatMove(file, &trash) : error;
}

static OSErr EntryID(const FSSpec *file, Boolean folder, long *id)
{
    CInfoPBRec info;
    OSErr error = ReadFileCatalog(file, &info);
    if(error != noErr)
        return error;
    if(((info.hFileInfo.ioFlAttrib & kioFlAttribDirMask) != 0) != folder)
        return fnfErr;
    *id = folder ? info.dirInfo.ioDrDirID : info.hFileInfo.ioDirID;
    return noErr;
}

static OSErr VerifyEntry(const FSSpec *file, Boolean folder, long expected)
{
    long id;
    OSErr error = EntryID(file, folder, &id);
    return error == noErr && id != expected ? fBsyErr : error;
}

static OSErr TrashEntry(Editor *editor, WindowRef window, const SidebarEntry *entry,
                        const FSSpec *file, long identity)
{
    Workspace *workspace = EditorWorkspace(editor);
    Handle empty = NULL;
    if(entry->kind == kFileRow && WorkspaceHasFile(workspace, file)) {
        empty = NewHandle(0);
        if(empty == NULL)
            return memFullErr;
        if(!WorkspaceCanClose(editor, window)) {
            DisposeHandle(empty);
            return userCanceledErr;
        }
    }
    OSErr error = VerifyEntry(file, entry->kind == kFolderRow, identity);
    if(error == noErr && !CanTrash(entry, file, window))
        error = fBsyErr;
    if(error == noErr)
        error = MoveToTrash(file);
    if(error == noErr && empty != NULL && WorkspaceHasFile(workspace, file)) {
        EditorTakeDocument(editor, empty, true);
        empty = NULL;
        WorkspaceFinishDocument(workspace);
        DisposeHandle(workspace->diskBytes);
        workspace->diskBytes = NULL;
        workspace->file = (FSSpec){0};
        workspace->dirty = workspace->pending = false;
        workspace->diskChanged = workspace->diskStampKnown = false;
        SetWTitle(window, "\pUntitled");
        Status(workspace, window, "\pMoved to Trash");
    }
    if(empty != NULL)
        DisposeHandle(empty);
    if(error == noErr)
        RefreshSidebars();
    return error;
}

static OSErr RevealInFinder(const FSSpec *file)
{
    const OSType signature = 'MACS';
    AEDesc target = {typeNull, NULL}, list = {typeNull, NULL};
    AppleEvent event = {typeNull, NULL}, reply = {typeNull, NULL};
    AliasHandle alias = NULL;
    OSErr error = NewAliasMinimal(file, &alias);
    if(error == noErr)
        error = AECreateDesc(typeApplSignature, &signature, sizeof(signature), &target);
    if(error == noErr)
        error = AECreateList(NULL, 0, false, &list);
    if(error == noErr) {
        HLock((Handle)alias);
        error = AEPutPtr(&list, 0, typeAlias, *alias, GetHandleSize((Handle)alias));
    }
    if(error == noErr)
        error = AECreateAppleEvent(kAEMiscStandards, kAEMakeObjectsVisible, &target,
                                   kAutoGenerateReturnID, kAnyTransactionID, &event);
    if(error == noErr)
        error = AEPutParamDesc(&event, keyDirectObject, &list);
    if(error == noErr)
        error = AESend(&event, &reply, kAEWaitReply | kAENeverInteract, kAENormalPriority,
                       120, NULL, NULL);
    if(error == noErr) {
        long finderError;
        DescType type;
        Size size;
        if(AEGetParamPtr(&reply, keyErrorNumber, typeSInt32, &type, &finderError,
                         sizeof(finderError), &size) == noErr)
            error = finderError;
    }
    if(error == noErr) {
        ProcessSerialNumber process = {0, kNoProcess};
        ProcessInfoRec info = {.processInfoLength = sizeof(info)};
        while(GetNextProcess(&process) == noErr) {
            if(GetProcessInformation(&process, &info) == noErr && info.processSignature == signature) {
                SetFrontProcess(&process);
                break;
            }
        }
    }
    if(alias != NULL)
        DisposeHandle((Handle)alias);
    AEDisposeDesc(&reply);
    AEDisposeDesc(&event);
    AEDisposeDesc(&list);
    AEDisposeDesc(&target);
    return error;
}

static void DetachFolder(Workspace *workspace, const SidebarEntry *entry)
{
    short volume = VolumeReference(entry->file.vRefNum);
    SidebarFolder **link = &workspace->roots;
    while(*link != NULL) {
        SidebarFolder *root = *link;
        if(root->volume == volume && root->directory == entry->directory) {
            *link = root->next;
            DisposePtr((Ptr)root);
            return;
        }
        link = &root->next;
    }
}

static void ShowSidebarMenu(Editor *editor, WindowRef window, Point point, SidebarEntry entry)
{
    enum { kNew = 1, kRename = 2, kReveal = 3, kDetach = 4, kTrash = 5 };
    Workspace *workspace = EditorWorkspace(editor);
    Boolean folder = entry.kind == kFolderRow;
    Boolean root = folder && entry.depth == 0;
    FSSpec file = entry.file;
    if(root) {
        SidebarFolder *attached = FindSidebarFolder(workspace->roots, file.vRefNum, entry.directory);
        if(attached == NULL || !FolderSpec(attached, &file))
            return;
    }
    long identity;
    if(EntryID(&file, folder, &identity) != noErr)
        return;
    MenuRef menu = GetMenu(132);
    if(menu == NULL)
        return;
    if(!root) {
        SetMenuItemText(menu, kDetach, "\p-");
        DisableMenuItem(menu, kDetach);
    }
    if(!folder)
        DeleteMenuItem(menu, kNew);
    if(folder && entry.directory == fsRtDirID)
        DisableMenuItem(menu, kRename);
    if(!CanTrash(&entry, &file, window))
        DisableMenuItem(menu, folder ? kTrash : kTrash - 1);
    LocalToGlobal(&point);
    InsertMenu(menu, -1);
    long selection = PopUpMenuSelect(menu, point.v, point.h, 1);
    DeleteMenu(132);
    DisposeMenu(menu);
    if((selection >> 16) != 132)
        return;
    OSErr error = noErr;
    const char *action = NULL;
    switch((selection & 0xffff) + (folder ? 0 : 1)) {
        case kNew: NewFileInFolder(editor, window, &entry); break;
        case kRename: {
            action = "Rename";
            Str255 name;
            error = AskName(window, file.name, true, name);
            if(error == noErr && !EqualString(file.name, name, true, true)) {
                error = VerifyEntry(&file, folder, identity);
                if(error == noErr)
                    error = RenameEntry(&entry, &file, name);
            }
            break;
        }
        case kReveal: action = "Reveal"; error = RevealInFinder(&file); break;
        case kDetach:
            DetachFolder(workspace, &entry);
            WorkspaceRefreshSidebar(workspace, window);
            break;
        case kTrash:
            action = "Delete";
            ParamText(file.name, "\p", "\p", "\p");
            if(CautionAlert(131, NULL) == 2)
                error = TrashEntry(editor, window, &entry, &file, identity);
            SetPortWindowPort(window);
            break;
    }
    if(error != noErr && error != userCanceledErr && action != NULL) {
        Str255 message;
        message[0] = snprintf((char *)message + 1, 255, "%s failed (error %d).", action, error);
        Status(workspace, window, message);
        SysBeep(1);
    }
}

Boolean WorkspaceClick(Editor *editor, WindowRef window, Point point, Boolean contextual,
                       FSSpec *openFile)
{
    if(point.h >= kSidebarWidth || point.v >= WindowCanvas(window).bottom - 23)
        return false;
    Workspace *workspace = EditorWorkspace(editor);
    openFile->name[0] = 0;
    if(point.v < kFirstRow || workspace->roots == NULL) {
        if(!contextual && (workspace->roots == NULL || point.h >= kSidebarWidth - 32))
            WorkspaceChooseFolder(workspace, window);
    } else if(point.v >= PagerTop(window)) {
        if(!contextual) {
            short capacity = SidebarCapacity(window);
            if(point.h < kSidebarWidth / 2 && workspace->firstRow > 0)
                workspace->firstRow = workspace->firstRow > capacity ?
                                      workspace->firstRow - capacity : 0;
            else if(point.h >= kSidebarWidth / 2 && workspace->more)
                workspace->firstRow += capacity;
            WorkspaceRefreshSidebar(workspace, window);
        }
    } else {
        short row = (point.v - kFirstRow) / kRowHeight;
        if(row < workspace->rowCount) {
            const SidebarEntry *entry = &workspace->rows[row];
            if(contextual && entry->kind != kSeparator) {
                ShowSidebarMenu(editor, window, point, *entry);
            } else if(entry->kind == kFolderRow) {
                if(entry->depth < kMaxDepth) {
                    OSErr error = ToggleFolder(workspace, entry);
                    if(error == noErr)
                        WorkspaceRefreshSidebar(workspace, window);
                    else
                        FileError(workspace, window, error, false);
                }
            } else if(entry->kind == kFileRow && !contextual) {
                *openFile = entry->file;
            }
        }
    }
    return true;
}

Boolean WorkspaceHasFile(Workspace *workspace, const FSSpec *file)
{
    return workspace->diskBytes != NULL && workspace->file.parID == file->parID &&
           EqualString(workspace->file.name, file->name, false, true) &&
           (workspace->file.vRefNum == file->vRefNum ||
            workspace->file.vRefNum == VolumeReference(file->vRefNum));
}

const FSSpec *WorkspaceCurrentFile(const Workspace *workspace)
{
    return workspace->diskBytes != NULL ? &workspace->file : NULL;
}

#ifdef LINEWREN_SMOKE_TEST
static short gRecoveryTestVolume;
static long gRecoveryTestDirectory;
#endif

static OSErr RecoveryDirectory(Boolean create, short *volume, long *directory)
{
#ifdef LINEWREN_SMOKE_TEST
    (void)create;
    *volume = gRecoveryTestVolume;
    *directory = gRecoveryTestDirectory;
    return *directory != 0 ? noErr : fnfErr;
#else
    long preferences;
    OSErr error = FindFolder(kOnSystemDisk, kPreferencesFolderType, create, volume, &preferences);
    if(error != noErr)
        return error;
    FSSpec folder;
    error = FSMakeFSSpec(*volume, preferences, "\pLinewren Recovery", &folder);
    if(error == fnfErr && create)
        return DirCreate(*volume, preferences, folder.name, directory);
    return error == noErr ? EntryID(&folder, true, directory) : error;
#endif
}

Boolean WorkspaceNextRecovery(short *index, FSSpec *file)
{
    if(RecoveryDirectory(false, &file->vRefNum, &file->parID) != noErr)
        return false;
    while(*index < 32767) {
        CInfoPBRec info = {0};
        info.hFileInfo.ioNamePtr = file->name;
        info.hFileInfo.ioVRefNum = file->vRefNum;
        info.hFileInfo.ioDirID = file->parID;
        info.hFileInfo.ioFDirIndex = ++*index;
        if(PBGetCatInfoSync(&info) != noErr)
            break;
        if((info.hFileInfo.ioFlAttrib & kioFlAttribDirMask) ||
           info.hFileInfo.ioFlFndrInfo.fdCreator != 'Lnwr' ||
           info.hFileInfo.ioFlFndrInfo.fdType != 'TEXT' ||
           file->name[0] <= kRecoveryPrefix || file->name[kRecoveryPrefix] != '-' ||
           NameKind(file->name, 0) == kDocumentUnsupported)
            continue;
        unsigned i = 1;
        for(; i < kRecoveryPrefix; ++i) {
            unsigned char c = file->name[i];
            if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                break;
        }
        if(i == kRecoveryPrefix)
            return true;
    }
    return false;
}

static void RecoveryName(const FSSpec *file, Str255 name)
{
    name[0] = file->name[0] - kRecoveryPrefix;
    memcpy(name + 1, file->name + 1 + kRecoveryPrefix, name[0]);
}

OSErr WorkspaceRestoreRecovery(Editor *editor, WindowRef window, const FSSpec *file)
{
    Workspace *workspace = EditorWorkspace(editor);
    if(workspace->dirty || workspace->pending || workspace->diskBytes != NULL)
        return paramErr;
    Handle bytes, text = NULL;
    OSErr error = ReadTextFile(file, &bytes);
    if(error == noErr)
        error = DecodeTextFile(bytes, &text);
    if(bytes != NULL)
        DisposeHandle(bytes);
    if(error != noErr)
        return error;
    SetPortWindowPort(window);
    error = EditorTakeDocument(editor, text, NameKind(file->name, 0) == kDocumentMarkdown);
    if(error != noErr) {
        DisposeHandle(text);
        return error;
    }
    workspace->recovery = *file;
    workspace->recovered = true;
    workspace->dirty = true;
    Str255 name, title = "\pRecovered: ";
    RecoveryName(file, name);
    memcpy(title + 1 + title[0], name + 1, name[0]);
    title[0] += name[0];
    SetWTitle(window, title);
    Status(workspace, window, "\pRecovered draft. Save to keep it as a new file.");
    return noErr;
}

Boolean WorkspaceIsRecovered(const Workspace *workspace)
{
    return workspace->recovered;
}

void WorkspaceFinishDocument(Workspace *workspace)
{
    if(workspace->recovery.name[0] != 0) {
        if(FSpDelete(&workspace->recovery) == noErr)
            FlushVol(NULL, workspace->recovery.vRefNum);
        workspace->recovery.name[0] = 0;
    }
    workspace->recoveryPending = false;
    workspace->recovered = false;
}

static OSErr Checkpoint(Editor *editor)
{
    Workspace *workspace = EditorWorkspace(editor);
    FSSpec file = workspace->recovery;
    Handle expected = NULL, bytes = NULL;
    OSErr error = file.name[0] != 0 ? ReadTextFile(&file, &expected)
        : RecoveryDirectory(true, &file.vRefNum, &file.parID);
    if(error == fnfErr && file.name[0] != 0) {
        file.name[0] = 0;
        error = RecoveryDirectory(true, &file.vRefNum, &file.parID);
    }
    if(error == noErr)
        error = EditorEncodeText(editor, &bytes);
    if(error == noErr && expected != NULL)
        error = WriteTextFile(&file, bytes, expected);
    else if(error == noErr) {
        static const Str63 untitled = "\pUntitled";
        ConstStr255Param name = workspace->diskBytes != NULL ? workspace->file.name : untitled;
        unsigned stem = name[0];
        for(unsigned i = stem; i > 1; --i)
            if(name[i] == '.') {
                stem = i - 1;
                break;
            }
        if(stem > 18)
            stem = 18;
        const char *extension = EditorIsMarkdown(editor) ? ".md" : ".txt";
        unsigned long serial = TickCount();
        error = dupFNErr;
        for(unsigned attempt = 0; attempt < 32 && error == dupFNErr; ++attempt) {
            file.name[0] = snprintf((char *)file.name + 1, sizeof(file.name) - 1,
                                    "%08lx-%.*s%s", serial + attempt, (int)stem, name + 1, extension);
            error = WriteTextFile(&file, bytes, NULL);
        }
    }
    if(expected != NULL)
        DisposeHandle(expected);
    if(bytes != NULL)
        DisposeHandle(bytes);
    if(error == noErr) {
        workspace->recovery = file;
        workspace->recoveryPending = false;
    }
    return error;
}

static OSErr LoadDocument(Editor *editor, WindowRef window, const FSSpec *file,
                          Handle bytes, Handle text)
{
    Workspace *workspace = EditorWorkspace(editor);
    SetPortWindowPort(window);
    OSErr error = EditorTakeDocument(editor, text, FileKind(file) == kDocumentMarkdown);
    if(error != noErr)
        return error;
    WorkspaceFinishDocument(workspace);
    if(workspace->diskBytes != NULL)
        DisposeHandle(workspace->diskBytes);
    workspace->diskBytes = bytes;
    workspace->file = *file;
    workspace->file.vRefNum = VolumeReference(file->vRefNum);
    ResetDiskCheck(workspace);
    workspace->dirty = false;
    workspace->pending = false;
    SetWTitle(window, file->name);
    if(workspace->roots == NULL)
        WorkspaceAttachFolder(workspace, window, file->vRefNum, file->parID);
    else
        InvalidateSidebar(window);
    Status(workspace, window, "\pSaved");
    return noErr;
}

OSErr WorkspaceOpen(Editor *editor, WindowRef window, const FSSpec *file)
{
    Handle bytes, text = NULL;
    OSErr error = ReadTextFile(file, &bytes);
    if(error == noErr)
        error = DecodeTextFile(bytes, &text);
    if(error == noErr && !WorkspaceCanClose(editor, window))
        error = userCanceledErr;
    if(error == noErr)
        error = LoadDocument(editor, window, file, bytes, text);
    if(error != noErr) {
        if(bytes != NULL)
            DisposeHandle(bytes);
        if(text != NULL)
            DisposeHandle(text);
    }
    return error;
}

void WorkspaceOpenError(Workspace *workspace, WindowRef window, OSErr error)
{
    FileError(workspace, window, error, false);
}

void WorkspaceEdited(Workspace *workspace)
{
    workspace->pending = true;
    workspace->editedAt = TickCount();
    if(!workspace->recoveryPending)
        workspace->recoveryAt = workspace->editedAt;
    workspace->recoveryPending = true;
}

static void CommitPending(Workspace *workspace)
{
    workspace->dirty |= workspace->pending;
    workspace->pending = false;
}

static OSErr SaveTo(Editor *editor, WindowRef window, const FSSpec *file, Boolean newFile)
{
    Workspace *workspace = EditorWorkspace(editor);
    Handle bytes;
    OSErr error = EditorEncodeText(editor, &bytes);
    if(error != noErr)
        return error;
    error = WriteTextFile(file, bytes, newFile ? NULL : workspace->diskBytes);
    if(error != noErr) {
        DisposeHandle(bytes);
        return error;
    }
    WorkspaceFinishDocument(workspace);
    if(workspace->diskBytes != NULL)
        DisposeHandle(workspace->diskBytes);
    workspace->diskBytes = bytes;
    workspace->file = *file;
    workspace->file.vRefNum = VolumeReference(file->vRefNum);
    ResetDiskCheck(workspace);
    EditorSetMarkdown(editor, FileKind(file) == kDocumentMarkdown);
    workspace->dirty = false;
    SetWTitle(window, file->name);
    if(newFile) {
        if(workspace->roots == NULL)
            WorkspaceAttachFolder(workspace, window, file->vRefNum, file->parID);
        else
            WorkspaceRefreshSidebar(workspace, window);
    }
    Status(workspace, window, "\pSaved");
    return noErr;
}

void WorkspaceIdle(Editor *editor, WindowRef window)
{
    Workspace *workspace = EditorWorkspace(editor);
    unsigned long now = TickCount();
    OSErr saveError = noErr;
    if(workspace->pending && now - workspace->editedAt >= kAutosaveDelay) {
        SetPortWindowPort(window);
        CommitPending(workspace);
        if(workspace->dirty && workspace->diskBytes != NULL)
            saveError = SaveTo(editor, window, &workspace->file, false);
        if(saveError != noErr)
            FileError(workspace, window, saveError, true);
    }
    /* A successful autosave clears recoveryPending, so it needs no second write. */
    if(workspace->recoveryPending && now - workspace->recoveryAt >= kRecoveryDelay) {
        workspace->recoveryAt = now;
        if(Checkpoint(editor) != noErr && saveError == noErr) {
#ifndef LINEWREN_SMOKE_TEST
            Status(workspace, window, "\pRecovery copy could not be written. Save to keep your edits.");
#endif
        }
    }
}

void WorkspaceCheckExternal(Workspace *workspace, WindowRef window, Boolean force)
{
    if(workspace->diskBytes == NULL || workspace->dirty || workspace->pending)
        return;
    unsigned long now = TickCount();
    if(!force && now - workspace->checkedAt < kExternalCheckDelay)
        return;
    workspace->checkedAt = now;
    DiskStamp stamp;
    OSErr error = ReadDiskStamp(&workspace->file, &stamp);
    if(error == noErr && !force && workspace->diskStampKnown &&
       stamp.id == workspace->diskStamp.id && stamp.modified == workspace->diskStamp.modified &&
       stamp.size == workspace->diskStamp.size)
        return;
    Boolean changed;
    if(error == fnfErr || error == dirNFErr || error == nsvErr) {
        workspace->diskStampKnown = false;
        changed = true;
    } else if(error == noErr) {
        changed = stamp.size != GetHandleSize(workspace->diskBytes);
        if(!changed) {
            Handle current;
            if(ReadTextFile(&workspace->file, &current) != noErr)
                return;
            changed = !SameBytes(workspace->diskBytes, current);
            DisposeHandle(current);
        }
        workspace->diskStamp = stamp;
        workspace->diskStampKnown = true;
    } else {
        return;
    }
    if(changed != workspace->diskChanged) {
        workspace->diskChanged = changed;
        Status(workspace, window, changed ? "\pFile changed on disk. Close and reopen to reload."
                                          : "\pSaved");
    }
}

static OSErr ChooseSaveFile(ConstStr255Param suggested, FSSpec *file)
{
#if TARGET_API_MAC_CARBON
    NavDialogCreationOptions options;
    OSErr error = NavGetDefaultDialogCreationOptions(&options);
    if(error != noErr)
        return error;
    options.optionFlags |= kNavDontAddTranslateItems;
    options.modality = kWindowModalityAppModal;
    options.saveFileName = CFStringCreateWithPascalString(NULL, suggested, kCFStringEncodingMacRoman);
    if(options.saveFileName == NULL)
        return memFullErr;
    NavDialogRef dialog;
    error = NavCreatePutFileDialog(&options, 'TEXT', 'Lnwr', NULL, NULL, &dialog);
    if(error != noErr) {
        CFRelease(options.saveFileName);
        return error;
    }

    NavReplyRecord reply = {0};
    error = NavDialogRun(dialog);
    if(error == noErr && NavDialogGetUserAction(dialog) != kNavUserActionSaveAs)
        error = userCanceledErr;
    if(error == noErr)
        error = NavDialogGetReply(dialog, &reply);
    if(error == noErr) {
        if(!reply.validRecord)
            error = userCanceledErr;
        else if(reply.replacing)
            error = dupFNErr;
        else {
            FSRef folder;
            AEKeyword keyword;
            DescType type;
            Size length;
            FSCatalogInfo info;
            error = AEGetNthPtr(&reply.selection, 1, typeFSRef, &keyword, &type,
                                &folder, sizeof(folder), &length);
            if(error == noErr)
                error = FSGetCatalogInfo(&folder, kFSCatInfoVolume | kFSCatInfoNodeID,
                                         &info, NULL, NULL, NULL);
            if(error == noErr) {
                if(!CFStringGetPascalString(reply.saveFileName, file->name, sizeof(file->name),
                                            kCFStringEncodingMacRoman))
                    error = bdNamErr;
                else {
                    file->vRefNum = info.volume;
                    file->parID = info.nodeID;
                }
            }
        }
        NavDisposeReply(&reply);
    }
    NavDialogDispose(dialog);
    CFRelease(options.saveFileName);
    return error;
#else
    StandardFileReply reply;
    StandardPutFile("\pSave UTF-8 text as:", suggested, &reply);
    if(!reply.sfGood)
        return userCanceledErr;
    *file = reply.sfFile;
    return reply.sfReplacing ? dupFNErr : noErr;
#endif
}

Boolean WorkspaceSave(Editor *editor, WindowRef window, Boolean saveAs)
{
    Workspace *workspace = EditorWorkspace(editor);
    SetPortWindowPort(window);
    CommitPending(workspace);
    OSErr error = noErr;
    if(saveAs || workspace->diskBytes == NULL) {
        FSSpec file;
        Str255 suggested;
        if(workspace->diskBytes == NULL && workspace->recovery.name[0] != 0)
            RecoveryName(&workspace->recovery, suggested);
        else {
            ConstStr255Param name = EditorIsMarkdown(editor) ? "\pUntitled.md" : "\pUntitled.txt";
            memcpy(suggested, name, name[0] + 1);
        }
        error = ChooseSaveFile(suggested, &file);
        SetPortWindowPort(window);
        if(error == userCanceledErr)
            return false;
        if(error == noErr)
            error = SaveTo(editor, window, &file, true);
        if(error == dupFNErr) {
            Status(workspace, window, "\pChoose a new filename.");
            SysBeep(1);
            return false;
        }
    } else if(workspace->dirty) {
        error = SaveTo(editor, window, &workspace->file, false);
    }
    if(error != noErr)
        FileError(workspace, window, error, true);
    return error == noErr;
}

Boolean WorkspaceCanClose(Editor *editor, WindowRef window)
{
    Workspace *workspace = EditorWorkspace(editor);
    SetPortWindowPort(window);
    CommitPending(workspace);
    if(!workspace->dirty)
        return true;
    if(workspace->diskBytes != NULL && WorkspaceSave(editor, window, false))
        return true;
    switch(CautionAlert(129, NULL)) {
        case 1: return WorkspaceSave(editor, window, workspace->diskBytes != NULL);
        case 3: return true;
        default: return false;
    }
}

static OSErr CreateDocument(Editor *editor, WindowRef window, const SidebarEntry *folder,
                            ConstStr255Param name)
{
    FSSpec file;
    OSErr error = FSMakeFSSpec(folder->file.vRefNum, folder->directory, name, &file);
    if(error != fnfErr)
        return error == noErr ? dupFNErr : error;
    Handle bytes = NewHandle(0), text = NewHandle(0);
    if(bytes == NULL || text == NULL) {
        error = memFullErr;
    } else if(!WorkspaceCanClose(editor, window)) {
        error = userCanceledErr;
    } else {
        error = WriteTextFile(&file, bytes, NULL);
        if(error == noErr) {
            error = LoadDocument(editor, window, &file, bytes, text);
            if(error == noErr) {
                Workspace *workspace = EditorWorkspace(editor);
                if(folder->depth < kMaxDepth &&
                   !FolderExpanded(workspace, file.vRefNum, folder->directory))
                    ToggleFolder(workspace, folder);
                WorkspaceRefreshSidebar(workspace, window);
                return noErr;
            }
            FSpDelete(&file);
        }
    }
    if(bytes != NULL)
        DisposeHandle(bytes);
    if(text != NULL)
        DisposeHandle(text);
    return error;
}

static Boolean SidebarName(Str255 name)
{
    short start = 1, end = name[0];
    while(start <= end && name[start] == ' ')
        ++start;
    while(end >= start && name[end] == ' ')
        --end;
    if(end < start)
        return false;
    short length = end - start + 1;
    for(short i = start; i <= end; ++i) {
        if(name[i] < ' ' || name[i] == ':')
            return false;
    }
    memmove(name + 1, name + start, length);
    name[0] = length;
    return length <= 31;
}

static Boolean MarkdownName(Str255 name)
{
    if(!SidebarName(name))
        return false;
    short length = name[0];
    if(NameKind(name, 0) != kDocumentMarkdown) {
        if(length > 28)
            return false;
        memcpy(name + length + 1, ".md", 3);
        name[0] += 3;
    }
    return name[0] <= 31;
}

static OSErr AskName(WindowRef window, ConstStr255Param initial, Boolean rename, Str255 name)
{
    DialogRef dialog = GetNewDialog(130, NULL, (WindowPtr)-1);
    if(dialog == NULL)
        return memFullErr;
    SetPortDialogPort(dialog);
    short type;
    Handle field;
    Rect bounds;
    GetDialogItem(dialog, 4, &type, &field, &bounds);
    SetDialogItemText(field, initial);
    if(rename) {
        SetWTitle(GetDialogWindow(dialog), "\pRename");
        Handle item;
        GetDialogItem(dialog, 1, &type, &item, &bounds);
        SetControlTitle((ControlRef)item, "\pRename");
        GetDialogItem(dialog, 3, &type, &item, &bounds);
        SetDialogItemText(item, "\pEnter a new name:");
    }
    SetDialogDefaultItem(dialog, 1);
    SetDialogCancelItem(dialog, 2);
    short selected = initial[0];
    if(rename)
        for(short i = initial[0]; i > 1; --i)
            if(initial[i] == '.') {
                selected = i - 1;
                break;
            }
    SelectDialogItemText(dialog, 4, 0, selected);
    ShowWindow(GetDialogWindow(dialog));
    short item;
    OSErr error;
    for(;;) {
        ModalDialog(NULL, &item);
        if(item == 2) {
            error = userCanceledErr;
            break;
        }
        if(item != 1)
            continue;
        GetDialogItemText(field, name);
        if(rename ? SidebarName(name) : MarkdownName(name)) {
            error = noErr;
            break;
        }
        SysBeep(1);
        SelectDialogItemText(dialog, 4, 0, 32767);
    }
    DisposeDialog(dialog);
    SetPortWindowPort(window);
    return error;
}

static void NewFileInFolder(Editor *editor, WindowRef window, const SidebarEntry *entry)
{
    Workspace *workspace = EditorWorkspace(editor);
    Str255 name;
    OSErr error = AskName(window, "\pUntitled.md", false, name);
    if(error == noErr)
        error = CreateDocument(editor, window, entry, name);
    if(error == dupFNErr) {
        Status(workspace, window, "\pA file with that name already exists in this folder.");
        SysBeep(1);
    } else if(error != noErr && error != userCanceledErr) {
        FileError(workspace, window, error, true);
    }
}

#ifdef LINEWREN_SMOKE_TEST
void WorkspaceSmokeReset(Workspace *workspace)
{
    workspace->dirty = false;
    workspace->pending = false;
    workspace->recoveryPending = false;
}

const char *WorkspaceSmokeRecovery(Editor *editor, WindowRef window)
{
    const char *result = "FAIL: create the recovery test folder";
    FSSpec folder, original, recovered, saved;
    Boolean originalWritten = false, savedWritten = false, snapshotWritten = false;
    Handle bytes = NULL, check = NULL;
    Editor *restored = NULL;
    if(HGetVol(NULL, &folder.vRefNum, &folder.parID) != noErr)
        return result;
    folder.name[0] = snprintf((char *)folder.name + 1, sizeof(folder.name) - 1,
                              "Linewren-recovery-%08lx", TickCount());
    if(DirCreate(folder.vRefNum, folder.parID, folder.name, &gRecoveryTestDirectory) != noErr)
        return result;
    gRecoveryTestVolume = VolumeReference(folder.vRefNum);
    result = "FAIL: open the named plain-text recovery fixture";
    if(FSMakeFSSpec(gRecoveryTestVolume, gRecoveryTestDirectory, "\pOriginal.txt", &original) != fnfErr ||
       FSMakeFSSpec(gRecoveryTestVolume, gRecoveryTestDirectory, "\pRestored.txt", &saved) != fnfErr)
        goto done;
    bytes = NewHandle(4);
    if(bytes == NULL)
        goto done;
    memcpy(*bytes, "base", 4);
    if(WriteTextFile(&original, bytes, NULL) != noErr)
        goto done;
    originalWritten = true;
    if(WorkspaceOpen(editor, window, &original) != noErr || EditorIsMarkdown(editor))
        goto done;

    Workspace *workspace = EditorWorkspace(editor);
    EventRecord key = {.what = keyDown, .message = 'a'};
    EditorKey(editor, &key);
    EditorCommand(editor, kUndoItem);
    workspace->editedAt -= kAutosaveDelay;
    workspace->recoveryAt -= kRecoveryDelay;
    WorkspaceIdle(editor, window);
    result = "FAIL: a successful autosave did not clear the need for a checkpoint";
    if(workspace->recoveryPending || workspace->recovery.name[0] != 0 ||
       workspace->pending || workspace->dirty)
        goto done;
    EditorKey(editor, &key);
    WorkspaceIdle(editor, window);
    result = "FAIL: checkpoint wrote before the recovery delay";
    if(workspace->recovery.name[0] != 0)
        goto done;
    workspace->recoveryAt -= kRecoveryDelay;
    WorkspaceIdle(editor, window);
    snapshotWritten = workspace->recovery.name[0] != 0;
    if(snapshotWritten)
        recovered = workspace->recovery;
    result = "FAIL: checkpoint did not write pending edits before autosave";
    if(!snapshotWritten || workspace->recoveryPending || !workspace->pending)
        goto done;
    key.message = 'b';
    EditorKey(editor, &key);
    workspace->recoveryAt -= kRecoveryDelay;
    WorkspaceIdle(editor, window);
    if(workspace->recoveryPending || !workspace->pending)
        goto done;
    workspace->recoveryAt -= kRecoveryDelay;
    unsigned long idleAt = workspace->recoveryAt;
    WorkspaceIdle(editor, window);
    result = "FAIL: checkpoint rewrote a draft without a new edit";
    if(workspace->recoveryAt != idleAt)
        goto done;

    result = "FAIL: recover the latest checkpoint as a separate plain-text draft";
    FSSpec found;
    short index = 0;
    if(!WorkspaceNextRecovery(&index, &found) ||
       !EqualString(found.name, recovered.name, true, true))
        goto done;
    Rect view = WindowCanvas(window);
    OSStatus status;
    restored = EditorNew(window, &view, &status);
    if(restored == NULL || WorkspaceRestoreRecovery(restored, window, &found) != noErr ||
       !WorkspaceIsRecovered(EditorWorkspace(restored)) || EditorIsMarkdown(restored) ||
       WorkspaceHasFile(EditorWorkspace(restored), &original))
        goto done;
    if(EditorEncodeText(restored, &check) != noErr || GetHandleSize(check) != 6 ||
       memcmp(*check, "abbase", 6) != 0)
        goto done;
    DisposeHandle(check);
    check = NULL;
    result = "FAIL: recovery changed the original or left a snapshot after Save As";
    if(SaveTo(restored, window, &saved, true) != noErr)
        goto done;
    savedWritten = true;
    index = 0;
    if(WorkspaceNextRecovery(&index, &found) ||
       ReadTextFile(&original, &check) != noErr || !SameBytes(bytes, check))
        goto done;
    EditorDelete(restored);
    restored = EditorNew(window, &view, &status);
    result = "FAIL: checkpoint an untitled Markdown draft";
    if(restored == NULL)
        goto done;
    key.message = 'c';
    EditorKey(restored, &key);
    Workspace *untitled = EditorWorkspace(restored);
    untitled->recoveryAt -= kRecoveryDelay;
    WorkspaceIdle(restored, window);
    index = 0;
    if(!WorkspaceNextRecovery(&index, &found) ||
       NameKind(found.name, 0) != kDocumentMarkdown)
        goto done;
    result = "FAIL: discarding an untitled draft left a recovery copy";
    WorkspaceFinishDocument(untitled);
    index = 0;
    if(WorkspaceNextRecovery(&index, &found))
        goto done;
    result = NULL;

done:
    if(restored != NULL) {
        WorkspaceFinishDocument(EditorWorkspace(restored));
        EditorDelete(restored);
    }
    if(check != NULL)
        DisposeHandle(check);
    if(bytes != NULL)
        DisposeHandle(bytes);
    if(snapshotWritten)
        FSpDelete(&recovered);
    if(savedWritten)
        FSpDelete(&saved);
    if(originalWritten)
        FSpDelete(&original);
    FSpDelete(&folder);
    gRecoveryTestDirectory = 0;
    return result;
}

static const char *CheckSidebar(Editor *editor, WindowRef window, const FSSpec *parent,
                               WorkspaceSmokeCheck check)
{
    Workspace *workspace = EditorWorkspace(editor);
    FSSpec first = *parent, second = *parent, nested, note, plain;
    long firstID, secondID, nestedID;
    Boolean firstCreated = false, secondCreated = false, nestedCreated = false;
    Boolean noteCreated = false, plainCreated = false, plainTrashed = false;
    FSSpec trashed;
    const char *result = "FAIL: could not create sidebar test folders";
    unsigned long stamp = TickCount();
    first.name[0] = snprintf((char *)first.name + 1, sizeof(first.name) - 1,
                             "Linewren-tree-%08lx-a", stamp);
    second.name[0] = snprintf((char *)second.name + 1, sizeof(second.name) - 1,
                              "Linewren-tree-%08lx-b", stamp);
    if(DirCreate(first.vRefNum, first.parID, first.name, &firstID) != noErr)
        goto done;
    firstCreated = true;
    if(DirCreate(second.vRefNum, second.parID, second.name, &secondID) != noErr)
        goto done;
    secondCreated = true;
    nested = first;
    nested.parID = firstID;
    memcpy(nested.name, "\pDrafts", 7);
    if(DirCreate(nested.vRefNum, nested.parID, nested.name, &nestedID) != noErr)
        goto done;
    nestedCreated = true;

    result = "FAIL: workspace roots or nested folders were missing from the sidebar";
    if(WorkspaceAttachFolder(workspace, window, first.vRefNum, firstID) != noErr ||
       WorkspaceAttachFolder(workspace, window, second.vRefNum, secondID) != noErr ||
       workspace->rowCount < 4 || workspace->rows[0].directory != secondID ||
       workspace->rows[1].kind != kSeparator || workspace->rows[2].directory != firstID ||
       workspace->rows[3].directory != nestedID || workspace->rows[3].depth != 1)
        goto done;

    result = "FAIL: collapsing a folder did not hide its descendants";
    Point root = {kFirstRow + 2 * kRowHeight + 5, 18};
    FSSpec ignored;
    WorkspaceClick(editor, window, root, false, &ignored);
    if(workspace->rows[2].expanded || workspace->rows[3].kind != kSeparator)
        goto done;
    WorkspaceClick(editor, window, root, false, &ignored);
    if(!workspace->rows[2].expanded || workspace->rows[3].directory != nestedID)
        goto done;

    result = "FAIL: creating a note did not open it in the selected subfolder";
    SidebarEntry folder = workspace->rows[3];
    Str255 name = "\pFirst draft";
    if(!MarkdownName(name))
        goto done;
    OSErr creation = CreateDocument(editor, window, &folder, name);
    if(creation != noErr) {
        static char report[100];
        snprintf(report, sizeof(report), "FAIL: could not create a note in the selected subfolder (%d)", creation);
        result = report;
        goto done;
    }
    note = workspace->file;
    noteCreated = true;
    if(note.parID != nestedID || !EqualString(note.name, "\pFirst draft.md", true, true) ||
       !EditorIsEmpty(editor))
        goto done;
    result = "FAIL: the destination subfolder stayed collapsed";
    if(!workspace->rows[3].expanded)
        goto done;
    result = "FAIL: new note was not revealed in its subfolder";
    if(workspace->rowCount < 5 || !WorkspaceHasFile(workspace, &workspace->rows[4].file))
        goto done;
    result = "FAIL: creating a duplicate note did not reject the filename";
    if(CreateDocument(editor, window, &folder, name) != dupFNErr)
        goto done;
    result = "FAIL: plain text did not appear in the sidebar or Quick Open search";
    if(FSMakeFSSpec(folder.file.vRefNum, nestedID, "\pPlain notes.txt", &plain) != fnfErr)
        goto done;
    Handle empty = NewHandle(0);
    if(empty == NULL || WriteTextFile(&plain, empty, NULL) != noErr) {
        if(empty != NULL)
            DisposeHandle(empty);
        goto done;
    }
    DisposeHandle(empty);
    plainCreated = true;
    WorkspaceRefreshSidebar(workspace, window);
    FSSpec found[4];
    unsigned matches = WorkspaceSearch(workspace, "\pplain", found, 4);
    if(matches != 1 || !EqualString(found[0].name, plain.name, false, true))
        goto done;
    FSSpec roots[4];
    if(WorkspaceCopyFolders(workspace, roots, 4) < 2)
        goto done;
    result = check(window, &note, &plain);
    if(result != NULL)
        goto done;
    SidebarEntry noteEntry = {.kind = kFileRow};
    result = "FAIL: sidebar rename did not retain the open document";
    if(CanTrash(&noteEntry, &note, NULL) ||
       RenameEntry(&noteEntry, &note, "\pRenamed draft.md") != noErr)
        goto done;
    memcpy(note.name, "\pRenamed draft.md", sizeof("\pRenamed draft.md") - 1);
    Str255 title;
    GetWTitle(window, title);
    if(!WorkspaceHasFile(workspace, &note) || !EqualString(title, note.name, true, true))
        goto done;
    SidebarEntry nestedEntry = {.kind = kFolderRow, .directory = nestedID};
    result = "FAIL: folder rename did not preserve the open child document";
    if(CanTrash(&nestedEntry, &nested, NULL) ||
       RenameEntry(&nestedEntry, &nested, "\pRenamed drafts") != noErr)
        goto done;
    memcpy(nested.name, "\pRenamed drafts", sizeof("\pRenamed drafts") - 1);
    if(!WorkspaceHasFile(workspace, &note))
        goto done;

    result = "FAIL: sidebar Trash move did not preserve the file identity";
    Str63 trashName;
    trashName[0] = snprintf((char *)trashName + 1, sizeof(trashName) - 1,
                            "Linewren-trash-%08lx.txt", stamp);
    if(RenameEntry(&noteEntry, &plain, trashName) != noErr)
        goto done;
    memcpy(plain.name, trashName, trashName[0] + 1);
    FSSpec trash;
    long identity, trashID;
    if(TrashFolder(plain.vRefNum, &trash) != noErr ||
       EntryID(&trash, true, &trashID) != noErr ||
       EntryID(&plain, false, &identity) != noErr ||
       WorkspaceOpen(editor, window, &plain) != noErr ||
       CanTrash(&noteEntry, &plain, NULL) || !CanTrash(&noteEntry, &plain, window) ||
       TrashEntry(editor, window, &noteEntry, &plain, identity) != noErr)
        goto done;
    plainTrashed = true;
    trashed = plain;
    trashed.parID = trashID;
    if(VerifyEntry(&trashed, false, identity) != noErr || EntryID(&plain, false, &identity) != fnfErr ||
       !EditorIsEmpty(editor) || workspace->diskBytes != NULL)
        goto done;
    result = "FAIL: removing a root from the sidebar affected the disk folder";
    SidebarEntry detached = {.file = second, .kind = kFolderRow, .directory = secondID};
    DetachFolder(workspace, &detached);
    if(FindSidebarFolder(workspace->roots, VolumeReference(second.vRefNum), secondID) != NULL ||
       VerifyEntry(&second, true, secondID) != noErr)
        goto done;
    OSErr reveal = RevealInFinder(&note);
    if(reveal != noErr) {
        static char failure[96];
        snprintf(failure, sizeof(failure), "FAIL: Finder reveal returned error %d", reveal);
        result = failure;
        goto done;
    }
    result = NULL;

done:
    if(plainTrashed)
        FSpDelete(&trashed); /* Remove only this test's uniquely named fixture. */
    else if(plainCreated)
        FSpDelete(&plain);
    if(noteCreated)
        FSpDelete(&note);
    if(nestedCreated)
        FSpDelete(&nested);
    if(secondCreated)
        FSpDelete(&second);
    if(firstCreated)
        FSpDelete(&first);
    return result;
}

const char *WorkspaceSmokeTest(Editor *editor, WindowRef window, WorkspaceSmokeCheck check)
{
    static const char original[] = "# Notes\n**Bold** and *italic*\n\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82\n";
    static const char changed[] = "!# Notes\n**Bold** and *italic*\n\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82\n";
    const char *result = "FAIL: could not create the Markdown smoke fixture";
    Workspace *workspace = EditorWorkspace(editor);
    FSSpec file, second;
    Handle bytes = NewHandle(sizeof(changed) - 1), saved = NULL;
    Boolean created = false, secondCreated = false;

    if(bytes == NULL)
        return result;
    if(HGetVol(NULL, &file.vRefNum, &file.parID) != noErr)
        goto done;
    unsigned long stamp = TickCount();
    file.name[0] = (unsigned char)snprintf((char *)file.name + 1, sizeof(file.name) - 1,
                                          "Linewren-check-%08lx.md", stamp);
    second = file;
    second.name[0] = (unsigned char)snprintf((char *)second.name + 1, sizeof(second.name) - 1,
                                            "Linewren-check-%08lx-b.md", stamp);
    SetHandleSize(bytes, sizeof(original) - 1);
    if(MemError() != noErr)
        goto done;
    memcpy(*bytes, original, sizeof(original) - 1);
    if(WriteTextFile(&file, bytes, NULL) != noErr)
        goto done;
    created = true;
    result = "FAIL: could not open the Markdown fixture or find it in the sidebar";
    if(WorkspaceOpen(editor, window, &file) != noErr ||
       !WorkspaceHasFile(workspace, &file) || workspace->rowCount == 0)
        goto done;
    result = "FAIL: opening a file created an Undo action";
    if(EditorCanUndo(editor))
        goto done;

    result = "FAIL: could not edit the Markdown fixture";
    SetHandleSize(bytes, sizeof(changed) - 1);
    if(MemError() != noErr)
        goto done;
    memcpy(*bytes, changed, sizeof(changed) - 1);
    EventRecord key = {.what = keyDown, .message = (0x12 << 8) | '!', .modifiers = shiftKey};
    EditorKey(editor, &key);
    workspace->editedAt -= kAutosaveDelay;
    WorkspaceIdle(editor, window);
    result = "FAIL: autosave did not preserve the edited UTF-8 Markdown";
    if(workspace->dirty || ReadTextFile(&file, &saved) != noErr || !SameBytes(bytes, saved))
        goto done;

    if(WriteTextFile(&file, saved, NULL) != dupFNErr)
        goto done;
    result = "FAIL: autosave did not preserve Undo";
    if(!EditorCanUndo(editor))
        goto done;
    EditorCommand(editor, kUndoItem);
    if(EditorCanUndo(editor) || !WorkspaceSave(editor, window, false) ||
       GetHandleSize(workspace->diskBytes) != sizeof(original) - 1 ||
       memcmp(*workspace->diskBytes, original, sizeof(original) - 1) != 0)
        goto done;

    EditorKey(editor, &key);
    result = "FAIL: an unsuccessful switch did not preserve the current document and Undo";
    if(WorkspaceOpen(editor, window, &second) != fnfErr ||
       !WorkspaceHasFile(workspace, &file) || !EditorCanUndo(editor) || !workspace->pending)
        goto done;

    SetHandleSize(bytes, 0);
    result = "FAIL: could not create the second Markdown fixture";
    if(MemError() != noErr || WriteTextFile(&second, bytes, NULL) != noErr)
        goto done;
    secondCreated = true;

    result = "FAIL: switching files did not replace the document in place";
    Str255 title;
    if(WorkspaceOpen(editor, window, &second) != noErr)
        goto done;
    GetWTitle(window, title);
    if(FrontWindow() != window || !EqualString(title, second.name, true, true) ||
       !WorkspaceHasFile(workspace, &second) || !EditorIsEmpty(editor) ||
       EditorCanUndo(editor) || workspace->dirty || workspace->pending)
        goto done;

    result = "FAIL: switching files lost edits that were waiting to autosave";
    if(WorkspaceOpen(editor, window, &file) != noErr || !SameBytes(saved, workspace->diskBytes))
        goto done;

    SetHandleSize(bytes, sizeof(original) - 1);
    if(MemError() != noErr)
        goto done;
    memcpy(*bytes, original, sizeof(original) - 1);
    result = "FAIL: overwrite protection accepted a stale snapshot";
    if(WriteTextFile(&file, bytes, workspace->diskBytes) != noErr ||
       WriteTextFile(&file, workspace->diskBytes, workspace->diskBytes) != fBsyErr)
        goto done;
    result = "FAIL: clean document did not report an external change";
    workspace->checkedAt -= kExternalCheckDelay;
    WorkspaceCheckExternal(workspace, window, false);
    if(!workspace->diskChanged || workspace->dirty || workspace->pending ||
       !SameBytes(saved, workspace->diskBytes))
        goto done;
    result = "FAIL: the external-change warning did not clear when the disk copy matched again";
    if(WriteTextFile(&file, saved, bytes) != noErr)
        goto done;
    WorkspaceCheckExternal(workspace, window, true);
    if(workspace->diskChanged)
        goto done;
    result = CheckSidebar(editor, window, &file, check);
    if(result == NULL)
        result = "LINEWREN_WORKSPACE_SMOKE_OK";

done:
    if(secondCreated)
        FSpDelete(&second);
    if(created)
        FSpDelete(&file);
    if(saved != NULL)
        DisposeHandle(saved);
    DisposeHandle(bytes);
    return result;
}
#endif
