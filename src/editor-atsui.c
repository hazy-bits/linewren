#include "mac-toolbox.h"
#include "editor.h"
#include "document.h"
#include "text-files.h"
#include "markdown-presentation.h"
#include "workspace.h"

#include <ATSUnicode.h>
#include <Controls.h>
#include <ControlDefinitions.h>
#include <Fonts.h>
#if TARGET_API_MAC_CARBON
#include <Gestalt.h>
#endif
#include <InternetConfig.h>
#include <Memory.h>
#include <OSUtils.h>
#include <QDOffscreen.h>
#include <Scrap.h>
#include <Sound.h>
#include <UnicodeConverter.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    kDefaultLineSpacing = -2, kCaretDescent = 2, kScrollbarMax = 1000,
    kLayoutSpan = 1024, kMaxSplits = 2 * kTextFileLimit / kLayoutSpan,
    kStyleDelay = 8, kUndoLimit = 12, kUndoBytes = 256 * 1024, kScrollbarGutter = 6,
    kIdleWrapBudget = 4
};

typedef struct {
    unsigned start;
    unsigned line;
} Position;

typedef struct {
    ATSUTextLayout layout;
    unsigned start, length, count, heading;
    uint16_t breaks[kLayoutSpan];
} LayoutSpan;

typedef struct {
    short baseline, height;
} RowMetrics;

typedef struct {
    Handle removed;
    unsigned start, inserted, anchor, caret;
    Boolean trailing;
} UndoEdit;

typedef struct {
    UndoEdit edits[kUndoLimit];
    unsigned count, bytes;
} EditHistory;

struct Editor {
    Workspace *workspace;
    Handle source;
    unsigned length, utf8Length, styleCapacity;
    uint16_t *styles;
    unsigned anchor, caret;
    Boolean trailing, active, markdown, stylePending, typewriter, trackingSelection;
    unsigned long styleAt;
    unsigned long clickAt;
    Point clickPoint;
    unsigned clickCount;
    EditHistory undo, redo;
    Boolean typing;
    unsigned long typedAt;
    ATSUStyle runs[kMarkdownDisplayMask + 1];
    const EditorTheme *theme;
    EditorTypography typography;
    ATSUFontID font;
    RowMetrics metrics[7];
    Rect view;
    Rect outline;
    MarkdownHeading *headings;
    unsigned headingCount, outlineFirst;
    RgnHandle clip, highlight;
    GWorldPtr buffer;
    ControlRef scrollbar;
    Position top, lastPage;
    unsigned lastPageOffset;
    Boolean lastPageKnown, splitsKnown;
    unsigned splitCount, splitScanStart, splitLineEnd;
    unsigned splits[kMaxSplits];
    Boolean reusableTail;
    unsigned reuseAfter, reuseCount, reuseIndex;
    unsigned reuseSplits[kMaxSplits];
    LayoutSpan paragraph;
    OSStatus layoutError;
};

#ifdef LINEWREN_SMOKE_TEST
static long gStyleFreeHeap;
static Boolean gCaptureWebURL;
static UniChar gCapturedWebURL[2048];
static unsigned gCapturedWebLength;
#endif

static short gDefaultFamily;
static ATSUFontID gDefaultFont;
static TextToUnicodeInfo gFromRoman;
static UnicodeToTextInfo gToRoman;
static void RefreshStyles(Editor *editor, Boolean immediate);
static void ShowCaret(Editor *editor);
static Boolean ClickMarkdown(Editor *editor, Point point, Boolean command);

/* Keep the column limit with the writing-surface defaults for future themes. */
enum { kMaximumTextWidth = 600 };

Boolean EditorInit(void)
{
    short family;
    GetFNum("\pVerdana", &family);
    if(family == 0)
        GetFNum("\pGeneva", &family);
    gDefaultFamily = family;
    if(ATSUFONDtoFontID(gDefaultFamily, normal, &gDefaultFont) == noErr &&
       CreateTextToUnicodeInfoByEncoding(kTextEncodingMacRoman, &gFromRoman) == noErr &&
       CreateUnicodeToTextInfoByEncoding(kTextEncodingMacRoman, &gToRoman) == noErr)
        return true;
    EditorShutdown();
    return false;
}

void EditorShutdown(void)
{
    if(gFromRoman != NULL)
        DisposeTextToUnicodeInfo(&gFromRoman);
    if(gToRoman != NULL)
        DisposeUnicodeToTextInfo(&gToRoman);
}

static void DisposeLayout(LayoutSpan *paragraph)
{
    if(paragraph->layout != NULL)
        ATSUDisposeTextLayout(paragraph->layout);
    paragraph->layout = NULL;
    paragraph->start = (unsigned)-1;
    paragraph->count = 0;
}

static void DropStyles(Editor *editor)
{
    DisposeLayout(&editor->paragraph);
    for(unsigned i = 0; i <= kMarkdownDisplayMask; ++i) {
        if(editor->runs[i] != NULL) {
            ATSUDisposeStyle(editor->runs[i]);
            editor->runs[i] = NULL;
        }
    }
}

static void ClearHistory(EditHistory *history)
{
    while(history->count != 0)
        DisposeHandle(history->edits[--history->count].removed);
    history->bytes = 0;
}

static void ClearUndo(Editor *editor)
{
    ClearHistory(&editor->undo);
    ClearHistory(&editor->redo);
    editor->typing = false;
}

static void PushHistory(EditHistory *history, UndoEdit edit)
{
    unsigned bytes = GetHandleSize(edit.removed);
    while(history->count != 0 &&
          (history->count == kUndoLimit || history->bytes + bytes > kUndoBytes)) {
        history->bytes -= GetHandleSize(history->edits[0].removed);
        DisposeHandle(history->edits[0].removed);
        --history->count;
        memmove(history->edits, history->edits + 1, history->count * sizeof(UndoEdit));
    }
    history->edits[history->count++] = edit;
    history->bytes += bytes;
}

static void ResetSplits(Editor *editor)
{
    editor->splitCount = editor->splitScanStart = editor->splitLineEnd = 0;
    editor->splitsKnown = editor->reusableTail = false;
}

static const UniChar *Source(const Editor *editor)
{
    return editor->source != NULL ? (const UniChar *)*editor->source : NULL;
}

EditorTypography EditorDefaultTypography(void)
{
    return (EditorTypography){gDefaultFamily, kBodyPoints, kDefaultLineSpacing};
}

Boolean EditorFontAvailable(short family)
{
    ATSUFontID font;
    return ATSUFONDtoFontID(family, normal, &font) == noErr;
}

static void MeasureTypography(Editor *editor)
{
    GrafPtr port;
    GetPort(&port);
#if TARGET_API_MAC_CARBON
    short font = GetPortTextFont((CGrafPtr)port), size = GetPortTextSize((CGrafPtr)port);
    Style face = GetPortTextFace((CGrafPtr)port);
#else
    short font = port->txFont, size = port->txSize;
    Style face = port->txFace;
#endif
    TextFont(editor->typography.family);
    TextFace(bold | italic);
    for(unsigned heading = 0; heading < 7; ++heading) {
        short points = MarkdownPointSize(heading, editor->typography.points);
        TextSize(points);
        FontInfo fontInfo;
        GetFontInfo(&fontInfo);
        short ascent = fontInfo.ascent > points ? fontInfo.ascent : points;
        short descent = fontInfo.descent > kCaretDescent ? fontInfo.descent : kCaretDescent;
        short height = ascent + descent + 3 + editor->typography.lineSpacing;
        short minimum = ascent + descent + 1;
        editor->metrics[heading] = (RowMetrics){ascent + 1, height < minimum ? minimum : height};
    }
    TextFont(font);
    TextSize(size);
    TextFace(face);
}

static Rect ScrollbarBounds(const Rect *view)
{
    Rect bounds = *view;
    bounds.left = bounds.right - kEditorScrollbarWidth;
    return bounds;
}

static Rect TextBounds(const Editor *editor)
{
    Rect text = editor->view;
    text.right -= kEditorScrollbarWidth;
    if(!EmptyRect(&editor->outline))
        text.right = editor->outline.left;
    text.right -= kScrollbarGutter;
    short extra = text.right - text.left - kMaximumTextWidth;
    if(extra > 0) {
        text.left += extra / 2;
        text.right = text.left + kMaximumTextWidth;
    }
    return text;
}

Editor *EditorNew(WindowRef window, const Rect *view, OSStatus *status)
{
    Editor *editor = (Editor *)NewPtrClear(sizeof(Editor));
    if(editor == NULL) {
        *status = memFullErr;
        return NULL;
    }
    editor->view = *view;
    editor->markdown = true;
    editor->typography = EditorDefaultTypography();
    editor->font = gDefaultFont;
    MeasureTypography(editor);
    editor->paragraph.start = (unsigned)-1;
    editor->workspace = WorkspaceNew();
    editor->clip = NewRgn();
    editor->highlight = NewRgn();
    Rect bounds = ScrollbarBounds(view);
    editor->scrollbar = NewControl(window, &bounds, "\p", false, 0, 0,
                                   kScrollbarMax, scrollBarProc, 0);
    if(editor->workspace == NULL || editor->clip == NULL ||
       editor->highlight == NULL || editor->scrollbar == NULL) {
        EditorDelete(editor);
        *status = memFullErr;
        return NULL;
    }
    *status = noErr;
    return editor;
}

void EditorDelete(Editor *editor)
{
    DropStyles(editor);
    if(editor->source != NULL)
        DisposeHandle(editor->source);
    free(editor->styles);
    free(editor->headings);
    ClearUndo(editor);
    if(editor->scrollbar != NULL)
        DisposeControl(editor->scrollbar);
    if(editor->clip != NULL)
        DisposeRgn(editor->clip);
    if(editor->highlight != NULL)
        DisposeRgn(editor->highlight);
    if(editor->buffer != NULL)
        DisposeGWorld(editor->buffer);
    if(editor->workspace != NULL)
        WorkspaceDelete(editor->workspace);
    DisposePtr((Ptr)editor);
}

Workspace *EditorWorkspace(Editor *editor) { return editor->workspace; }

void EditorSetTheme(Editor *editor, const EditorTheme *theme)
{
    DropStyles(editor);
    editor->theme = theme;
    editor->layoutError = noErr;
}

/* Return a value beyond the file limit for malformed UTF-16. */
static unsigned UTF8Length(const UniChar *source, unsigned length)
{
    unsigned bytes = 0;
    for(unsigned i = 0; i < length; ++i) {
        UniChar ch = source[i];
        if(ch >= 0xd800 && ch <= 0xdbff && i + 1 < length &&
           (source[i + 1] & 0xfc00) == 0xdc00) {
            ++i;
            bytes += 4;
        } else if(ch == 0 || (ch & 0xf800) == 0xd800) {
            return kTextFileLimit + 1;
        } else {
            bytes += ch < 0x80 ? 1 : ch < 0x800 ? 2 : 3;
        }
    }
    return bytes;
}

OSStatus EditorEncodeText(Editor *editor, Handle *bytes)
{
    return EncodeTextUTF8(Source(editor), editor->length, bytes);
}

static uint16_t *DocumentStyles(Editor *editor, const UniChar *text, unsigned length,
                                Boolean markdown, MarkdownHeading **headings, unsigned *count)
{
    *headings = NULL;
    *count = 0;
    if(length == 0)
        return NULL;
    if(!markdown)
        return calloc(length, sizeof(*editor->styles));
    return EmptyRect(&editor->outline) ? MarkdownStyles(text, length)
                                      : MarkdownStylesWithOutline(text, length, headings, count);
}

static void ReplaceOutline(Editor *editor, MarkdownHeading *headings, unsigned count)
{
    Boolean hadHeadings = editor->headingCount != 0;
    free(editor->headings);
    editor->headings = headings;
    editor->headingCount = count;
    if((hadHeadings || count != 0) && !EmptyRect(&editor->outline))
        InvalidateRect(&editor->outline);
}

OSStatus EditorTakeDocument(Editor *editor, Handle text, Boolean markdown)
{
    HLock(text);
    unsigned length = GetHandleSize(text) / sizeof(UniChar);
    MarkdownHeading *headings;
    unsigned count;
    uint16_t *styles = DocumentStyles(editor, (const UniChar *)*text, length, markdown,
                                      &headings, &count);
    if(length != 0 && styles == NULL) {
        HUnlock(text);
        return memFullErr;
    }
    DisposeLayout(&editor->paragraph);
    if(editor->source != NULL)
        DisposeHandle(editor->source);
    free(editor->styles);
    editor->source = text;
    editor->markdown = markdown;
    editor->styles = styles;
    ReplaceOutline(editor, headings, count);
    editor->outlineFirst = 0;
    editor->styleCapacity = length;
    editor->length = length;
    editor->utf8Length = UTF8Length(Source(editor), length);
    editor->anchor = editor->caret = 0;
    editor->clickCount = 0;
    editor->trailing = false;
    editor->stylePending = false;
    ClearUndo(editor);
    editor->top = (Position){0, 0};
    editor->lastPageOffset = 0;
    editor->lastPageKnown = false;
    ResetSplits(editor);
    editor->layoutError = noErr;
    InvalidateRect(&editor->view);
    return noErr;
}

OSStatus EditorTakeText(Editor *editor, Handle text)
{
    return EditorTakeDocument(editor, text, editor->markdown);
}

void EditorSetMarkdown(Editor *editor, Boolean markdown)
{
    if(editor->markdown == markdown)
        return;
    editor->markdown = markdown;
    editor->stylePending = true;
    editor->styleAt = 0;
    RefreshStyles(editor, true);
}

size_t EditorWordCount(Editor *editor)
{
    return editor->markdown
        ? DocumentWordCountStyled(Source(editor), editor->styles, editor->length, kMarkdownMarker)
        : DocumentWordCount(Source(editor), editor->length);
}

size_t EditorCharacterCount(Editor *editor)
{
    return DocumentCharacterCount(Source(editor), editor->length);
}

Boolean EditorIsMarkdown(Editor *editor) { return editor->markdown; }

static OSStatus GetStyle(Editor *editor, unsigned flags, ATSUStyle *out)
{
    ATSUStyle *style = &editor->runs[flags];
    if(*style == NULL) {
        OSStatus status = ATSUCreateStyle(style);
        if(status != noErr)
            return status;

        MarkdownPresentation display = MarkdownDisplay(flags, editor->theme, editor->typography.points);
        Fixed size = (Fixed)display.points << 16;
        Boolean isBold = (display.face & bold) != 0;
        Boolean isItalic = (display.face & italic) != 0;
        Boolean isUnderline = (display.face & underline) != 0;
        Boolean isCondensed = (display.face & condense) != 0;
        ATSUAttributeTag tags[] = {kATSUFontTag, kATSUSizeTag, kATSUColorTag,
                                   kATSUQDBoldfaceTag, kATSUQDItalicTag,
                                   kATSUQDUnderlineTag, kATSUQDCondensedTag};
        ByteCount sizes[] = {sizeof(editor->font), sizeof(size), sizeof(display.color),
                             sizeof(isBold), sizeof(isItalic), sizeof(isUnderline),
                             sizeof(isCondensed)};
        ATSUAttributeValuePtr values[] = {&editor->font, &size, &display.color, &isBold, &isItalic,
                                           &isUnderline, &isCondensed};
        status = ATSUSetAttributes(*style, sizeof(tags) / sizeof(tags[0]), tags, sizes, values);
        if(status != noErr) {
            ATSUDisposeStyle(*style);
            *style = NULL;
            return status;
        }
    }
    *out = *style;
    return noErr;
}

static OSStatus MakeLayout(Editor *editor, LayoutSpan *paragraph)
{
    UniCharCount length = paragraph->length;
    const UniChar *source = Source(editor) + paragraph->start;
    const uint16_t *styles = editor->styles + paragraph->start;
    ATSUStyle style;
    OSStatus status = GetStyle(editor, styles[0] & kMarkdownDisplayMask, &style);
    if(status != noErr)
        return status;
    status = ATSUCreateTextLayoutWithTextPtr(source, 0, length, length, 1, &length,
                                            &style, &paragraph->layout);
    if(status != noErr)
        return status;
    ATSUAttributeTag tag = kATSULineLayoutOptionsTag;
    ATSLineLayoutOptions options = kATSLineFractDisable;
    ByteCount size = sizeof(options);
    ATSUAttributeValuePtr value = &options;
    status = ATSUSetLayoutControls(paragraph->layout, 1, &tag, &size, &value);
    if(status == noErr)
        status = ATSUSetTransientFontMatching(paragraph->layout, true);
    for(unsigned start = 0, end; status == noErr && start < length; start = end) {
        unsigned flags = styles[start] & kMarkdownDisplayMask;
        for(end = start + 1; end < length && (styles[end] & kMarkdownDisplayMask) == flags; ++end)
            ;
        if(start != 0) {
            status = GetStyle(editor, flags, &style);
            if(status == noErr)
                status = ATSUSetRunStyle(paragraph->layout, style, start, end - start);
        }
    }
    return status;
}

static OSStatus Wrap(Editor *editor, LayoutSpan *paragraph)
{
    OSStatus status = MakeLayout(editor, paragraph);
    Rect text = TextBounds(editor);
    Fixed width = (Fixed)(text.right - text.left - 8) << 16;
    for(UniCharArrayOffset offset = 0; status == noErr && offset < paragraph->length;) {
        UniCharArrayOffset next = paragraph->length;
        status = ATSUBreakLine(paragraph->layout, offset, width, true, &next);
        if(status == kATSULineBreakInWord)
            status = noErr;
        if(status == noErr && (next <= offset || next > paragraph->length))
            status = paramErr;
        if(status == noErr)
            paragraph->breaks[paragraph->count++] = next;
        offset = next;
    }
    return status;
}

/* Every span starts its own ATSUI layout. At an aligned checkpoint (or
   unchanged hard break), the following spans have identical inputs. */
static Boolean ReuseTail(Editor *editor, unsigned start, Boolean hardBreak)
{
    if(!editor->reusableTail || start <= editor->reuseAfter)
        return false;
    unsigned i = editor->reuseIndex;
    while(i < editor->reuseCount && editor->reuseSplits[i] < start)
        ++i;
    editor->reuseIndex = i;
    Boolean matches = i < editor->reuseCount && editor->reuseSplits[i] == start;
    if(!hardBreak && !matches)
        return false;
    i += matches;
    unsigned count = editor->reuseCount - i;
    memcpy(editor->splits + editor->splitCount, editor->reuseSplits + i,
           count * sizeof(*editor->splits));
    editor->splitCount += count;
    editor->splitScanStart = editor->length;
    return true;
}

/* Index only the prefix needed by the caller. Leave the final trial line,
   which may be truncated, for the next span. Idle calls cap the ATSUI work. */
static OSStatus IndexThrough(Editor *editor, unsigned offset, unsigned budget)
{
    if(editor->splitsKnown)
        return noErr;
    const UniChar *source = Source(editor);
    LayoutSpan span = {0};
    while(editor->splitScanStart <= offset) {
        unsigned start = editor->splitScanStart;
        if(start >= editor->length) {
            editor->splitsKnown = true;
            break;
        }
        unsigned end = editor->splitLineEnd;
        if(end <= start) {
            for(end = start; end < editor->length && source[end] != '\r'; ++end)
                ;
            editor->splitLineEnd = end;
        }
        if(end - start <= kLayoutSpan) {
            editor->splitScanStart = end == editor->length ? end : end + 1;
            if(end < editor->length && ReuseTail(editor, end + 1, true)) {
                editor->splitsKnown = true;
                break;
            }
            continue;
        }
        if(budget == 0)
            break;
        --budget;
        unsigned length = kLayoutSpan;
        if((source[start + length] & 0xfc00) == 0xdc00)
            --length;
        span.start = start;
        span.length = length;
        OSStatus status = Wrap(editor, &span);
        if(status == noErr && span.count > 1 &&
           span.breaks[span.count - 2] >= kLayoutSpan / 2)
            length = span.breaks[span.count - 2];
        DisposeLayout(&span);
        if(status != noErr)
            return status;
        start += length;
        editor->splits[editor->splitCount++] = start;
        editor->splitScanStart = start;
        if(ReuseTail(editor, start, false)) {
            editor->splitsKnown = true;
            break;
        }
    }
    if(editor->splitScanStart >= editor->length)
        editor->splitsKnown = true;
    return noErr;
}

/* Index of the first split after offset, or splitCount at the end. */
static unsigned NextSplit(Editor *editor, unsigned offset)
{
    unsigned first = 0, end = editor->splitCount;
    while(first < end) {
        unsigned middle = first + (end - first) / 2;
        if(editor->splits[middle] <= offset)
            first = middle + 1;
        else
            end = middle;
    }
    return first;
}

static LayoutSpan *Layout(Editor *editor, unsigned start)
{
    LayoutSpan *paragraph = &editor->paragraph;
    if(paragraph->start == start)
        return paragraph;
    DisposeLayout(paragraph);
    OSStatus status = IndexThrough(editor, start, kMaxSplits);
    if(status != noErr) {
        editor->layoutError = status;
        return NULL;
    }
    unsigned split = NextSplit(editor, start);
    unsigned limit = split < editor->splitCount ? editor->splits[split] : editor->length;
    const UniChar *source = Source(editor);
    unsigned end = start;
    while(end < limit && source[end] != '\r')
        ++end;
    paragraph->start = start;
    paragraph->length = end - start;
    paragraph->heading = 0;
    for(unsigned i = start; i < end; ++i) {
        unsigned level = editor->styles[i] & kMarkdownHeadingMask;
        if(level > 0 && level < 5 && (paragraph->heading == 0 || level < paragraph->heading))
            paragraph->heading = level;
    }
    if(paragraph->length == 0) {
        paragraph->count = 1;
    } else {
        status = Wrap(editor, paragraph);
        if(status != noErr) {
            editor->layoutError = status;
            DisposeLayout(paragraph);
            return NULL;
        }
    }
    return paragraph;
}

static unsigned LineOffset(const LayoutSpan *paragraph, unsigned line)
{
    return line != 0 ? paragraph->breaks[line - 1] : 0;
}

static unsigned SourceOffset(Editor *editor, Position position)
{
    if(position.line == 0)
        return position.start;
    LayoutSpan *paragraph = Layout(editor, position.start);
    return position.start + (paragraph != NULL ? LineOffset(paragraph, position.line) : 0);
}

static unsigned SpanStart(Editor *editor, unsigned offset)
{
    unsigned split = NextSplit(editor, offset);
    unsigned limit = split != 0 ? editor->splits[split - 1] : 0;
    const UniChar *source = Source(editor);
    while(offset > limit && source[offset - 1] != '\r')
        --offset;
    return offset;
}

/* A trial layout can inspect up to one span past its starting offset.
   Keep only checkpoints whose lookahead cannot have touched the change. */
static void ReindexAfter(Editor *editor, unsigned offset, unsigned oldEnd, long delta)
{
    unsigned rewind = offset > kLayoutSpan ? offset - kLayoutSpan : 0;
    if(editor->splitsKnown) {
        unsigned first = NextSplit(editor, oldEnd);
        editor->reuseCount = editor->splitCount - first;
        editor->reuseIndex = 0;
        editor->reuseAfter = (long)oldEnd + delta;
        editor->reusableTail = true;
        for(unsigned i = 0; i < editor->reuseCount; ++i)
            editor->reuseSplits[i] = (long)editor->splits[first + i] + delta;
    } else if(editor->reusableTail) {
        unsigned unchanged = oldEnd > editor->reuseAfter ? oldEnd : editor->reuseAfter;
        unsigned count = 0;
        for(unsigned i = 0; i < editor->reuseCount; ++i) {
            if(editor->reuseSplits[i] > unchanged)
                editor->reuseSplits[count++] = (long)editor->reuseSplits[i] + delta;
        }
        editor->reuseCount = count;
        editor->reuseIndex = 0;
        editor->reuseAfter = (long)unchanged + delta;
    }
    if(editor->splitsKnown || rewind < editor->splitScanStart) {
        editor->splitScanStart = SpanStart(editor, rewind);
        editor->splitCount = NextSplit(editor, editor->splitScanStart);
    }
    editor->splitLineEnd = editor->splitScanStart;
    editor->splitsKnown = false;
}

static Position PositionAt(Editor *editor, unsigned offset)
{
    OSStatus status = IndexThrough(editor, offset, kMaxSplits);
    if(status != noErr) {
        editor->layoutError = status;
        return (Position){0, 0};
    }
    Position position = {SpanStart(editor, offset), 0};
    LayoutSpan *paragraph = Layout(editor, position.start);
    if(paragraph != NULL && paragraph->length != 0) {
        while(position.line + 1 < paragraph->count &&
              position.start + paragraph->breaks[position.line] <= offset)
            ++position.line;
    }
    return position;
}

static Boolean Step(Editor *editor, Position *position, Boolean forward)
{
    LayoutSpan *paragraph = Layout(editor, position->start);
    if(paragraph == NULL)
        return false;
    if(forward) {
        if(position->line + 1 < paragraph->count) {
            ++position->line;
        } else if(position->start + paragraph->length < editor->length) {
            unsigned end = position->start + paragraph->length;
            *position = (Position){end + (Source(editor)[end] == '\r'), 0};
        } else {
            return false;
        }
    } else if(position->line != 0) {
        --position->line;
    } else if(position->start != 0) {
        unsigned start = SpanStart(editor, position->start - 1);
        paragraph = Layout(editor, start);
        if(paragraph == NULL)
            return false;
        *position = (Position){start, paragraph->count - 1};
    } else {
        return false;
    }
    return true;
}

static Boolean Before(Position a, Position b)
{
    return a.start < b.start || (a.start == b.start && a.line < b.line);
}

static Position CaretRow(Editor *editor)
{
    Position row = PositionAt(editor, editor->caret);
    if(editor->trailing && SourceOffset(editor, row) == editor->caret) {
        Position previous = row;
        if(Step(editor, &previous, false)) {
            LayoutSpan *span = Layout(editor, previous.start);
            if(span != NULL && span->length != 0 &&
               previous.start + span->breaks[previous.line] == editor->caret)
                return previous;
        }
    }
    return row;
}

static Fixed CaretX(const LayoutSpan *span, unsigned line, unsigned offset)
{
    if(span->length == 0)
        return 0;
    unsigned end = span->breaks[line];
    ATSUCaret caret, other;
    Boolean split;
    /* At a line end, pass the insertion offset and ask for its trailing edge. */
    if(ATSUOffsetToPosition(span->layout, offset, offset < end, &caret, &other, &split) == noErr)
        return caret.fX;
    return 0;
}

static unsigned HitLine(Editor *editor, Position row, Fixed x, Boolean *trailing)
{
    *trailing = false;
    LayoutSpan *span = Layout(editor, row.start);
    if(span == NULL || span->length == 0)
        return row.start;
    unsigned start = LineOffset(span, row.line), end = span->breaks[row.line];
    UniCharArrayOffset offset = start, other;
    Boolean leading;
    /* ATSUI already returns the insertion offset, including on trailing hits. */
    if(ATSUPositionToOffset(span->layout, x, 0, &offset, &leading, &other) != noErr)
        return row.start + start;
    if(offset < start)
        offset = start;
    if(offset >= end) {
        offset = end;
        *trailing = true;
    }
    return row.start + offset;
}

static RowMetrics MetricsAt(Editor *editor, Position row)
{
    LayoutSpan *span = Layout(editor, row.start);
    return editor->metrics[span != NULL ? span->heading : 0];
}

/* Keep the starting row visible even if the window is shorter than that row. */
static short PageEdge(Editor *editor, Position *row, Boolean forward, const Position *limit)
{
    short available = editor->view.bottom - editor->view.top - MetricsAt(editor, *row).height;
    short steps = 0;
    Position next = *row;
    while(available > 0 && Step(editor, &next, forward)) {
        available -= MetricsAt(editor, next).height;
        if(available < 0)
            break;
        *row = next;
        ++steps;
        if(limit != NULL && !Before(next, *limit))
            break;
    }
    return steps;
}

static short PageSteps(Editor *editor)
{
    Position row = editor->top;
    short steps = PageEdge(editor, &row, true, NULL);
    return steps > 0 ? steps : 1;
}

static Position MoveLines(Editor *editor, Position position, short lines)
{
    for(; lines > 0 && Step(editor, &position, true); --lines)
        ;
    for(; lines < 0 && Step(editor, &position, false); ++lines)
        ;
    return position;
}

static Position BottomPage(Editor *editor)
{
    if(!editor->lastPageKnown) {
        editor->lastPage = PositionAt(editor, editor->length);
        PageEdge(editor, &editor->lastPage, false, NULL);
        editor->lastPageOffset = SourceOffset(editor, editor->lastPage);
        editor->lastPageKnown = editor->layoutError == noErr;
    }
    return editor->lastPage;
}

static unsigned ScrollMaximum(const Editor *editor)
{
    unsigned maximum = editor->lastPageOffset;
    if(maximum > editor->length || (!editor->lastPageKnown && maximum == 0))
        maximum = editor->length;
    return maximum;
}

static void InvalidateWritingSurface(const Editor *editor)
{
    Rect writing = editor->view;
    if(!EmptyRect(&editor->outline))
        writing.right = editor->outline.left;
    InvalidateRect(&writing);
    Rect bar = ScrollbarBounds(&editor->view);
    InvalidateRect(&bar);
}

static void ScrollTo(Editor *editor, Position position)
{
    if(editor->lastPageKnown && Before(editor->lastPage, position))
        position = editor->lastPage;
    if(position.start != editor->top.start || position.line != editor->top.line) {
        editor->top = position;
        InvalidateWritingSurface(editor);
    }
}

Boolean EditorSetTypography(Editor *editor, EditorTypography typography)
{
    if(editor->typography.family == typography.family &&
       editor->typography.points == typography.points &&
       editor->typography.lineSpacing == typography.lineSpacing)
        return true;
    ATSUFontID font;
    if(ATSUFONDtoFontID(typography.family, normal, &font) != noErr)
        return false;
    unsigned offset = SourceOffset(editor, editor->top);
    DropStyles(editor);
    editor->typography = typography;
    editor->font = font;
    MeasureTypography(editor);
    editor->lastPageKnown = false;
    ResetSplits(editor);
    editor->layoutError = noErr;
    ScrollTo(editor, PositionAt(editor, offset));
    InvalidateRect(&editor->view);
    return true;
}

void EditorScrollLines(Editor *editor, short lines)
{
    ScrollTo(editor, MoveLines(editor, editor->top, lines));
}

static void PaintSelection(Editor *editor, LayoutSpan *span, Position row, Fixed x, short baseline)
{
    unsigned start = LineOffset(span, row.line);
    unsigned end = span->length != 0 ? span->breaks[row.line] : start;
    if(editor->anchor != editor->caret) {
        unsigned low = editor->anchor < editor->caret ? editor->anchor : editor->caret;
        unsigned high = editor->anchor > editor->caret ? editor->anchor : editor->caret;
        if(low < row.start + end && high > row.start + start) {
            unsigned left = low > row.start + start ? low - row.start : start;
            unsigned right = high < row.start + end ? high - row.start : end;
            if(ATSUGetTextHighlight(span->layout, x, (Fixed)baseline << 16,
                                   left, right - left, editor->highlight) == noErr) {
                RGBColor highlight = editor->theme->page.red < 0x8000
                    ? (RGBColor){0x2d2d, 0x4545, 0x6262}
                    : (RGBColor){0xc5c5, 0xdcdc, 0xf5f5};
                RGBForeColor(&highlight);
                PaintRgn(editor->highlight);
            }
        }
    } else if(editor->caret >= row.start + start && editor->caret <= row.start + end) {
        const UniChar *source = Source(editor);
        unsigned first = row.start + start, last = row.start + end;
        /* A soft wrap has two visual positions for the same source offset. */
        if((editor->caret == first && editor->trailing && first != 0 && source[first - 1] != '\r') ||
           (editor->caret == last && !editor->trailing && last < editor->length && source[last] != '\r'))
            return;
        Fixed dx = CaretX(span, row.line, editor->caret - row.start);
        short h = (short)((x + dx + 0x8000) >> 16);
        Rect caret = {baseline - editor->typography.points, h, baseline + kCaretDescent + 1, h + 2};
        RGBForeColor(&editor->theme->caret);
        PaintRect(&caret);
    }
}

static Boolean PaintText(Editor *editor, const Rect *text)
{
    editor->layoutError = noErr;
    RGBBackColor(&editor->theme->page);
    EraseRect(text);
    Position position = editor->top;
    Boolean more = false;
    for(short top = text->top;;) {
        LayoutSpan *paragraph = Layout(editor, position.start);
        if(paragraph == NULL)
            break;
        RowMetrics metrics = editor->metrics[paragraph->heading];
        if(top != text->top && top + metrics.height > text->bottom)
            break;
        short baseline = top + metrics.baseline;
        Fixed x = (Fixed)text->left << 16;
        Boolean selection = editor->anchor != editor->caret;
        if(editor->active && selection)
            PaintSelection(editor, paragraph, position, x, baseline);
        if(paragraph->length != 0) {
            unsigned start = LineOffset(paragraph, position.line);
            unsigned length = paragraph->breaks[position.line] - start;
            OSStatus status = ATSUDrawText(paragraph->layout, start, length, x, (Fixed)baseline << 16);
            if(status != noErr) {
                editor->layoutError = status;
                break;
            }
        }
        if(editor->active && !selection)
            PaintSelection(editor, paragraph, position, x, baseline);
        more = Step(editor, &position, true);
        if(!more)
            break;
        top += metrics.height;
    }
    if(editor->layoutError != noErr) {
        RGBForeColor(&editor->theme->text);
        TextFont(kFontIDGeneva);
        TextFace(normal);
        TextSize(kBodyPoints);
        MoveTo(text->left, text->top + editor->metrics[0].baseline);
        DrawString("\pUnable to lay out this text.");
    }
    return more;
}

void EditorDraw(Editor *editor)
{
    Rect text = TextBounds(editor);
    Rect canvas = text;
    OffsetRect(&canvas, -text.left, -text.top);
    if(editor->buffer == NULL)
        NewGWorld(&editor->buffer, 0, &canvas, NULL, NULL, useTempMem);
    PixMapHandle pixels = editor->buffer != NULL ? GetGWorldPixMap(editor->buffer) : NULL;
    Boolean more;
    if(pixels != NULL && LockPixels(pixels)) {
        CGrafPtr port;
        GDHandle device;
        GetGWorld(&port, &device);
        SetGWorld(editor->buffer, NULL);
        more = PaintText(editor, &canvas);
        SetGWorld(port, device);
        RGBColor foreground;
        GetForeColor(&foreground);
        ForeColor(blackColor);
        BackColor(whiteColor);
#if TARGET_API_MAC_CARBON
        const BitMap *destination = GetPortBitMapForCopyBits(port);
#else
        const BitMap *destination = &((GrafPtr)port)->portBits;
#endif
        CopyBits((const BitMap *)*pixels, destination,
                 &canvas, &text, srcCopy, NULL);
        RGBForeColor(&foreground);
        UnlockPixels(pixels);
    } else {
        GetClip(editor->clip);
        RectRgn(editor->highlight, &text);
        SectRgn(editor->highlight, editor->clip, editor->highlight);
        SetClip(editor->highlight);
        more = PaintText(editor, &text);
        SetClip(editor->clip);
    }

    if(!more && editor->splitsKnown && editor->layoutError == noErr) {
        BottomPage(editor);
        ScrollTo(editor, editor->top);
    }
    RGBBackColor(&editor->theme->page);
    Rect leftGutter = {text.top, editor->view.left, text.bottom, text.left};
    short gutterRight = EmptyRect(&editor->outline) ? ScrollbarBounds(&editor->view).left
                                                   : editor->outline.left;
    Rect rightGutter = {text.top, text.right, text.bottom, gutterRight};
    EraseRect(&leftGutter);
    EraseRect(&rightGutter);
    if((more || editor->top.start != 0 || editor->top.line != 0) && editor->layoutError == noErr) {
        unsigned maximum = ScrollMaximum(editor);
        unsigned offset = SourceOffset(editor, editor->top);
        short value = maximum == 0 ? 0 : offset >= maximum ? kScrollbarMax
                                                         : offset * kScrollbarMax / maximum;
        if(value != GetControlValue(editor->scrollbar))
            SetControlValue(editor->scrollbar, value);
        if(!ControlVisible(editor->scrollbar))
            ShowControl(editor->scrollbar);
        else
            Draw1Control(editor->scrollbar);
    } else {
        if(ControlVisible(editor->scrollbar))
            HideControl(editor->scrollbar);
        Rect bar = ScrollbarBounds(&editor->view);
        EraseRect(&bar);
    }
}

#ifdef LINEWREN_FONT_SPECIMENS
OSStatus EditorSpecimenLayoutStatus(Editor *editor)
{
    return editor->layoutError;
}
#endif

void EditorResize(Editor *editor, const Rect *view, const Rect *outline)
{
    unsigned offset = SourceOffset(editor, editor->top);
    Boolean wasVisible = !EmptyRect(&editor->outline);
    DisposeLayout(&editor->paragraph);
    editor->lastPageKnown = false;
    ResetSplits(editor);
    editor->layoutError = noErr;
    editor->view = *view;
    editor->outline = outline != NULL ? *outline : (Rect){0};
    if(editor->buffer != NULL) {
        DisposeGWorld(editor->buffer);
        editor->buffer = NULL;
    }
    Rect bar = ScrollbarBounds(view);
    MoveControl(editor->scrollbar, bar.left, bar.top);
    SizeControl(editor->scrollbar, bar.right - bar.left, bar.bottom - bar.top);
    ScrollTo(editor, PositionAt(editor, offset));
    Boolean visible = !EmptyRect(&editor->outline);
    if(visible && !wasVisible) {
        editor->stylePending = true;
        RefreshStyles(editor, true);
    } else if(!visible && wasVisible)
        ReplaceOutline(editor, NULL, 0);
}

static void ClickScrollbar(Editor *editor, Point point)
{
    switch(TrackControl(editor->scrollbar, point, NULL)) {
        case kControlUpButtonPart: EditorScrollLines(editor, -1); break;
        case kControlDownButtonPart: EditorScrollLines(editor, 1); break;
        case kControlPageUpPart: EditorScrollLines(editor, -PageSteps(editor)); break;
        case kControlPageDownPart: EditorScrollLines(editor, PageSteps(editor)); break;
        case kControlIndicatorPart: {
            short value = GetControlValue(editor->scrollbar);
            unsigned maximum = ScrollMaximum(editor);
            ScrollTo(editor, value == kScrollbarMax ? BottomPage(editor)
                     : PositionAt(editor, maximum * value / kScrollbarMax));
            break;
        }
    }
}

static void ShowCaret(Editor *editor)
{
    Position row = CaretRow(editor);
    if(editor->typewriter && !editor->trackingSelection) {
        short remaining = (editor->view.bottom - editor->view.top - MetricsAt(editor, row).height) / 2;
        Position previous = row;
        while(Step(editor, &previous, false)) {
            remaining -= MetricsAt(editor, previous).height;
            if(remaining < 0)
                break;
            row = previous;
        }
        ScrollTo(editor, row);
    } else if(Before(row, editor->top)) {
        ScrollTo(editor, row);
    } else {
        Position visible = editor->top;
        if(Before(visible, row)) {
            PageEdge(editor, &visible, true, &row);
            if(Before(visible, row)) {
                PageEdge(editor, &row, false, NULL);
                ScrollTo(editor, row);
            }
        }
    }
    Rect text = TextBounds(editor);
    InvalidateRect(&text);
}

void EditorSetTypewriter(Editor *editor, Boolean enabled)
{
    editor->typewriter = enabled;
    if(enabled)
        ShowCaret(editor);
}

static void Select(Editor *editor, unsigned offset, Boolean trailing, Boolean extend)
{
    Boolean updateOutline = !editor->stylePending && editor->headingCount != 0;
    unsigned heading = updateOutline ? EditorOutlineCurrent(editor) : 0;
    editor->typing = false;
    if(!extend)
        editor->anchor = offset;
    editor->caret = offset;
    editor->trailing = trailing;
    ShowCaret(editor);
    if(updateOutline && heading != EditorOutlineCurrent(editor))
        InvalidateRect(&editor->outline);
}

unsigned EditorOutlineCount(const Editor *editor) { return editor->headingCount; }

Boolean EditorOutlineReady(const Editor *editor)
{
    return !editor->stylePending || editor->headingCount == 0;
}

unsigned EditorOutlinePage(Editor *editor, unsigned rows, short direction)
{
    unsigned last = editor->headingCount > rows ? editor->headingCount - rows : 0;
    unsigned first = editor->outlineFirst > last ? last : editor->outlineFirst;
    if(direction < 0)
        first = first > rows ? first - rows : 0;
    else if(direction > 0)
        first = first + rows < last ? first + rows : last;
    return editor->outlineFirst = first;
}

unsigned EditorOutlineCurrent(const Editor *editor)
{
    unsigned lo = 0, hi = editor->headingCount;
    while(lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        if(editor->headings[mid].start <= editor->caret)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo == 0 ? editor->headingCount : lo - 1;
}

Boolean EditorOutlineLabel(Editor *editor, unsigned index, Str255 label, unsigned *level)
{
    if(index >= editor->headingCount || editor->stylePending)
        return false;
    MarkdownHeading heading = editor->headings[index];
    *level = heading.level;
    UniChar text[128];
    unsigned length = 0;
    const UniChar *source = Source(editor);
    for(unsigned i = heading.start; i < heading.end && length < 128; ++i) {
        UniChar ch = source[i];
        if(ch == '\r' || ch == '\n' || ch == '\t')
            ch = ' ';
        if(ch == ' ' && (length == 0 || text[length - 1] == ' '))
            continue;
        if((editor->styles[i] & kMarkdownHeadingMask) &&
           !(editor->styles[i] & kMarkdownMarker))
            text[length++] = ch;
        else if(ch == ' ' && length != 0)
            text[length++] = ch;
    }
    while(length != 0 && text[length - 1] == ' ')
        --length;
    if(length != 0 && (text[length - 1] & 0xfc00) == 0xd800)
        --length;
    ByteCount read, written = 0;
    if(length != 0)
        ConvertFromUnicodeToText(gToRoman, length * sizeof(*text), text,
                                 kUnicodeUseFallbacksMask, 0, NULL, NULL, NULL,
                                 255, &read, &written, label + 1);
    label[0] = written;
    return written != 0;
}

void EditorJumpToHeading(Editor *editor, unsigned index)
{
    if(editor->stylePending)
        RefreshStyles(editor, true);
    if(index < editor->headingCount) {
        Select(editor, editor->headings[index].start, false, false);
        ScrollTo(editor, CaretRow(editor));
    }
}

static unsigned HitPoint(Editor *editor, Point point, Position *row, Boolean *trailing)
{
    short top = editor->view.top;
    *row = editor->top;
    short bottom = top + MetricsAt(editor, *row).height;
    while(point.v >= bottom) {
        Position next = *row;
        if(!Step(editor, &next, true)) {
            *trailing = false;
            row->start = editor->length;
            row->line = 0;
            return editor->length;
        }
        short nextBottom = bottom + MetricsAt(editor, next).height;
        if(nextBottom > editor->view.bottom)
            break;
        *row = next;
        bottom = nextBottom;
    }
    return HitLine(editor, *row, (Fixed)(point.h - TextBounds(editor).left) * 65536, trailing);
}

static unsigned CursorStepBy(Editor *editor, unsigned offset, Boolean forward,
                             ATSUCursorMovementType movement);

static void ClickRange(Editor *editor, Point point, unsigned clicks,
                       unsigned *start, unsigned *end, Boolean *trailing)
{
    Position row;
    *start = *end = HitPoint(editor, point, &row, trailing);
    if(clicks < 2)
        return;
    LayoutSpan *span = Layout(editor, row.start);
    if(span == NULL || span->length == 0) {
        if(clicks == 3 && *end < editor->length && Source(editor)[*end] == '\r')
            ++*end;
        return;
    }
    unsigned first = row.start + LineOffset(span, row.line);
    unsigned last = row.start + span->breaks[row.line];
    if(clicks == 3) {
        *start = first;
        *end = last + (last < editor->length && Source(editor)[last] == '\r');
        *trailing = *end == last;
        return;
    }
    Fixed x = (Fixed)(point.h - TextBounds(editor).left) * 65536;
    unsigned character = *start;
    if(character > first &&
       (character >= last || x < CaretX(span, row.line, character - row.start)))
        character = CursorStepBy(editor, character, false, kATSUByTypographicCluster);
    *end = CursorStepBy(editor, character, true, kATSUByWord);
    *start = CursorStepBy(editor, *end, false, kATSUByWord);
    *trailing = true;
}

void EditorClick(Editor *editor, const EventRecord *event)
{
    Point point = event->where;
    GlobalToLocal(&point);
    if(!PtInRect(point, &editor->view))
        return;
    if(ControlVisible(editor->scrollbar) && TestControl(editor->scrollbar, point) != 0) {
        editor->clickCount = 0;
        ClickScrollbar(editor, point);
        return;
    }
    if(editor->markdown && !(event->modifiers & (shiftKey | optionKey | controlKey)) &&
       ClickMarkdown(editor, point, (event->modifiers & cmdKey) != 0)) {
        editor->clickCount = 0;
        return;
    }
    Boolean extend = (event->modifiers & shiftKey) != 0;
    if(!extend && editor->clickCount < 3 && event->when - editor->clickAt <= GetDblTime() &&
       abs(point.h - editor->clickPoint.h) <= 4 && abs(point.v - editor->clickPoint.v) <= 4)
        ++editor->clickCount;
    else
        editor->clickCount = 1;
    editor->clickAt = event->when;
    editor->clickPoint = point;
    unsigned clicks = editor->clickCount;
    Boolean trailing;
    unsigned first, last;
    ClickRange(editor, point, clicks, &first, &last, &trailing);
    if(!extend)
        editor->anchor = first;
    editor->trackingSelection = true;
    Select(editor, last, trailing, true);

    while(StillDown()) {
        Point next;
        GetMouse(&next);
        short scroll = next.v < editor->view.top ? -1 : next.v >= editor->view.bottom ? 1 : 0;
        if(scroll != 0)
            EditorScrollLines(editor, scroll);
        if(scroll != 0 || !EqualPt(point, next)) {
            unsigned start, end, anchor = editor->anchor;
            ClickRange(editor, next, clicks, &start, &end, &trailing);
            if(abs(next.h - editor->clickPoint.h) > 4 || abs(next.v - editor->clickPoint.v) > 4)
                editor->clickCount = 0;
            if(clicks > 1) {
                anchor = start < first ? last : first;
                if(start < first) {
                    end = start;
                    trailing = false;
                }
            }
            Boolean changed = anchor != editor->anchor || end != editor->caret ||
                              trailing != editor->trailing;
            if(changed) {
                editor->anchor = anchor;
                Select(editor, end, trailing, true);
            }
            if(changed || scroll != 0) {
                EditorDraw(editor);
#if TARGET_API_MAC_CARBON
                GrafPtr port;
                GetPort(&port);
                QDFlushPortBuffer((CGrafPtr)port, NULL);
#endif
            }
            point = next;
        }
        unsigned long now;
        Delay(1, &now);
    }
    editor->trackingSelection = false;
}

static void SelectionRange(const Editor *editor, unsigned *start, unsigned *end)
{
    *start = editor->anchor < editor->caret ? editor->anchor : editor->caret;
    *end = editor->anchor > editor->caret ? editor->anchor : editor->caret;
}

static unsigned CursorStepBy(Editor *editor, unsigned offset, Boolean forward,
                             ATSUCursorMovementType movement)
{
    const UniChar *text = Source(editor);
    if(forward ? offset == editor->length : offset == 0)
        return offset;
    unsigned adjacent = forward ? offset : offset - 1;
    if(text[adjacent] == '\r')
        return forward ? offset + 1 : offset - 1;
    LayoutSpan *span = Layout(editor, SpanStart(editor, adjacent));
    UniCharArrayOffset next;
    if(span != NULL) {
        OSStatus status = forward
            ? ATSUNextCursorPosition(span->layout, offset - span->start, movement, &next)
            : ATSUPreviousCursorPosition(span->layout, offset - span->start, movement, &next);
        if(status == noErr && next <= span->length && next + span->start != offset)
            return span->start + next;
    }
    if(forward)
        return offset + ((text[offset] & 0xfc00) == 0xd800 ? 2 : 1);
    return offset - ((text[offset - 1] & 0xfc00) == 0xdc00 ? 2 : 1);
}

static unsigned CursorStep(Editor *editor, unsigned offset, Boolean forward)
{
    return CursorStepBy(editor, offset, forward, kATSUByTypographicCluster);
}

static unsigned WordEdge(Editor *editor, unsigned offset, Boolean forward)
{
    const UniChar *text = Source(editor);
    if(forward) {
        while(offset < editor->length && DocumentIsSpace(text[offset]))
            ++offset;
        unsigned edge = CursorStepBy(editor, offset, true, kATSUByWord);
        while(edge > offset && DocumentIsSpace(text[edge - 1]))
            --edge;
        return edge;
    }
    while(offset != 0 && DocumentIsSpace(text[offset - 1]))
        --offset;
    unsigned edge = CursorStepBy(editor, offset, false, kATSUByWord);
    while(edge < offset && DocumentIsSpace(text[edge]))
        ++edge;
    return edge;
}

static Boolean Reserve(Editor *editor, unsigned needed)
{
    unsigned capacity = (needed + 1023) & ~1023u;
    if(needed > editor->styleCapacity) {
        uint16_t *styles = realloc(editor->styles, capacity * sizeof(*styles));
        if(styles == NULL)
            return false;
        editor->styles = styles;
        editor->styleCapacity = capacity;
    }
    if(editor->source == NULL) {
        editor->source = NewHandle(capacity * sizeof(UniChar));
        if(editor->source == NULL)
            return false;
        HLock(editor->source);
    } else if(needed * sizeof(UniChar) > (unsigned)GetHandleSize(editor->source)) {
        DisposeLayout(&editor->paragraph);
        HUnlock(editor->source);
        SetHandleSize(editor->source, capacity * sizeof(UniChar));
        OSErr error = MemError();
        HLock(editor->source);
        if(error != noErr)
            return false;
    }
    return true;
}

/* Keep checkpoints outside a short edit. A touched long line needs new soft wraps. */
static void AdjustSplits(Editor *editor, unsigned start, unsigned end, unsigned inserted)
{
    long delta = (long)inserted - (end - start);
    if(!editor->splitsKnown) {
        ReindexAfter(editor, start, end, delta);
        return;
    }
    const UniChar *text = Source(editor);
    unsigned first = start, last = end;
    while(first != 0 && start - first <= kLayoutSpan && text[first - 1] != '\r')
        --first;
    while(last < editor->length && last - end <= kLayoutSpan && text[last] != '\r')
        ++last;
    unsigned split = NextSplit(editor, first);
    if(last - first > kLayoutSpan || last - first - (end - start) + inserted > kLayoutSpan ||
       (split < editor->splitCount && editor->splits[split] <= last)) {
        ReindexAfter(editor, start, end, delta);
        return;
    }
    for(; split < editor->splitCount; ++split)
        editor->splits[split] = (long)editor->splits[split] + delta;
}

static uint16_t InsertionStyle(Editor *editor, unsigned start, unsigned end)
{
    uint16_t left = start != 0 ? editor->styles[start - 1] : 0;
    uint16_t right = end < editor->length ? editor->styles[end] : 0;
    if(left != 0 && left == right && !(left & kMarkdownMarker))
        return left;

    /* The parser may leave heading padding unstyled. Look back through it
       without crossing a line break, and keep inline styles local. */
    const UniChar *text = Source(editor);
    unsigned before = start;
    while(before != 0 && (text[before - 1] == ' ' || text[before - 1] == '\t') &&
          !(left & kMarkdownHeadingMask)) {
        --before;
        left = before != 0 ? editor->styles[before - 1] : 0;
    }
    unsigned heading = !(right & kMarkdownMarker) ? right & kMarkdownHeadingMask : 0;
    if(heading == 0 && (!(left & kMarkdownMarker) || before < start))
        heading = left & kMarkdownHeadingMask;
    return heading != 0 ? heading | kMarkdownBold : 0;
}

static Boolean CaptureEdit(Editor *editor, unsigned start, unsigned end, unsigned inserted,
                           UndoEdit *edit)
{
    unsigned bytes = (end - start) * sizeof(UniChar);
    Handle removed = NewHandle(bytes);
    if(removed == NULL)
        return false;
    if(bytes != 0)
        memcpy(*removed, Source(editor) + start, bytes);
    *edit = (UndoEdit){removed, start, inserted, editor->anchor, editor->caret, editor->trailing};
    return true;
}

static Boolean Splice(Editor *editor, unsigned start, unsigned end, const UniChar *insert,
                      unsigned count, unsigned caret, Boolean record, Boolean typing)
{
    unsigned removed = end - start;
    if(removed == 0 && count == 0)
        return true;
    unsigned bytes = UTF8Length(insert, count);
    unsigned oldBytes = removed != 0 ? UTF8Length(Source(editor) + start, removed) : 0;
    unsigned newLength = editor->length - removed + count;
    if(bytes > kTextFileLimit || oldBytes > editor->utf8Length ||
       editor->utf8Length - oldBytes + bytes > kTextFileLimit ||
       !Reserve(editor, newLength)) {
        SysBeep(1);
        return false;
    }

    unsigned long now = TickCount();
    UndoEdit *previous = editor->undo.count != 0 ? &editor->undo.edits[editor->undo.count - 1] : NULL;
    Boolean merge = typing && editor->typing && previous != NULL &&
                    now - editor->typedAt <= 45 && start == previous->start + previous->inserted;
    UndoEdit undo;
    if(record && !merge && !CaptureEdit(editor, start, end, count, &undo)) {
        SysBeep(1);
        return false;
    }

    unsigned top = SourceOffset(editor, editor->top);
    AdjustSplits(editor, start, end, count);
    DisposeLayout(&editor->paragraph);
    UniChar *text = (UniChar *)*editor->source;
    unsigned tail = editor->length - end;
    uint16_t style = count != 0 ? InsertionStyle(editor, start, end) : 0;
    if(tail != 0) {
        memmove(text + start + count, text + end, tail * sizeof(*text));
        memmove(editor->styles + start + count, editor->styles + end, tail * sizeof(*editor->styles));
    }
    if(count != 0) {
        memcpy(text + start, insert, count * sizeof(*text));
        for(unsigned i = 0; i < count; ++i) {
            if(insert[i] == '\r')
                style = 0;
            editor->styles[start + i] = style;
        }
    }
    editor->length = newLength;
    editor->utf8Length = editor->utf8Length - oldBytes + bytes;
    if(record) {
        ClearHistory(&editor->redo);
        if(merge)
            previous->inserted += count;
        else
            PushHistory(&editor->undo, undo);
    }
    if(top > start)
        top = top >= end ? top - removed + count : start;
    editor->lastPageKnown = false;
    editor->top = PositionAt(editor, top);
    editor->styleAt = TickCount();
    editor->stylePending = true;
    WorkspaceEdited(editor->workspace);
    Select(editor, caret, false, false);
    editor->typing = typing;
    editor->typedAt = now;
    return true;
}

static Boolean ReplaceRange(Editor *editor, unsigned start, unsigned end, const UniChar *insert,
                            unsigned count, unsigned caret, Boolean record)
{
    return Splice(editor, start, end, insert, count, caret, record, false);
}

static void MoveLineEdge(Editor *editor, Boolean toEnd, Boolean extend)
{
    Position row = CaretRow(editor);
    LayoutSpan *span = Layout(editor, row.start);
    if(span != NULL) {
        unsigned offset = toEnd && span->length != 0 ? span->breaks[row.line] : LineOffset(span, row.line);
        Select(editor, row.start + offset, toEnd, extend);
    }
}

static void MoveVertical(Editor *editor, short lines, Boolean extend)
{
    Position from = CaretRow(editor);
    LayoutSpan *span = Layout(editor, from.start);
    if(span == NULL)
        return;
    Fixed x = CaretX(span, from.line, editor->caret - from.start);
    Position to = MoveLines(editor, from, lines);
    if(to.start == from.start && to.line == from.line && editor->layoutError == noErr) {
        /* At a document edge there is no adjacent row to preserve x on. */
        Select(editor, lines > 0 ? editor->length : 0, lines > 0, extend);
        return;
    }
    Boolean trailing;
    unsigned offset = HitLine(editor, to, x, &trailing);
    Select(editor, offset, trailing, extend);
}

static OSStatus FromRoman(const char *input, ByteCount length, UniChar *out, ByteCount *bytes)
{
    ByteCount read;
    return ConvertFromTextToUnicode(gFromRoman, length, input, 0, 0, NULL, NULL, NULL,
                                    length * sizeof(UniChar), &read, bytes, out);
}

static unsigned SearchText(ConstStr255Param input, UniChar *text)
{
    if(input[0] == 0)
        return 0;
    ByteCount bytes = 0;
    return FromRoman((const char *)input + 1, input[0], text, &bytes) == noErr
        ? bytes / sizeof(*text) : 0;
}

static UniChar FoldSearch(UniChar ch)
{
    return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
}

static Boolean IsWebURL(const UniChar *url, unsigned length)
{
    static const char *const schemes[] = {"https://", "http://"};
    for(unsigned i = 0; i < length; ++i)
        if(url[i] <= ' ' || url[i] == 127)
            return false;
    for(unsigned i = 0; i < sizeof(schemes) / sizeof(*schemes); ++i) {
        unsigned prefix = strlen(schemes[i]);
        unsigned j = 0;
        while(j < prefix && j < length && FoldSearch(url[j]) == schemes[i][j])
            ++j;
        if(j == prefix && j < length)
            return true;
    }
    return false;
}

static void OpenWebURL(const UniChar *url, unsigned length)
{
#ifdef LINEWREN_SMOKE_TEST
    if(gCaptureWebURL) {
        gCapturedWebLength = length;
        memcpy(gCapturedWebURL, url, length * sizeof(*url));
        return;
    }
#endif
    Handle bytes;
    if(EncodeTextUTF8(url, length, &bytes) != noErr) {
        SysBeep(1);
        return;
    }
    ICInstance internet;
    OSStatus error = ICStart(&internet, 'Lnwr');
    if(error == noErr) {
        long start = 0, end = GetHandleSize(bytes);
        HLock(bytes);
        error = ICLaunchURL(internet, "\p", *bytes, end, &start, &end);
        ICStop(internet);
    }
    DisposeHandle(bytes);
    if(error != noErr)
        SysBeep(1);
}

enum { kContextQueryLimit = 128 };

static void OpenTextLookup(const UniChar *text, unsigned length, Boolean dictionary)
{
    Handle bytes;
    if(EncodeTextUTF8(text, length, &bytes) != noErr)
        return;
    static const char hex[] = "0123456789ABCDEF";
    static const char searchPrefix[] = "https://www.google.com/search?q=";
    UniChar url[sizeof(searchPrefix) + kContextQueryLimit * 9];
    const char *prefix = dictionary ? "dict:///" : searchPrefix;
    unsigned size = 0;
    while(*prefix)
        url[size++] = *prefix++;
    const unsigned char *input = (const unsigned char *)*bytes;
    for(unsigned i = 0, count = GetHandleSize(bytes); i < count; ++i) {
        unsigned char ch = input[i];
        if((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~')
            url[size++] = ch;
        else {
            url[size++] = '%';
            url[size++] = hex[ch >> 4];
            url[size++] = hex[ch & 15];
        }
    }
    DisposeHandle(bytes);
    OpenWebURL(url, size);
}

static Boolean FindQuery(const UniChar *text, unsigned length, Str255 query)
{
    if(length == 0 || length > 255)
        return false;
    for(unsigned i = 0; i < length; ++i)
        if(text[i] == '\r' || text[i] == '\n')
            return false;
    Str255 candidate;
    ByteCount read, written;
    if(ConvertFromUnicodeToText(gToRoman, length * sizeof(*text), text, 0,
                               0, NULL, NULL, NULL, 255, &read, &written,
                               candidate + 1) != noErr || read != length * sizeof(*text))
        return false;
    candidate[0] = written;
    memcpy(query, candidate, written + 1);
    return written != 0;
}

Boolean EditorFindSelection(const Editor *editor, Str255 query)
{
    unsigned start, end;
    SelectionRange(editor, &start, &end);
    return start != end && FindQuery(Source(editor) + start, end - start, query);
}

Boolean EditorContextMenu(Editor *editor, Point point, Str255 find)
{
    Rect text = TextBounds(editor);
    if(!PtInRect(point, &text))
        return false;

    enum { kMenu = 136, kFind = 5, kSearch = 6, kDictionary = 7 };
    MenuRef menu = GetMenu(kMenu);
    if(menu == NULL)
        return false;
    unsigned start, end;
    SelectionRange(editor, &start, &end);
    if(start == end) {
        Position row;
        Boolean trailing;
        unsigned hit = HitPoint(editor, point, &row, &trailing);
        Boolean hitTrailing = trailing;
        ClickRange(editor, point, 2, &start, &end, &trailing);
        Select(editor, hit, hitTrailing, false);
        DisableMenuItem(menu, 1);
        DisableMenuItem(menu, 2);
    }
    if(ReadScrap(NULL, 'utxt') <= 0 && ReadScrap(NULL, 'TEXT') <= 0)
        DisableMenuItem(menu, 3);

    const UniChar *source = Source(editor);
    while(start < end && DocumentIsSpace(source[start]))
        ++start;
    while(end > start && DocumentIsSpace(source[end - 1]))
        --end;
    unsigned length = end - start;
    if(length > kContextQueryLimit) {
        length = kContextQueryLimit;
        if((source[start + length - 1] & 0xfc00) == 0xd800)
            --length;
    }
    UniChar query[kContextQueryLimit];
    if(length != 0)
        memcpy(query, source + start, length * sizeof(*query));
    else {
        DisableMenuItem(menu, kFind);
        DisableMenuItem(menu, kSearch);
        DisableMenuItem(menu, kDictionary);
    }
    Boolean dictionary = false;
#if TARGET_API_MAC_CARBON
    long version;
    dictionary = Gestalt(gestaltSystemVersion, &version) == noErr && version >= 0x1040;
#endif
    if(!dictionary)
        DeleteMenuItem(menu, kDictionary);

    LocalToGlobal(&point);
    InsertMenu(menu, -1);
    long choice = PopUpMenuSelect(menu, point.v, point.h, 1);
    DeleteMenu(kMenu);
    DisposeMenu(menu);
    if((choice >> 16) != kMenu)
        return false;
    switch(choice & 0xffff) {
        case 1: EditorCommand(editor, kCutItem); break;
        case 2: EditorCommand(editor, kCopyItem); break;
        case 3: EditorCommand(editor, kPasteItem); break;
        case kSearch: OpenTextLookup(query, length, false); break;
        case kDictionary: OpenTextLookup(query, length, true); break;
        case kFind: return FindQuery(query, length, find);
    }
    return false;
}

static Boolean ClickMarkdown(Editor *editor, Point point, Boolean command)
{
    Position row;
    Boolean trailing;
    unsigned offset = HitPoint(editor, point, &row, &trailing);
    LayoutSpan *span = Layout(editor, row.start);
    if(span == NULL || span->length == 0)
        return false;
    unsigned first = row.start + LineOffset(span, row.line);
    unsigned last = row.start + span->breaks[row.line];
    Fixed x = (Fixed)(point.h - TextBounds(editor).left) * 65536;
    unsigned at = offset > first ? offset - 1 : first;
    for(; at <= offset && at < last; ++at) {
        Fixed left = CaretX(span, row.line, at - row.start);
        Fixed right = CaretX(span, row.line, at + 1 - row.start);
        if((x >= left && x < right) || (x >= right && x < left))
            break;
    }
    if(at > offset || at >= last)
        return false;
    UniChar ch = Source(editor)[at];
    if(!command && ch != '[' && ch != ']' && ch != ' ' && ch != 'x' && ch != 'X')
        return false;
    RefreshStyles(editor, true);
    if(command) {
        if(!(editor->styles[at] & kMarkdownLink))
            return false;
        UniChar url[2048];
        unsigned length = MarkdownLinkAt(Source(editor), editor->length, at, url, 2048);
        if(!IsWebURL(url, length))
            return false;
        OpenWebURL(url, length);
        return true;
    }

    MarkdownEdit edit;
    if(!MarkdownToggleTask(Source(editor), editor->styles, editor->length, at, &edit))
        return false;
    unsigned anchor = editor->anchor, caret = editor->caret;
    Boolean bias = editor->trailing;
    if(ReplaceRange(editor, edit.start, edit.end, edit.text, edit.length, caret, true)) {
        editor->anchor = anchor;
        Select(editor, caret, bias, true);
        RefreshStyles(editor, true);
    }
    return true;
}

static DocumentQuery EditorQuery(Editor *editor, const UniChar *query, unsigned length)
{
    return (DocumentQuery){Source(editor), query, editor->length, length};
}

static void SelectMatch(Editor *editor, unsigned offset, unsigned length)
{
    editor->anchor = offset;
    Select(editor, offset + length, true, true);
}

/* The live query keeps the current match. Navigation moves one match and wraps. */
unsigned EditorSearch(Editor *editor, ConstStr255Param input, short direction, unsigned *ordinal)
{
    UniChar query[255];
    unsigned length = SearchText(input, query);
    unsigned start, end;
    *ordinal = 0;
    if(length == 0)
        return 0;
    SelectionRange(editor, &start, &end);
    DocumentMatch match = DocumentFind(EditorQuery(editor, query, length),
                                       direction > 0 ? end : start, direction < 0);
    *ordinal = match.ordinal;
    if(match.count != 0)
        SelectMatch(editor, match.offset, length);
    else if(direction == 0 && start != end)
        Select(editor, start, false, false);
    return match.count;
}

static unsigned ReplaceAll(Editor *editor, DocumentQuery query,
                           const UniChar *insert, unsigned insertLength)
{
    unsigned first = 0, end = 0, matches = 0;
    for(size_t at = DocumentNextMatch(query, 0); at < query.length;
        at = DocumentNextMatch(query, end)) {
        if(matches++ == 0)
            first = at;
        end = at + query.queryLength;
    }
    if(matches == 0)
        return 0;
    long delta = (long)matches * ((long)insertLength - (long)query.queryLength);
    if((long)editor->length + delta > kTextFileLimit) {
        SysBeep(1);
        return 0;
    }
    unsigned length = (long)(end - first) + delta;
    UniChar *replacement = length != 0 ? malloc(length * sizeof(*replacement)) : NULL;
    if(length != 0 && replacement == NULL) {
        SysBeep(1);
        return 0;
    }
    unsigned used = 0, copied = first;
    query.length = end;
    for(size_t at = first; at < end; at = DocumentNextMatch(query, copied)) {
        unsigned gap = at - copied;
        if(gap != 0)
            memcpy(replacement + used, query.text + copied, gap * sizeof(*replacement));
        used += gap;
        if(insertLength != 0)
            memcpy(replacement + used, insert, insertLength * sizeof(*insert));
        used += insertLength;
        copied = at + query.queryLength;
    }
    Boolean applied = ReplaceRange(editor, first, end, replacement, length, first, true);
    free(replacement);
    return applied ? matches : 0;
}

unsigned EditorReplaceText(Editor *editor, ConstStr255Param find,
                           ConstStr255Param replacement, Boolean all)
{
    UniChar query[255], insert[255];
    unsigned queryLength = SearchText(find, query);
    unsigned insertLength = SearchText(replacement, insert);
    if(queryLength == 0)
        return 0;
    DocumentQuery pattern = EditorQuery(editor, query, queryLength);
    if(all)
        return ReplaceAll(editor, pattern, insert, insertLength);

    unsigned start, end;
    SelectionRange(editor, &start, &end);
    if(end - start != queryLength || !DocumentMatchesAt(pattern, start)) {
        DocumentMatch match = DocumentFind(pattern, end, false);
        if(match.count == 0) {
            SysBeep(1);
            return 0;
        }
        SelectMatch(editor, match.offset, queryLength);
        start = match.offset;
        end = start + queryLength;
    }
    if(!ReplaceRange(editor, start, end, insert, insertLength, start + insertLength, true))
        return 0;
    DocumentMatch next = DocumentFind(EditorQuery(editor, query, queryLength),
                                      start + insertLength, false);
    if(next.count != 0)
        SelectMatch(editor, next.offset, queryLength);
    return 1;
}

void EditorKey(Editor *editor, const EventRecord *event)
{
    editor->clickCount = 0;
    unsigned char key = event->message & charCodeMask;
    Boolean extend = (event->modifiers & shiftKey) != 0;
    if(event->modifiers & controlKey)
        return;
    if(event->modifiers & cmdKey) {
        if(event->modifiers & optionKey)
            return;
        switch(key) {
            case 0x1c: MoveLineEdge(editor, false, extend); break;
            case 0x1d: MoveLineEdge(editor, true, extend); break;
            case 0x1e: Select(editor, 0, false, extend); break;
            case 0x1f: Select(editor, editor->length, false, extend); break;
        }
        return;
    }
    unsigned start, end;
    SelectionRange(editor, &start, &end);
    switch(key) {
        case 0x1c:
        case 0x1d: {
            Boolean right = key == 0x1d;
            unsigned offset = (event->modifiers & optionKey)
                              ? WordEdge(editor, editor->caret, right)
                              : !extend && start != end ? (right ? end : start)
                              : CursorStep(editor, editor->caret, right);
            Select(editor, offset, !right, extend);
            return;
        }
        case 0x1e: MoveVertical(editor, -1, extend); return;
        case 0x1f: MoveVertical(editor, 1, extend); return;
        case 0x0b: MoveVertical(editor, -PageSteps(editor), extend); return;
        case 0x0c: MoveVertical(editor, PageSteps(editor), extend); return;
        case 0x01: Select(editor, 0, false, extend); return;
        case 0x04: Select(editor, editor->length, false, extend); return;
        case '\b':
        case 0x7f:
            if(start == end) {
                ATSUCursorMovementType movement = event->modifiers & optionKey
                    ? kATSUByWord : kATSUByTypographicCluster;
                if(key == '\b')
                    start = CursorStepBy(editor, start, false, movement);
                else
                    end = CursorStepBy(editor, end, true, movement);
            }
            ReplaceRange(editor, start, end, NULL, 0, start, true);
            return;
    }

    if(key == 0x03)
        key = '\r';
    if(key == '\r' || key == '\t') {
        MarkdownEdit edit;
        MarkdownListAction action = key == '\r' ? kContinueList : extend ? kOutdentList : kIndentList;
        if(editor->markdown && start == end && editor->length != 0 && !(event->modifiers & optionKey) &&
           MarkdownListEdit(Source(editor), editor->length, start, action, &edit)) {
            ReplaceRange(editor, edit.start, edit.end, edit.text, edit.length, edit.caret, true);
            return;
        }
    } else if(key < ' ') {
        return;
    }
    UniChar ch = key;
    ByteCount bytes;
    if(key < 0x80 || FromRoman((const char *)&key, 1, &ch, &bytes) == noErr)
        Splice(editor, start, end, &ch, 1, start + 1, true,
               start == end && key >= ' ');
}

static OSStatus ClearClipboard(void)
{
#if TARGET_API_MAC_CARBON
    return ClearCurrentScrap();
#else
    return ZeroScrap();
#endif
}

static OSStatus WriteScrap(ResType type, Size length, const void *data)
{
#if TARGET_API_MAC_CARBON
    ScrapRef scrap;
    OSStatus error = GetCurrentScrap(&scrap);
    return error == noErr ? PutScrapFlavor(scrap, type, kScrapFlavorMaskNone, length, data) : error;
#else
    return PutScrap(length, type, data);
#endif
}

static Boolean CopySelection(Editor *editor, unsigned start, unsigned end)
{
    if(start == end)
        return false;
    const UniChar *text = Source(editor) + start;
    ByteCount length = (end - start) * sizeof(*text);
    if(ClearClipboard() != noErr || WriteScrap('utxt', length, text) != noErr)
        return false;
    Handle roman = NewHandle(length);
    if(roman != NULL) {
        HLock(roman);
        ByteCount read, written;
        OSStatus status = ConvertFromUnicodeToText(gToRoman, length, text, kUnicodeUseFallbacksMask,
                                                  0, NULL, NULL, NULL, length, &read, &written, *roman);
        if(status == noErr || status == kTECUsedFallbacksStatus)
            WriteScrap('TEXT', written, *roman);
        DisposeHandle(roman);
    }
    return true;
}

static void Paste(Editor *editor, unsigned start, unsigned end)
{
    Handle text = NewHandle(0), roman = NULL;
    if(text == NULL)
        return;
    long bytes = ReadScrap(text, 'utxt');
    if(bytes <= 0) {
        roman = text;
        text = NULL;
        bytes = ReadScrap(roman, 'TEXT');
        if(bytes > 0 && bytes <= kTextFileLimit) {
            text = NewHandle(bytes * sizeof(UniChar));
            if(text != NULL) {
                HLock(roman);
                HLock(text);
                ByteCount converted;
                if(FromRoman(*roman, bytes, (UniChar *)*text, &converted) == noErr)
                    bytes = converted;
                else
                    bytes = 0;
            }
        }
        DisposeHandle(roman);
    }
    if(text != NULL) {
        if(bytes > 0 && bytes <= 2 * kTextFileLimit && bytes % sizeof(UniChar) == 0) {
            HLock(text);
            UniChar *chars = (UniChar *)*text;
            unsigned length = bytes / sizeof(*chars), used = 0;
            for(unsigned i = chars[0] == 0xfeff ? 1 : 0; i < length; ++i) {
                UniChar ch = chars[i];
                if(ch == '\r' && i + 1 < length && chars[i + 1] == '\n')
                    ++i;
                chars[used++] = ch == '\n' ? '\r' : ch;
            }
            ReplaceRange(editor, start, end, chars, used, start + used, true);
        }
        DisposeHandle(text);
    }
}

Boolean EditorCanUndo(Editor *editor) { return editor->undo.count != 0; }
Boolean EditorCanRedo(Editor *editor) { return editor->redo.count != 0; }

static Boolean FormattingSpace(UniChar ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == 0xa0 || ch == 0x1680 ||
           (ch >= 0x2000 && ch <= 0x200a) || ch == 0x202f || ch == 0x205f || ch == 0x3000;
}

static Boolean HasEmphasisPair(Editor *editor, unsigned first, unsigned last,
                               UniChar marker, unsigned width)
{
    const UniChar *text = Source(editor);
    unsigned left = first, right = last;
    while(left > 0 && text[left - 1] == marker)
        --left;
    while(right < editor->length && text[right] == marker)
        ++right;
    unsigned run = first - left;
    if(run != right - last || run < width || run > 3)
        return false;
    if(first == last)
        return run == width; /* Cancel an empty pair before typing. */
    /* A triple run combines strong and emphasis. A double run is only strong. */
    if(width == 1 && run == 2)
        return false;

    if(!(editor->styles[first - 1] & kMarkdownMarker) ||
       !(editor->styles[last] & kMarkdownMarker))
        return false;
    unsigned flag = width == 2 ? kMarkdownBold : kMarkdownItalic;
    Boolean styled = false;
    for(unsigned i = first; i < last; ++i) {
        if((editor->styles[i] & kMarkdownMarker) || FormattingSpace(text[i]))
            continue;
        if(!(editor->styles[i] & flag))
            return false;
        styled = true;
    }
    return styled;
}

static Boolean FindEmphasisPair(Editor *editor, UniChar marker, unsigned width,
                                unsigned *first, unsigned *last)
{
    if(HasEmphasisPair(editor, *first, *last, marker, width))
        return true;
    /* Also accept a selection that includes both delimiter runs. */
    const UniChar *text = Source(editor);
    unsigned start = *first, end = *last;
    while(start < end && text[start] == marker)
        ++start;
    while(end > start && text[end - 1] == marker)
        --end;
    if(start == *first || end == *last || start == end ||
       !HasEmphasisPair(editor, start, end, marker, width))
        return false;
    *first = start;
    *last = end;
    return true;
}

static void ToggleEmphasis(Editor *editor, unsigned start, unsigned end, unsigned width)
{
    RefreshStyles(editor, true);
    const UniChar *text = Source(editor);
    unsigned first = start, last = end;
    while(first < last && FormattingSpace(text[first]))
        ++first;
    while(last > first && FormattingSpace(text[last - 1]))
        --last;
    for(unsigned i = first; i < last; ++i) {
        if(text[i] == '\r') {
            SysBeep(1);
            return;
        }
    }
    if((start != end && first == last) ||
       (first < editor->length && (editor->styles[first] & (kMarkdownCode | kMarkdownList)))) {
        SysBeep(1);
        return;
    }

    Boolean remove = FindEmphasisPair(editor, '*', width, &first, &last) ||
                     FindEmphasisPair(editor, '_', width, &first, &last);
    unsigned payload = last - first;
    unsigned padding = remove ? 0 : width;
    unsigned count = payload + 2 * padding;
    UniChar *insert = count != 0 ? malloc(count * sizeof(*insert)) : NULL;
    if(count != 0 && insert == NULL) {
        SysBeep(1);
        return;
    }
    for(unsigned i = 0; i < padding; ++i) {
        insert[i] = '*';
        insert[padding + payload + i] = '*';
    }
    if(payload != 0)
        memcpy(insert + padding, text + first, payload * sizeof(*insert));

    Boolean reversed = editor->caret < editor->anchor;
    unsigned left = remove ? first - width : first;
    unsigned right = remove ? last + width : last;
    unsigned selectionStart = left + padding, selectionEnd = selectionStart + payload;
    unsigned caret = reversed ? selectionStart : selectionEnd;
    if(ReplaceRange(editor, left, right, insert, count, caret, true)) {
        RefreshStyles(editor, true);
        editor->anchor = reversed ? selectionEnd : selectionStart;
        Select(editor, caret, false, true);
    }
    free(insert);
}

static unsigned OffsetAfterPrefix(unsigned offset, const MarkdownEdit *edit)
{
    if(offset < edit->start)
        return offset;
    unsigned after = edit->start + edit->length;
    return offset < edit->end ? after : after + offset - edit->end;
}

static void FormatLine(Editor *editor, MarkdownPrefix prefix)
{
    RefreshStyles(editor, true);
    MarkdownEdit edit;
    if(!MarkdownPrefixEdit(Source(editor), editor->styles, editor->length,
                           editor->caret, prefix, &edit))
        return;
    unsigned anchor = OffsetAfterPrefix(editor->anchor, &edit);
    unsigned caret = OffsetAfterPrefix(editor->caret, &edit);
    if(ReplaceRange(editor, edit.start, edit.end, edit.text, edit.length, caret, true)) {
        RefreshStyles(editor, true);
        editor->anchor = anchor;
        Select(editor, caret, false, true);
    }
}

static void InsertLink(Editor *editor, unsigned start, unsigned end)
{
    const UniChar *source = Source(editor);
    unsigned length = 4;
    for(unsigned i = start; i < end; ++i) {
        if(source[i] == '\r') {
            SysBeep(1);
            return;
        }
        length += source[i] == '[' || source[i] == ']' || source[i] == '\\' ? 2 : 1;
    }
    UniChar *link = malloc(length * sizeof(*link));
    if(link == NULL) {
        SysBeep(1);
        return;
    }
    unsigned used = 0;
    link[used++] = '[';
    for(unsigned i = start; i < end; ++i) {
        UniChar ch = source[i];
        if(ch == '[' || ch == ']' || ch == '\\')
            link[used++] = '\\';
        link[used++] = ch;
    }
    memcpy(link + used, u"]()", 3 * sizeof(*link));
    unsigned caret = start + (start == end ? 1 : length - 1);
    if(ReplaceRange(editor, start, end, link, length, caret, true))
        RefreshStyles(editor, true);
    free(link);
}

static void ReplayEdit(Editor *editor, EditHistory *from, EditHistory *to)
{
    if(from->count == 0)
        return;
    UndoEdit edit = from->edits[from->count - 1], inverse;
    if(!CaptureEdit(editor, edit.start, edit.start + edit.inserted,
                    GetHandleSize(edit.removed) / sizeof(UniChar), &inverse)) {
        SysBeep(1);
        return;
    }
    HLock(edit.removed);
    Boolean applied = ReplaceRange(editor, edit.start, edit.start + edit.inserted,
                                   (const UniChar *)*edit.removed, inverse.inserted,
                                   edit.caret, false);
    if(applied) {
        from->bytes -= GetHandleSize(edit.removed);
        --from->count;
        DisposeHandle(edit.removed);
        PushHistory(to, inverse);
        editor->anchor = edit.anchor;
        Select(editor, edit.caret, edit.trailing, true);
    } else {
        DisposeHandle(inverse.removed);
        HUnlock(edit.removed);
    }
}

void EditorCommand(Editor *editor, short item)
{
    editor->typing = false;
    unsigned start, end;
    SelectionRange(editor, &start, &end);
    switch(item) {
        case kUndoItem: ReplayEdit(editor, &editor->undo, &editor->redo); break;
        case kRedoItem: ReplayEdit(editor, &editor->redo, &editor->undo); break;
        case kCutItem:
            if(!CopySelection(editor, start, end))
                break;
            /* Fall through to delete the copied range. */
        case kClearItem:
            ReplaceRange(editor, start, end, NULL, 0, start, true);
            break;
        case kCopyItem: CopySelection(editor, start, end); break;
        case kPasteItem: Paste(editor, start, end); break;
        case kBoldItem:
        case kItalicItem:
            if(editor->markdown)
                ToggleEmphasis(editor, start, end, item == kBoldItem ? 2 : 1);
            break;
        case kLinkItem:
            if(editor->markdown)
                InsertLink(editor, start, end);
            break;
        case kHeadingItem:
        case kQuoteItem:
        case kListItem:
        case kTaskItem:
            if(editor->markdown)
                FormatLine(editor, (MarkdownPrefix)(item - kHeadingItem));
            break;
        case kSelectAllItem:
            editor->typing = false;
            editor->anchor = 0;
            editor->caret = editor->length;
            editor->trailing = false;
            InvalidateWritingSurface(editor);
            if(!EmptyRect(&editor->outline))
                InvalidateRect(&editor->outline);
            break;
    }
}

static void ReindexStyledLines(Editor *editor, const uint16_t *styles)
{
    if(editor->splitCount == 0 && editor->splitsKnown)
        return;
    const UniChar *text = Source(editor);
    for(unsigned start = 0, end; start < editor->length; start = end + 1) {
        for(end = start; end < editor->length && text[end] != '\r'; ++end)
            ;
        if(end - start > kLayoutSpan &&
           memcmp(editor->styles + start, styles + start, (end - start) * sizeof(*styles)) != 0) {
            unsigned last = editor->length;
            while(last > start && editor->styles[last - 1] == styles[last - 1])
                --last;
            ReindexAfter(editor, start, last, 0);
            return;
        }
    }
}

static void RefreshStyles(Editor *editor, Boolean immediate)
{
    if(!editor->stylePending || (!immediate && TickCount() - editor->styleAt < kStyleDelay))
        return;
    editor->stylePending = false;
    unsigned bytes = editor->length * sizeof(*editor->styles);
    MarkdownHeading *headings;
    unsigned count;
    uint16_t *styles = DocumentStyles(editor, Source(editor), editor->length, editor->markdown,
                                      &headings, &count);
    ReplaceOutline(editor, headings, count);
#ifdef LINEWREN_SMOKE_TEST
    gStyleFreeHeap = FreeMem();
#endif
    if(styles == NULL)
        return;
    if(memcmp(editor->styles, styles, bytes) != 0) {
        unsigned offset = SourceOffset(editor, editor->top);
        ReindexStyledLines(editor, styles);
        memcpy(editor->styles, styles, bytes);
        DisposeLayout(&editor->paragraph);
        editor->lastPageKnown = false;
        ScrollTo(editor, PositionAt(editor, offset));
        InvalidateWritingSurface(editor);
    }
    free(styles);
}

void EditorIdle(Editor *editor)
{
    RefreshStyles(editor, false);
    unsigned long started = TickCount();
    for(unsigned span = 0; span < kIdleWrapBudget && !editor->splitsKnown; ++span) {
        if(IndexThrough(editor, editor->length, 1) != noErr || TickCount() != started)
            return;
    }
    if(editor->splitsKnown && !editor->lastPageKnown) {
        BottomPage(editor);
        ScrollTo(editor, editor->top);
        Rect bar = ScrollbarBounds(&editor->view);
        InvalidateRect(&bar);
    }
}

void EditorActivate(Editor *editor, Boolean active)
{
    editor->active = active;
    InvalidateRect(&editor->view);
}

#ifdef LINEWREN_SMOKE_TEST
Boolean EditorIsEmpty(Editor *editor) { return editor->length == 0; }

Boolean EditorSelectionAt(Editor *editor, unsigned anchor, unsigned caret)
{
    return editor->anchor == anchor && editor->caret == caret;
}

unsigned EditorTopOffset(Editor *editor)
{
    return SourceOffset(editor, editor->top);
}

Boolean EditorAllSelected(Editor *editor)
{
    return editor->anchor == 0 && editor->caret == editor->length;
}

Boolean EditorTypographyMatches(Editor *editor, EditorTypography typography)
{
    ATSUFontID expectedFont, actualFont;
    ATSUStyle body, title;
    Fixed bodySize, titleSize;
    ByteCount size;
    return ATSUFONDtoFontID(typography.family, normal, &expectedFont) == noErr &&
           GetStyle(editor, 0, &body) == noErr &&
           GetStyle(editor, 1 | kMarkdownBold, &title) == noErr &&
           ATSUGetAttribute(body, kATSUFontTag, sizeof(actualFont), &actualFont, &size) == noErr &&
           ATSUGetAttribute(body, kATSUSizeTag, sizeof(bodySize), &bodySize, &size) == noErr &&
           ATSUGetAttribute(title, kATSUSizeTag, sizeof(titleSize), &titleSize, &size) == noErr &&
           actualFont == expectedFont && bodySize == (Fixed)typography.points << 16 &&
           titleSize == (Fixed)(typography.points + kTitlePoints - kBodyPoints) << 16;
}

static short FirstLetterGap(Editor *editor, short row)
{
    short baseline = editor->view.top + editor->metrics[0].baseline + row * editor->metrics[0].height;
    unsigned threshold = ((unsigned)editor->theme->page.red + editor->theme->text.red) / 2;
    Boolean dark = editor->theme->page.red < editor->theme->text.red;
    Boolean seenInk = false;
    short gap = 0;
    for(short x = editor->view.left; x < editor->view.left + 24; ++x) {
        Boolean ink = false;
        for(short y = baseline - editor->typography.points; y < baseline + 1 && !ink; ++y) {
            RGBColor color;
            GetCPixel(x, y, &color);
            ink = dark ? color.red > threshold : color.red < threshold;
        }
        if(ink && gap != 0)
            return gap;
        if(seenInk && !ink)
            ++gap;
        seenInk |= ink;
    }
    return -1;
}

static const char *CheckMouseAndWordSelection(Editor *editor)
{
    static const UniChar fixture[] = u"alpha beta\r# Heading\rgamma delta";
    Handle text = NewHandle(sizeof(fixture) - sizeof(*fixture));
    if(text == NULL)
        return "FAIL: allocate the selection fixture";
    memcpy(*text, fixture, sizeof(fixture) - sizeof(*fixture));
    if(EditorTakeText(editor, text) != noErr) {
        DisposeHandle(text);
        return "FAIL: load the selection fixture";
    }
    LayoutSpan *span = Layout(editor, 0);
    if(span == NULL)
        return "FAIL: lay out the selection fixture";
    if(MetricsAt(editor, (Position){11, 0}).height <= editor->metrics[0].height)
        return "FAIL: body rows did not use less space than headings";
    span = Layout(editor, 0);
    if(span == NULL)
        return "FAIL: lay out the selection fixture";
    unsigned previous = 0;
    for(short pixel = 0; pixel <= (CaretX(span, 0, 10) >> 16); ++pixel) {
        Boolean trailing;
        unsigned offset = HitLine(editor, (Position){0, 0}, (Fixed)pixel << 16, &trailing);
        if(offset < previous || offset > previous + 1)
            return "FAIL: mouse hit testing skipped or reversed a caret between glyphs";
        previous = offset;
    }
    short x = editor->view.left + (CaretX(span, 0, 8) >> 16);
    Point where = {editor->view.top + editor->metrics[0].baseline - 2, x};
    LocalToGlobal(&where);
    EventRecord mouse = {.what = mouseDown, .where = where, .when = 100};
    EditorClick(editor, &mouse);
    if(!EditorSelectionAt(editor, 8, 8))
        return "FAIL: single click missed the caret between letters";
    ++mouse.when;
    EditorClick(editor, &mouse);
    if(!EditorSelectionAt(editor, 6, 10))
        return "FAIL: double-click did not select the word";
    ++mouse.when;
    EditorClick(editor, &mouse);
    if(!EditorSelectionAt(editor, 0, 11))
        return "FAIL: triple-click did not select the line and its return";

    Select(editor, 8, false, false);
    EventRecord key = {.what = keyDown, .message = 0x1c, .modifiers = optionKey};
    EditorKey(editor, &key);
    if(!EditorSelectionAt(editor, 6, 6))
        return "FAIL: Option-Left did not move to the start of the word";
    key.modifiers |= shiftKey;
    key.message = 0x1d;
    EditorKey(editor, &key);
    if(!EditorSelectionAt(editor, 6, 10))
        return "FAIL: Option-Shift-Right did not extend to the end of the word";

    const EditorTheme *original = editor->theme;
    EditorTheme dark = *original;
    dark.page = (RGBColor){0x1919, 0x1919, 0x1919};
    dark.text = (RGBColor){0xbdbd, 0xbdbd, 0xbdbd};
    EditorSetTheme(editor, &dark);
    EditorDraw(editor);
    Boolean colored = false, ink = false;
    for(short y = editor->view.top; y < editor->view.top + editor->metrics[0].height; ++y) {
        for(short h = x - 4; h <= x + 4; ++h) {
            RGBColor pixel;
            GetCPixel(h, y, &pixel);
            colored |= pixel.blue > pixel.red + 0x1800 && pixel.red < 0x6000;
            ink |= pixel.red > 0x9000;
        }
    }
    EditorSetTheme(editor, original);
    if(!colored || !ink)
        return "FAIL: dark selection did not preserve light ink over a colored highlight";

    Select(editor, 5, false, false);
    key.modifiers = optionKey;
    key.message = 0x1d;
    EditorKey(editor, &key);
    if(!EditorSelectionAt(editor, 10, 10))
        return "FAIL: Option-Right did not cross the space to the next word's end";
    Select(editor, 6, false, false);
    key.message = 0x1c;
    EditorKey(editor, &key);
    if(!EditorSelectionAt(editor, 0, 0))
        return "FAIL: Option-Left did not cross the space to the previous word's start";

    const unsigned end = sizeof(fixture) / sizeof(*fixture) - 1;
    const struct { unsigned key, inside, edge; } vertical[] = {
        {0x1e, 8, 0},
        {0x1f, end - 3, end}
    };
    for(unsigned i = 0; i < sizeof(vertical) / sizeof(*vertical); ++i) {
        key.message = vertical[i].key;
        key.modifiers = shiftKey;
        Select(editor, vertical[i].inside, false, false);
        EditorKey(editor, &key);
        if(!EditorSelectionAt(editor, vertical[i].inside, vertical[i].edge))
            return "FAIL: Shift with Up or Down did not extend to the document edge";
        key.modifiers = 0;
        Select(editor, vertical[i].inside, false, false);
        EditorKey(editor, &key);
        if(!EditorSelectionAt(editor, vertical[i].edge, vertical[i].edge))
            return "FAIL: Up or Down did not move to the document edge";
    }
    return NULL;
}

static Boolean MatchesFullReflow(Editor *editor)
{
    if(!editor->splitsKnown)
        return false;
    unsigned splits[kMaxSplits];
    unsigned count = editor->splitCount;
    memcpy(splits, editor->splits, count * sizeof(*splits));
    ResetSplits(editor);
    return IndexThrough(editor, editor->length, kMaxSplits) == noErr &&
           editor->splitsKnown && editor->splitCount == count &&
           memcmp(splits, editor->splits, count * sizeof(*splits)) == 0;
}

static const char *CheckDeferredReflow(Editor *editor, WindowRef window,
                                       unsigned long *visibleTicks, unsigned long *pasteTicks)
{
    const unsigned length = 96 * 1024;
    Handle text = NewHandle(length * sizeof(UniChar));
    if(text == NULL)
        return "FAIL: could not allocate the nonconverging wrap fixture";
    for(unsigned i = 0; i < length; ++i)
        ((UniChar *)*text)[i] = 'i';
    if(EditorTakeText(editor, text) != noErr) {
        DisposeHandle(text);
        return "FAIL: could not load the nonconverging wrap fixture";
    }
    if(IndexThrough(editor, length, kMaxSplits) != noErr || !editor->splitsKnown)
        return "FAIL: could not index the initial long line";

    /* An unchanged replacement exercises convergence at the first checkpoint. */
    const UniChar unchanged = 'i';
    if(!ReplaceRange(editor, 10, 11, &unchanged, 1, 11, true) ||
       !editor->splitsKnown || !MatchesFullReflow(editor))
        return "FAIL: an aligned checkpoint did not reuse the original tail";

    const UniChar wider = 'W';
    unsigned long then = TickCount();
    if(!ReplaceRange(editor, 10, 11, &wider, 1, 11, true))
        return "FAIL: could not edit the long line";
    EditorDraw(editor);
    *visibleTicks = TickCount() - then;
    if(editor->layoutError != noErr || editor->splitsKnown ||
       editor->splitScanStart >= length / 2 || Source(editor)[10] != wider || editor->caret != 11)
        return "FAIL: first paint did not defer the nonconverging tail";

    unsigned scanned = editor->splitScanStart, count = editor->splitCount;
    editor->anchor = editor->caret = length;
    EditorDraw(editor);
    if(editor->splitsKnown || editor->splitScanStart != scanned)
        return "FAIL: painting an offscreen caret completed the tail";
    editor->anchor = editor->caret = 11;
    EditorIdle(editor);
    if(editor->splitsKnown || editor->splitScanStart <= scanned ||
       editor->splitCount - count > kIdleWrapBudget)
        return "FAIL: idle reflow did not advance by a bounded batch";

    unsigned advanced = editor->splitScanStart;
    if(!ReplaceRange(editor, 11, 12, &wider, 1, 12, true) || editor->splitsKnown ||
       editor->splitScanStart >= advanced || Source(editor)[11] != wider)
        return "FAIL: a second edit did not restart pending reflow";
    EditorDraw(editor);
    for(unsigned batch = 0; !editor->splitsKnown && batch < kMaxSplits; ++batch)
        EditorIdle(editor);
    if(editor->layoutError != noErr || !MatchesFullReflow(editor))
        return "FAIL: deferred wrap checkpoints differed from a full reflow";

    /* A multiline insertion also exercises offset shifts and hard-break convergence. */
    unsigned inserted = 48 * 1024;
    UniChar *paste = malloc(inserted * sizeof(*paste));
    if(paste == NULL)
        return "FAIL: could not allocate the multiline insertion fixture";
    for(unsigned i = 0; i < inserted; ++i)
        paste[i] = i % 80 == 79 ? '\r' : 'a';
    then = TickCount();
    Boolean edited = ReplaceRange(editor, 12, 12, paste, inserted, 12 + inserted, true);
    free(paste);
    EditorDraw(editor);
    *pasteTicks = TickCount() - then;
    if(!edited || editor->layoutError != noErr || editor->caret != 12 + inserted)
        return "FAIL: multiline insertion did not repaint the caret";
    if(IndexThrough(editor, editor->length, kMaxSplits) != noErr || !MatchesFullReflow(editor))
        return "FAIL: multiline insertion wrap checkpoints differed from a full reflow";
    return NULL;
}

static const char *CheckTypingPresentation(Editor *editor)
{
    static const UniChar heading[] = {'#', ' ', 'H', 'i', '\r'};
    Handle source = NewHandle(sizeof(heading));
    if(source == NULL)
        return "FAIL: allocate the heading typing fixture";
    memcpy(*source, heading, sizeof(heading));
    if(EditorTakeText(editor, source) != noErr) {
        DisposeHandle(source);
        return "FAIL: load the heading typing fixture";
    }

    EventRecord key = {.what = keyDown, .message = 'A'};
    Select(editor, 4, false, false);
    EditorKey(editor, &key);
    EditorKey(editor, &key);
    if(editor->caret != 6 || editor->styles[4] != (1 | kMarkdownBold) ||
       editor->styles[5] != (1 | kMarkdownBold))
        return "FAIL: typed heading characters did not keep the heading style before idle";

    Select(editor, editor->length, false, false);
    EditorKey(editor, &key);
    Position row = CaretRow(editor);
    LayoutSpan *span = Layout(editor, row.start);
    Fixed before, after, ascent, descent;
    if(span == NULL || span->length != 1 || editor->styles[row.start] != 0 ||
       ATSUMeasureText(span->layout, 0, 1, &before, &after, &ascent, &descent) != noErr)
        return "FAIL: could not measure the newly typed body character";
    Fixed x = CaretX(span, row.line, editor->caret - row.start);
    if(x <= 0 || labs(x - after) > 65536L)
        return "FAIL: the caret after one typed glyph did not reach its right edge";
    return NULL;
}

static const char *CheckSearchAndPlainText(Editor *editor)
{
    static const UniChar fixture[] = u"One two one";
    Handle source = NewHandle(sizeof(fixture) - sizeof(*fixture));
    if(source == NULL)
        return "FAIL: allocate the search fixture";
    memcpy(*source, fixture, sizeof(fixture) - sizeof(*fixture));
    if(EditorTakeText(editor, source) != noErr)
        return "FAIL: load the search fixture";
    unsigned ordinal;
    if(EditorSearch(editor, "\pone", 1, &ordinal) != 2 || !EditorSelectionAt(editor, 0, 3) ||
       EditorSearch(editor, "\pone", 1, &ordinal) != 2 || !EditorSelectionAt(editor, 8, 11))
        return "FAIL: Find did not select successive case-insensitive matches";
    if(EditorReplaceText(editor, "\pone", "\pthree", false) != 1 ||
       editor->length != 13 || memcmp(Source(editor), u"One two three", 13 * sizeof(UniChar)) != 0)
        return "FAIL: Replace did not update the selected match";
    if(EditorReplaceText(editor, "\ptwo", "\p2", true) != 1 ||
       editor->length != 11 || memcmp(Source(editor), u"One 2 three", 11 * sizeof(UniChar)) != 0)
        return "FAIL: Replace All did not update every match";
    EditorCommand(editor, kUndoItem);
    EditorCommand(editor, kUndoItem);
    if(EditorReplaceText(editor, "\pone", "\pX", true) != 2 ||
       editor->length != 7 || memcmp(Source(editor), u"X two X", 7 * sizeof(UniChar)) != 0)
        return "FAIL: Replace All did not replace both matches";
    EditorCommand(editor, kUndoItem);
    if(editor->length != 11 || memcmp(Source(editor), fixture, 11 * sizeof(UniChar)) != 0)
        return "FAIL: one Undo did not revert Replace All";
    EditorCommand(editor, kRedoItem);
    if(editor->length != 7 || memcmp(Source(editor), u"X two X", 7 * sizeof(UniChar)) != 0)
        return "FAIL: one Redo did not replay Replace All";

    static const UniChar markdown[] = u"# Heading";
    source = NewHandle(sizeof(markdown) - sizeof(*markdown));
    if(source == NULL)
        return "FAIL: allocate the plain-text fixture";
    memcpy(*source, markdown, sizeof(markdown) - sizeof(*markdown));
    EditorSetMarkdown(editor, false);
    if(EditorTakeText(editor, source) != noErr || editor->styles[2] != 0 ||
       EditorWordCount(editor) != 2)
        return "FAIL: plain text was styled as Markdown or counted incorrectly";
    EditorSetMarkdown(editor, true);
    if(!(editor->styles[2] & kMarkdownHeadingMask))
        return "FAIL: Markdown styling did not return after switching document kinds";
    return NULL;
}

static const char *CheckMarkdownClicks(Editor *editor)
{
    static const UniChar fixture[] = u"- [ ] Task\r\r[Example](https://example.com)";
    Handle source = NewHandle(sizeof(fixture) - sizeof(*fixture));
    if(source == NULL)
        return "FAIL: allocate the Markdown click fixture";
    memcpy(*source, fixture, sizeof(fixture) - sizeof(*fixture));
    if(EditorTakeText(editor, source) != noErr) {
        DisposeHandle(source);
        return "FAIL: load the Markdown click fixture";
    }
    Rect text = TextBounds(editor);
    Position row = {0, 0};
    LayoutSpan *span = Layout(editor, row.start);
    if(span == NULL)
        return "FAIL: lay out the task marker";
    Point point = {text.top + 1, text.left + ((CaretX(span, 0, 3) + CaretX(span, 0, 4)) >> 17)};
    EventRecord click = {.what = mouseDown, .where = point};
    LocalToGlobal(&click.where);
    EditorClick(editor, &click);
    if(Source(editor)[3] != 'x' || !EditorCanUndo(editor))
        return "FAIL: clicking a task marker did not toggle it";
    EditorCommand(editor, kUndoItem);
    if(Source(editor)[3] != ' ')
        return "FAIL: Undo did not restore the task marker";

    short y = text.top;
    while(row.start < 12) {
        y += MetricsAt(editor, row).height;
        if(!Step(editor, &row, true))
            return "FAIL: locate the link row";
    }
    span = Layout(editor, row.start);
    if(span == NULL)
        return "FAIL: lay out the link label";
    click.where = (Point){y + 1, text.left + ((CaretX(span, row.line, 1) +
                                              CaretX(span, row.line, 2)) >> 17)};
    LocalToGlobal(&click.where);
    click.modifiers = cmdKey;
    gCaptureWebURL = true;
    EditorClick(editor, &click);
    gCaptureWebURL = false;
    static const UniChar url[] = u"https://example.com";
    if(gCapturedWebLength != sizeof(url) / sizeof(*url) - 1 ||
       memcmp(gCapturedWebURL, url, sizeof(url) - sizeof(*url)) != 0)
        return "FAIL: Command-click did not resolve the web link";
    static const UniChar query[] = u"caf\u00e9 & tea";
    static const UniChar search[] = u"https://www.google.com/search?q=caf%C3%A9%20%26%20tea";
    gCaptureWebURL = true;
    OpenTextLookup(query, sizeof(query) / sizeof(*query) - 1, false);
    gCaptureWebURL = false;
    if(gCapturedWebLength != sizeof(search) / sizeof(*search) - 1 ||
       memcmp(gCapturedWebURL, search, sizeof(search) - sizeof(*search)) != 0)
        return "FAIL: context search did not encode selected text in the URL";
    static const UniChar lookup[] = u"dict:///caf%C3%A9%20%26%20tea";
    gCaptureWebURL = true;
    OpenTextLookup(query, sizeof(query) / sizeof(*query) - 1, true);
    gCaptureWebURL = false;
    if(gCapturedWebLength != sizeof(lookup) / sizeof(*lookup) - 1 ||
       memcmp(gCapturedWebURL, lookup, sizeof(lookup) - sizeof(*lookup)) != 0)
        return "FAIL: dictionary lookup did not put the term in the URL path";
    return NULL;
}

static const char *CheckWritingSurface(Editor *editor)
{
    Rect view = editor->view, wide = view;
    Rect bar = ScrollbarBounds(&view);
    Rect outline = {view.top, bar.left - 148, view.bottom, bar.left};
    EditorResize(editor, &view, &outline);
    Rect control;
#if TARGET_API_MAC_CARBON
    GetControlBounds(editor->scrollbar, &control);
#else
    control = (*editor->scrollbar)->contrlRect;
#endif
    if(TextBounds(editor).right >= outline.left || control.left != bar.left)
        return "FAIL: the floating outline overlapped text or displaced the scrollbar";

    wide.right = wide.left + 900;
    EditorResize(editor, &wide, NULL);
    Rect column = TextBounds(editor);
    if(column.right - column.left != kMaximumTextWidth ||
       abs((column.left - wide.left) -
           (wide.right - kEditorScrollbarWidth - kScrollbarGutter - column.right)) > 1)
        return "FAIL: wide writing column was not capped and centered";

    Handle source = NewHandle(160 * sizeof(UniChar));
    if(source == NULL)
        return "FAIL: allocate the writing-surface fixture";
    for(unsigned i = 0; i < 160; ++i)
        ((UniChar *)*source)[i] = i % 2 == 0 ? 'x' : '\r';
    if(EditorTakeText(editor, source) != noErr) {
        DisposeHandle(source);
        return "FAIL: load the writing-surface fixture";
    }
    Boolean trailing;
    Point point = {column.top + 1, column.left + 32};
    if(HitPoint(editor, point, &(Position){0}, &trailing) != 1)
        return "FAIL: centered column hit testing used the wrong origin";
    EditorSetTypewriter(editor, true);
    Select(editor, 80, false, false);
    Position row = editor->top, caret = CaretRow(editor);
    short y = 0;
    while(Before(row, caret)) {
        y += MetricsAt(editor, row).height;
        if(!Step(editor, &row, true))
            return "FAIL: could not reach the typewriter caret row";
    }
    short rowHeight = MetricsAt(editor, caret).height;
    if(abs(2 * y + rowHeight - (view.bottom - view.top)) >= 2 * rowHeight)
        return "FAIL: typewriter scrolling did not center the caret row";
    unsigned top = EditorTopOffset(editor);
    EventRecord click = {.what = mouseDown, .where = point};
    LocalToGlobal(&click.where);
    EditorClick(editor, &click);
    if(EditorTopOffset(editor) != top)
        return "FAIL: typewriter scrolling shifted the viewport during mouse selection";
    EditorSetTypewriter(editor, false);
    BottomPage(editor);
    unsigned maximum = ScrollMaximum(editor);
    EventRecord newline = {.what = keyDown, .message = '\r'};
    EditorKey(editor, &newline);
    if(editor->lastPageKnown || maximum == 0 || ScrollMaximum(editor) != maximum)
        return "FAIL: Return switched the scrollbar to an estimated range";
    EditorResize(editor, &view, NULL);
    return NULL;
}

const char *EditorSmokeTest(Editor *editor, char *timing, unsigned timingSize)
{
    const char *input = CheckMouseAndWordSelection(editor);
    if(input != NULL)
        return input;
    input = CheckSearchAndPlainText(editor);
    if(input != NULL)
        return input;
    input = CheckWritingSurface(editor);
    if(input != NULL)
        return input;
    input = CheckMarkdownClicks(editor);
    if(input != NULL)
        return input;
    WorkspaceSmokeReset(editor->workspace);
    static const char sample[] = "# Notes\n**Bold** and *italic* with [a link](https://example.com).\n"
                                 "markdown and caf\xc3\xa9\n";
    static const char prose[] = "A classic Macintosh should render markdown with regular spacing. ";
    static const char shortNote[] = "markdown\nmacbook\n";
    unsigned long stamp = TickCount();
    FSSpec file;
    if(HGetVol(NULL, &file.vRefNum, &file.parID) != noErr)
        return "FAIL: locate the ATSUI fixture folder";
    file.name[0] = snprintf((char *)file.name + 1, sizeof(file.name) - 1,
                            "Linewren-ATS-%08lx.md", stamp);
    FSSpec shortFile = file;
    shortFile.name[0] = snprintf((char *)shortFile.name + 1, sizeof(shortFile.name) - 1,
                                 "Linewren-ATS-%08lx-s.md", stamp);
    Boolean shortWritten = false;
    Handle bytes = NewHandle(kTextFileLimit);
    if(bytes == NULL)
        return "FAIL: allocate the ATSUI fixture";
    memcpy(*bytes, sample, sizeof(sample) - 1);
    for(unsigned i = sizeof(sample) - 1; i < kTextFileLimit; ++i) {
        unsigned column = (i - (sizeof(sample) - 1)) % 256;
        (*bytes)[i] = i < kTextFileLimit / 2 && column == 255
                      ? '\n' : prose[column % (sizeof(prose) - 1)];
    }
    const char *result = "FAIL: write the ATSUI Markdown fixture";
    Boolean written = WriteTextFile(&file, bytes, NULL) == noErr;
    DisposeHandle(bytes);
    if(!written)
        return result;

    WindowRef window = FrontWindow();
    result = "FAIL: open a 192 KiB Markdown file in the ATSUI workspace";
    OSErr openError = WorkspaceOpen(editor, window, &file);
    if(openError != noErr) {
        editor->layoutError = openError;
        goto done;
    }
    if(!WorkspaceHasFile(editor->workspace, &file))
        goto done;
    long openFreeHeap = FreeMem();
    result = "FAIL: UTF-16 content or Markdown styles missing after open";
    if(editor->length != kTextFileLimit - 1 || Source(editor)[sizeof(sample) - 4] != 0xe9 ||
       editor->styles[2] != (1 | kMarkdownBold) || !(editor->styles[10] & kMarkdownBold))
        goto done;

    result = "FAIL: ATSUI paragraph did not create styled runs and wrap";
    LayoutSpan *paragraph = Layout(editor, 8);
    if(paragraph == NULL || paragraph->count < 2 || editor->runs[kMarkdownBold] == NULL ||
       editor->runs[kMarkdownItalic] == NULL || editor->runs[kMarkdownLink] == NULL)
        goto done;

    result = "FAIL: wrapped-line caret did not round-trip through hit testing";
    for(unsigned row = 0; row < 2; ++row) {
        unsigned start = LineOffset(paragraph, row);
        unsigned end = paragraph->breaks[row];
        for(unsigned offset = start + 1; offset < end && offset <= start + 3; ++offset) {
            Boolean trailing;
            if(HitLine(editor, (Position){8, row}, CaretX(paragraph, row, offset), &trailing) != 8 + offset) {
                static char failure[100];
                snprintf(failure, sizeof(failure), "%s (row %u, offset %u)", result, row, offset);
                result = failure;
                goto done;
            }
        }
    }

    result = "FAIL: caret at a soft wrap did not round-trip through hit testing";
    unsigned wrap = paragraph->breaks[0];
    Boolean trailing;
    if(CaretX(paragraph, 0, wrap) <= 0 ||
       HitLine(editor, (Position){8, 0}, CaretX(paragraph, 0, wrap), &trailing) != 8 + wrap ||
       !trailing)
        goto done;

    result = "FAIL: ATSUI did not paint a scrolling document";
    EditorDraw(editor);
    if(editor->layoutError != noErr || !ControlVisible(editor->scrollbar) ||
       editor->buffer == NULL)
        goto done;

    result = "FAIL: ATSUI scroll and resize did not preserve the reading position";
    EditorScrollLines(editor, 5);
    unsigned offset = SourceOffset(editor, editor->top);
    if(offset <= 8)
        goto done;
    Rect view = editor->view;
    view.right += 60;
    unsigned long resizedAt = TickCount();
    SizeWindow(window, WindowCanvas(window).right + 60, WindowCanvas(window).bottom, true);
    EditorResize(editor, &view, NULL);
    EditorDraw(editor);
    unsigned long resizeTicks = TickCount() - resizedAt;
    if(SourceOffset(editor, editor->top) > offset || editor->top.start != SpanStart(editor, offset))
        goto done;

    result = "FAIL: ATSUI could not draw the last page of a large file";
    ScrollTo(editor, BottomPage(editor));
    EditorDraw(editor);
    if(editor->layoutError != noErr || SourceOffset(editor, editor->top) < editor->length - 1024 ||
       GetControlValue(editor->scrollbar) != kScrollbarMax)
        goto done;

    result = "FAIL: a large-note keystroke did not repaint and preserve the edit";
    Select(editor, 2, false, false);
    Select(editor, 3, false, true);
    EventRecord key = {.what = keyDown, .message = 'V'};
    unsigned long then = TickCount();
    EditorKey(editor, &key);
    EditorDraw(editor);
    unsigned long editTicks = TickCount() - then;
    if(editor->layoutError != noErr || Source(editor)[2] != 'V' || editor->caret != 3)
        goto done;
    editor->styleAt -= kStyleDelay;
    then = TickCount();
    EditorIdle(editor);
    EditorDraw(editor);
    unsigned long styleTicks = TickCount() - then;
    long styleFreeHeap = gStyleFreeHeap;
    if(editor->layoutError != noErr || !(editor->styles[2] & kMarkdownBold))
        goto done;
    EditorCommand(editor, kUndoItem);
    unsigned longLine = editor->length - 10;
    Select(editor, longLine, false, false);
    Select(editor, longLine + 1, false, true);
    then = TickCount();
    EditorKey(editor, &key);
    EditorDraw(editor);
    unsigned long longTicks = TickCount() - then;
    if(editor->layoutError != noErr || Source(editor)[longLine] != 'V')
        goto done;
    EditorCommand(editor, kUndoItem);
    unsigned firstLong = kTextFileLimit / 2 + 16;
    Select(editor, firstLong, false, false);
    Select(editor, firstLong + 1, false, true);
    then = TickCount();
    EditorKey(editor, &key);
    EditorDraw(editor);
    unsigned long earlyTicks = TickCount() - then;
    if(editor->layoutError != noErr || Source(editor)[firstLong] != 'V')
        goto done;
    EditorCommand(editor, kUndoItem);
    result = "FAIL: switch from the large file to a short note";
    bytes = NewHandle(sizeof(shortNote) - 1);
    if(bytes == NULL)
        goto done;
    memcpy(*bytes, shortNote, sizeof(shortNote) - 1);
    shortWritten = WriteTextFile(&shortFile, bytes, NULL) == noErr;
    DisposeHandle(bytes);
    if(!shortWritten || WorkspaceOpen(editor, window, &shortFile) != noErr ||
       FrontWindow() != window || !WorkspaceHasFile(editor->workspace, &shortFile))
        goto done;

    result = "FAIL: short note unexpectedly scrolls";
    EditorActivate(editor, false);
    RGBBackColor(&editor->theme->page);
    EraseRect(&editor->view);
    EditorDraw(editor);
    if(editor->layoutError != noErr || ControlVisible(editor->scrollbar))
        goto done;
    EditorScrollLines(editor, 5);
    if(SourceOffset(editor, editor->top) != 0)
        goto done;

    result = "FAIL: expected the screen-metric gap in markdown and macbook";
    short verdana;
    GetFNum("\pVerdana", &verdana);
    short first = FirstLetterGap(editor, 0), second = FirstLetterGap(editor, 1);
    if(first < 0 || first != second || (verdana != 0 && first != 2)) {
        static char gapDiagnostic[100];
        snprintf(gapDiagnostic, sizeof(gapDiagnostic), "%s (%d and %d pixels)", result, first, second);
        result = gapDiagnostic;
        goto done;
    }
    result = "FAIL: keyboard selection or Undo did not restore the note";
    EditorActivate(editor, true);
    MoveLineEdge(editor, true, false);
    if(editor->caret != 8)
        goto done;
    MoveLineEdge(editor, false, false);
    if(editor->caret != 0)
        goto done;
    key.message = 0x1d;
    key.modifiers = shiftKey;
    EditorKey(editor, &key);
    if(editor->anchor != 0 || editor->caret != 1)
        goto done;
    key.message = 'M';
    key.modifiers = 0;
    EditorKey(editor, &key);
    if(Source(editor)[0] != 'M')
        goto done;
    EditorCommand(editor, kUndoItem);
    if(Source(editor)[0] != 'm' || editor->anchor != 0 || editor->caret != 1)
        goto done;

    result = "FAIL: Unicode paste or grapheme Backspace lost source data";
    const UniChar unicode[] = {0xd83d, 0xde00, 0x0065, 0x0301};
    if(ClearClipboard() != noErr || WriteScrap('utxt', sizeof(unicode), unicode) != noErr)
        goto done;
    EditorCommand(editor, kSelectAllItem);
    EditorCommand(editor, kPasteItem);
    if(editor->length != 4 || memcmp(Source(editor), unicode, sizeof(unicode)) != 0)
        goto done;
    EditorCommand(editor, kSelectAllItem);
    EditorCommand(editor, kCutItem);
    if(!EditorIsEmpty(editor))
        goto done;
    EditorCommand(editor, kPasteItem);
    if(editor->length != 4 || memcmp(Source(editor), unicode, sizeof(unicode)) != 0)
        goto done;
    key.message = '\b';
    EditorKey(editor, &key);
    if(editor->length != 2 || Source(editor)[1] != unicode[1])
        goto done;
    EditorKey(editor, &key);
    if(!EditorIsEmpty(editor))
        goto done;

    result = "FAIL: Return did not continue a list item";
    const UniChar list[] = {'-', ' ', 'a'};
    if(!ReplaceRange(editor, 0, 0, list, 3, 3, true))
        goto done;
    key.message = '\r';
    EditorKey(editor, &key);
    if(editor->length != 6 || Source(editor)[3] != '\r' || Source(editor)[4] != '-' || editor->caret != 6)
        goto done;

    result = CheckTypingPresentation(editor);
    if(result != NULL)
        goto done;

    unsigned long deferredTicks, pasteTicks;
    result = CheckDeferredReflow(editor, window, &deferredTicks, &pasteTicks);
    if(result != NULL)
        goto done;
    snprintf(timing, timingSize,
             "Guest ticks (60/sec): edit %lu, Markdown %lu, long line start/end %lu/%lu, resize %lu, nonconverging edit %lu, 48 KiB insertion %lu\n"
             "Sampled free app heap after open/Markdown parse: %ld/%ld KiB",
             editTicks, styleTicks, earlyTicks, longTicks, resizeTicks, deferredTicks, pasteTicks,
             openFreeHeap / 1024, styleFreeHeap / 1024);
    result = "LINEWREN_ATSUI_SMOKE_OK";
done:
    if(shortWritten)
        FSpDelete(&shortFile);
    FSpDelete(&file);
    if(editor->layoutError != noErr) {
        static char diagnostic[128];
        snprintf(diagnostic, sizeof(diagnostic), "%s (ATSUI %ld, free heap %ld)",
                 result, (long)editor->layoutError, (long)FreeMem());
        return diagnostic;
    }
    return result;
}
#endif
