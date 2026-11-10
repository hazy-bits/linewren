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
void WorkspaceDraw(Workspace *workspace, WindowRef window, const EditorTheme *theme,
                   ConstStr255Param counter, Boolean sidebarVisible, Boolean footerVisible);
void WorkspaceRefreshSidebar(Workspace *workspace, WindowRef window);
Boolean WorkspaceClick(Editor *editor, WindowRef window, Point point, Boolean contextual,
                       FSSpec *openFile);
void WorkspaceChooseFolder(Workspace *workspace, WindowRef window);
void WorkspaceRestoreFolders(Workspace *workspace, WindowRef window,
                             const FSSpec *folders, unsigned count);
unsigned WorkspaceCopyFolders(Workspace *workspace, FSSpec *folders, unsigned capacity);
unsigned WorkspaceSearch(Workspace *workspace, ConstStr255Param query,
                         FSSpec *files, unsigned capacity);
OSErr WorkspaceOpen(Editor *editor, WindowRef window, const FSSpec *file);
void WorkspaceOpenError(Workspace *workspace, WindowRef window, OSErr error);
Boolean WorkspaceHasFile(Workspace *workspace, const FSSpec *file);
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
