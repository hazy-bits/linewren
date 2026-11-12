#ifndef LINEWREN_EDITOR_H
#define LINEWREN_EDITOR_H

#include <Events.h>
#include <Windows.h>
#include <stddef.h>

typedef struct Editor Editor;

enum { kEditorScrollbarWidth = 16 };

/* These values match the Edit menu resource. */
enum {
    kUndoItem = 1,
    kRedoItem = 2,
    kCutItem = 3,
    kCopyItem = 4,
    kPasteItem = 5,
    kClearItem = 6,
    kSelectAllItem = 7,
    kBoldItem = 9,
    kItalicItem = 10,
    kLinkItem = 16,
    kHeadingItem = 17,
    kQuoteItem = 18,
    kListItem = 19,
    kTaskItem = 20
};

Boolean EditorInit(void);
void EditorShutdown(void);
Editor *EditorNew(WindowRef window, const Rect *view, OSStatus *status);
void EditorDelete(Editor *editor);
void EditorDraw(Editor *editor);
void EditorResize(Editor *editor, const Rect *view, const Rect *outline);
void EditorActivate(Editor *editor, Boolean active);
void EditorClick(Editor *editor, const EventRecord *event);
Boolean EditorContextMenu(Editor *editor, Point point, Str255 find);
Boolean EditorFindSelection(const Editor *editor, Str255 query);
void EditorKey(Editor *editor, const EventRecord *event);
void EditorIdle(Editor *editor);
Boolean EditorCanUndo(Editor *editor);
Boolean EditorCanRedo(Editor *editor);
void EditorCommand(Editor *editor, short item);
unsigned EditorSearch(Editor *editor, ConstStr255Param text, short direction, unsigned *ordinal);
unsigned EditorReplaceText(Editor *editor, ConstStr255Param find,
                           ConstStr255Param replacement, Boolean all);

typedef struct {
    RGBColor page;
    RGBColor text;
    RGBColor marker;
    RGBColor secondary;
    RGBColor caret;
    RGBColor panel;
    RGBColor divider;
} EditorTheme;

void EditorScrollLines(Editor *editor, short lines);
void EditorSetTheme(Editor *editor, const EditorTheme *theme);
void EditorSetMarkdown(Editor *editor, Boolean markdown);
size_t EditorWordCount(Editor *editor);
size_t EditorCharacterCount(Editor *editor);
void EditorSetTypewriter(Editor *editor, Boolean enabled);
unsigned EditorOutlineCount(const Editor *editor);
Boolean EditorOutlineReady(const Editor *editor);
unsigned EditorOutlinePage(Editor *editor, unsigned rows, short direction);
unsigned EditorOutlineCurrent(const Editor *editor);
Boolean EditorOutlineLabel(Editor *editor, unsigned index, Str255 label, unsigned *level);
void EditorJumpToHeading(Editor *editor, unsigned index);
Boolean EditorIsMarkdown(Editor *editor);
typedef struct {
    short family;
    short points;
    signed char lineSpacing; /* Pixel adjustment to the font's line height. */
} EditorTypography;

EditorTypography EditorDefaultTypography(void);
Boolean EditorFontAvailable(short family);
Boolean EditorSetTypography(Editor *editor, EditorTypography typography);

/* The editor owns text after a successful call. */
OSStatus EditorTakeText(Editor *editor, Handle text);
OSStatus EditorTakeDocument(Editor *editor, Handle text, Boolean markdown);
OSStatus EditorEncodeText(Editor *editor, Handle *bytes);

#ifdef LINEWREN_SMOKE_TEST
Boolean EditorAllSelected(Editor *editor);
Boolean EditorIsEmpty(Editor *editor);
Boolean EditorSelectionAt(Editor *editor, unsigned anchor, unsigned caret);
unsigned EditorTopOffset(Editor *editor);
Boolean EditorTypographyMatches(Editor *editor, EditorTypography typography);
const char *EditorSmokeTest(Editor *editor, char *timing, unsigned timingSize);
#endif

#endif
