#ifndef LINEWREN_WORKSPACE_H
#define LINEWREN_WORKSPACE_H

#include "editor.h"
#include <Files.h>
#include <stddef.h>

typedef struct Workspace Workspace;

enum { kSidebarWidth = 174 };

Workspace *WorkspaceNew(void);
void WorkspaceDelete(Workspace *workspace);
Workspace *EditorWorkspace(Editor *editor);
short WorkspaceLabelFont(void);
#if !TARGET_API_MAC_CARBON
short WorkspaceCyrillicFont(void);
short WorkspaceFilenameFont(const FSSpec *file);
short WorkspaceTitleFont(const Workspace *workspace);
#endif
void WorkspaceDraw(Workspace *workspace, WindowRef window, const EditorTheme *theme,
                   ConstStr255Param counter, ConstStr255Param language,
                   Boolean sidebarVisible, Boolean footerVisible);
Rect WorkspaceLanguageBounds(Rect canvas, ConstStr255Param counter, ConstStr255Param language);
void WorkspaceRefreshSidebar(Workspace *workspace, WindowRef window);
Boolean WorkspaceClick(Editor *editor, WindowRef window, Point point, Boolean contextual,
                       FSSpec *openFile);
void WorkspaceChooseFolder(Workspace *workspace, WindowRef window);
/* On success a file has directory ID zero. */
OSErr WorkspaceLocateFolder(const FSSpec *file, long *directory);
OSErr WorkspaceAttachFolder(Workspace *workspace, WindowRef window, short volume, long directory);
void WorkspaceRestoreFolders(Workspace *workspace, WindowRef window,
                             const FSSpec *folders, unsigned count);
unsigned WorkspaceCopyFolders(Workspace *workspace, FSSpec *folders, unsigned capacity);
unsigned WorkspaceSearch(Workspace *workspace, const SearchField *query,
                         FSSpec *files, unsigned capacity);
OSErr WorkspaceOpen(Editor *editor, WindowRef window, const FSSpec *file);
OSErr WorkspaceOpenMacRoman(Editor *editor, WindowRef window, const FSSpec *file);
void WorkspaceReload(Editor *editor, WindowRef window);
void WorkspaceOpenError(Workspace *workspace, WindowRef window, OSErr error);
Boolean WorkspaceHasFile(Workspace *workspace, const FSSpec *file);
const FSSpec *WorkspaceCurrentFile(const Workspace *workspace);
#if !TARGET_API_MAC_CARBON
Boolean WorkspaceRememberSpelling(Editor *editor);
#endif
void WorkspaceEdited(Workspace *workspace);
void WorkspaceIdle(Editor *editor, WindowRef window);
Boolean WorkspaceNextRecovery(short *index, FSSpec *file);
OSErr WorkspaceRestoreRecovery(Editor *editor, WindowRef window, const FSSpec *file);
Boolean WorkspaceIsRecovered(const Workspace *workspace);
void WorkspaceFinishDocument(Workspace *workspace);
void WorkspaceCheckExternal(Workspace *workspace, WindowRef window, Boolean force);
Boolean WorkspaceSave(Editor *editor, WindowRef window, Boolean saveAs);
Boolean WorkspaceCanClose(Editor *editor, WindowRef window);

#ifdef LINEWREN_SMOKE_TEST
void WorkspaceSmokeReset(Workspace *workspace);
const char *WorkspaceSmokeRecovery(Editor *editor, WindowRef window);
typedef const char *(*WorkspaceSmokeCheck)(WindowRef, const FSSpec *, const FSSpec *);
const char *WorkspaceSmokeTest(Editor *editor, WindowRef window, WorkspaceSmokeCheck check);
#endif

#endif
