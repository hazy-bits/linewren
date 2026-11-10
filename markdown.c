#include "markdown.h"
#include "vendor/md4c/md4c.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    const uint16_t *text;
    unsigned length;
    uint16_t *styles;
    unsigned heading, tableHeader, quote, bold, italic, link, strike;
    MD_ATTRIBUTE href;
    uint16_t *url;
    unsigned click, capacity, urlLength;
    MarkdownHeading *headings;
    unsigned headingCount, headingCapacity;
    bool collectHeadings, headingListed;
} Styling;

static void MarkSource(MD_SOURCE_TYPE kind, MD_OFFSET start, MD_OFFSET end,
                       unsigned level, void *data)
{
    Styling *state = data;
    if(state->styles == NULL)
        return;
    unsigned flags = kMarkdownMarker | level;
    if(kind == MD_SOURCE_LIST)
        flags |= kMarkdownList;
    if(kind == MD_SOURCE_TASK)
        flags |= kMarkdownTask;
    for(unsigned i = start; i < end && i < state->length; ++i)
        state->styles[i] |= flags;
}

static void ChangeBlock(MD_BLOCKTYPE kind, void *detail, Styling *state, int delta)
{
    switch(kind) {
        case MD_BLOCK_H:
            state->heading = delta > 0 ? ((MD_BLOCK_H_DETAIL *)detail)->level : 0;
            state->headingListed = false;
            break;
        case MD_BLOCK_TH: state->tableHeader += delta; break;
        case MD_BLOCK_QUOTE: state->quote += delta; break;
        default: break;
    }
}

static int EnterBlock(MD_BLOCKTYPE kind, void *detail, void *data)
{
    ChangeBlock(kind, detail, data, 1);
    return 0;
}

static int LeaveBlock(MD_BLOCKTYPE kind, void *detail, void *data)
{
    ChangeBlock(kind, detail, data, -1);
    return 0;
}

static void ChangeSpan(MD_SPANTYPE kind, Styling *state, int delta)
{
    switch(kind) {
        case MD_SPAN_STRONG: state->bold += delta; break;
        case MD_SPAN_EM: state->italic += delta; break;
        case MD_SPAN_A:
        case MD_SPAN_IMG: state->link += delta; break;
        case MD_SPAN_DEL: state->strike += delta; break;
        default: break;
    }
}

static int EnterSpan(MD_SPANTYPE kind, void *detail, void *data)
{
    Styling *state = data;
    if(kind == MD_SPAN_A)
        state->href = ((MD_SPAN_A_DETAIL *)detail)->href;
    ChangeSpan(kind, state, 1);
    return 0;
}

static int LeaveSpan(MD_SPANTYPE kind, void *detail, void *data)
{
    (void)detail;
    Styling *state = data;
    if(kind == MD_SPAN_A)
        state->href = (MD_ATTRIBUTE){0};
    ChangeSpan(kind, state, -1);
    return 0;
}

static void CollectHeading(Styling *state, unsigned start, unsigned end)
{
    if(state->headingListed) {
        MarkdownHeading *heading = &state->headings[state->headingCount - 1];
        if(end > heading->end)
            heading->end = end;
        return;
    }
    if(state->headingCount == state->headingCapacity) {
        unsigned capacity = state->headingCapacity ? state->headingCapacity * 2 : 16;
        MarkdownHeading *headings = realloc(state->headings, capacity * sizeof(*headings));
        if(headings == NULL) {
            state->collectHeadings = false;
            return;
        }
        state->headings = headings;
        state->headingCapacity = capacity;
    }
    state->headings[state->headingCount++] = (MarkdownHeading){start, end, state->heading};
    state->headingListed = true;
}

static int StyleText(MD_TEXTTYPE kind, const MD_CHAR *text, MD_SIZE length, void *data)
{
    Styling *state = data;
    /* MD4C also emits synthetic spaces and line breaks from its own buffers. */
    uintptr_t source = (uintptr_t)state->text, address = (uintptr_t)text;
    if(address < source || (address - source) / sizeof(*text) >= state->length)
        return 0;
    unsigned start = (address - source) / sizeof(*text);
    if(length > state->length - start)
        return 0;
    if(state->styles == NULL) {
        if(state->click >= start && state->click - start < length &&
           state->href.size != 0 && state->href.size <= state->capacity) {
            state->urlLength = state->href.size;
            memcpy(state->url, state->href.text, state->urlLength * sizeof(*state->url));
        }
        return 0;
    }

    if(state->collectHeadings && state->heading != 0 && length != 0)
        CollectHeading(state, start, start + length);
    unsigned flags = state->heading;
    if(state->bold || state->heading || state->tableHeader)
        flags |= kMarkdownBold;
    if(state->italic)
        flags |= kMarkdownItalic;
    if(state->link)
        flags |= kMarkdownLink;
    if(kind == MD_TEXT_CODE)
        flags |= kMarkdownCode;
    if(kind == MD_TEXT_HTML || state->strike || state->quote)
        flags |= kMarkdownMuted;
    for(unsigned i = start; i < start + length; ++i)
        state->styles[i] |= flags;
    return 0;
}

static int ParseMarkdown(Styling *state)
{
    static const MD_PARSER parser = {
        .flags = MD_FLAG_PERMISSIVEAUTOLINKS | MD_FLAG_TABLES |
                 MD_FLAG_TASKLISTS | MD_FLAG_STRIKETHROUGH | MD_FLAG_LINEWREN_EDIT_LISTS,
        .enter_block = EnterBlock, .leave_block = LeaveBlock,
        .enter_span = EnterSpan, .leave_span = LeaveSpan,
        .text = StyleText, .source = MarkSource
    };
    return md_parse(state->text, state->length, &parser, state);
}

uint16_t *MarkdownStylesWithOutline(const uint16_t *text, unsigned length,
                                    MarkdownHeading **headings, unsigned *count)
{
    uint16_t *styles = calloc(length, sizeof(*styles));
    Styling state = {.text = text, .length = length, .styles = styles,
                     .collectHeadings = headings != NULL};
    if(styles != NULL && ParseMarkdown(&state) != 0) {
        free(state.headings);
        state.headings = NULL;
        state.headingCount = 0;
        free(styles);
        styles = NULL;
    }
    if(headings != NULL) {
        *headings = state.headings;
        *count = state.headingCount;
    }
    return styles;
}

uint16_t *MarkdownStyles(const uint16_t *text, unsigned length)
{
    return MarkdownStylesWithOutline(text, length, NULL, NULL);
}

unsigned MarkdownLinkAt(const uint16_t *text, unsigned length, unsigned at,
                        uint16_t *url, unsigned capacity)
{
    Styling state = {.text = text, .length = length, .click = at,
                     .url = url, .capacity = capacity};
    return ParseMarkdown(&state) == 0 ? state.urlLength : 0;
}

typedef struct {
    unsigned line, mark, markEnd, content, column, contentColumn;
    unsigned number;
    bool ordered, task;
} ListItem;

static bool Blank(uint16_t ch) { return ch == ' ' || ch == '\t'; }
static bool Break(uint16_t ch) { return ch == '\r' || ch == '\n'; }
static unsigned NextColumn(unsigned column, uint16_t ch)
{
    return ch == '\t' ? (column + 4) & ~3u : column + 1;
}

/* Keep the editing operation local to one item line. The parser decides
   whether a prefix is a list marker (including when inside code or HTML). */
static bool ReadItem(const uint16_t *text, const uint16_t *styles, unsigned line,
                     unsigned end, ListItem *item)
{
    unsigned mark = line, column = 0;
    while(mark < end && Blank(text[mark])) {
        column = NextColumn(column, text[mark]);
        ++mark;
    }
    if(mark == end || !(styles[mark] & kMarkdownList))
        return false;
    unsigned pos = mark;
    unsigned number = 0;
    while(pos < end && text[pos] >= '0' && text[pos] <= '9')
        number = number * 10 + text[pos++] - '0';
    bool ordered = pos != mark;
    unsigned markEnd = pos + 1, contentColumn = column + markEnd - mark;
    pos = markEnd;
    while(pos < end && Blank(text[pos]))
        contentColumn = NextColumn(contentColumn, text[pos++]);
    if(pos == markEnd || pos == end || contentColumn - column - (markEnd - mark) > 4)
        contentColumn = column + markEnd - mark + 1;
    bool task = end - pos >= 3 && (styles[pos] & kMarkdownTask);
    if(task) {
        pos += 3;
        while(pos < end && Blank(text[pos]))
            ++pos;
    }
    *item = (ListItem){line, mark, markEnd, pos, column, contentColumn, number, ordered, task};
    return true;
}

bool MarkdownToggleTask(const uint16_t *text, const uint16_t *styles, unsigned length,
                        unsigned at, MarkdownEdit *edit)
{
    if(at >= length || !(styles[at] & kMarkdownTask))
        return false;
    unsigned mark = at;
    while(mark > 0 && at - mark < 2 && text[mark] != '[')
        --mark;
    if(length - mark < 3 || text[mark] != '[' || text[mark + 2] != ']')
        return false;
    *edit = (MarkdownEdit){.start = mark + 1, .end = mark + 2, .length = 1,
                           .text = {text[mark + 1] == ' ' ? 'x' : ' '}};
    return true;
}

bool MarkdownPrefixEdit(const uint16_t *text, const uint16_t *styles, unsigned length,
                        unsigned caret, MarkdownPrefix prefix, MarkdownEdit *edit)
{
    static const uint16_t *const markers[] = {u"# ", u"> ", u"- ", u"- [ ] "};
    unsigned line = caret, end = caret;
    while(line > 0 && !Break(text[line - 1]))
        --line;
    while(end < length && !Break(text[end]))
        ++end;
    unsigned start = line;
    while(start < end && Blank(text[start]))
        ++start;
    *edit = (MarkdownEdit){.start = start, .end = start};
    ListItem item;
    if(start < end && (styles[start] & kMarkdownCode))
        return false;
    if((prefix == kListPrefix || prefix == kTaskPrefix) &&
       ReadItem(text, styles, line, end, &item)) {
        if(prefix == kListPrefix) {
            edit->end = item.content;
        } else {
            start = item.markEnd;
            while(start < end && Blank(text[start]))
                ++start;
            edit->start = start;
            edit->end = item.task ? item.content : start;
            if(!item.task) {
                const uint16_t *marker = start == item.markEnd ? u" [ ] " : u"[ ] ";
                while(*marker)
                    edit->text[edit->length++] = *marker++;
            }
        }
    } else if(prefix == kQuotePrefix && start < end && text[start] == '>') {
        edit->end = start + 1;
        if(edit->end < end && Blank(text[edit->end]))
            ++edit->end;
    } else if(prefix == kHeadingPrefix && start < end && text[start] == '#' &&
              (styles[start] & (kMarkdownHeadingMask | kMarkdownMarker)) != 0) {
        while(edit->end < end && text[edit->end] == '#')
            ++edit->end;
        while(edit->end < end && Blank(text[edit->end]))
            ++edit->end;
    } else {
        const uint16_t *marker = markers[prefix];
        while(*marker)
            edit->text[edit->length++] = *marker++;
    }
    return true;
}

static bool ContinueItem(const uint16_t *text, const ListItem *item, unsigned end,
                         unsigned caret, MarkdownEdit *edit)
{
    if(caret < item->markEnd)
        return false;
    if(item->content == end) {
        edit->start = item->mark;
        edit->end = end;
        edit->caret = item->mark;
        return true;
    }
    if(caret < item->content)
        return false;
    unsigned indent = item->mark - item->line;
    if(indent + 16 > sizeof(edit->text) / sizeof(edit->text[0]))
        return false;
    edit->start = edit->end = caret;
    edit->text[edit->length++] = '\r';
    memcpy(edit->text + edit->length, text + item->line, indent * sizeof(*text));
    edit->length += indent;
    if(item->ordered) {
        unsigned number = item->number == 999999999 ? item->number : item->number + 1;
        unsigned divisor = 1;
        while(number / divisor >= 10)
            divisor *= 10;
        for(; divisor != 0; divisor /= 10)
            edit->text[edit->length++] = '0' + number / divisor % 10;
    }
    edit->text[edit->length++] = text[item->markEnd - 1];
    edit->text[edit->length++] = ' ';
    if(item->task) {
        edit->text[edit->length++] = '[';
        edit->text[edit->length++] = ' ';
        edit->text[edit->length++] = ']';
        edit->text[edit->length++] = ' ';
    }
    edit->caret = caret + edit->length;
    return true;
}

static unsigned LineStart(const uint16_t *text, unsigned offset)
{
    while(offset > 0 && !Break(text[offset - 1]))
        --offset;
    return offset;
}

static bool ChangeIndent(const uint16_t *text, const uint16_t *styles, const ListItem *item,
                         unsigned caret, bool indent, MarkdownEdit *edit)
{
    unsigned target = 0, line = item->line;
    /* Nest under the preceding sibling, using its content column. Outdent to
       the preceding parent's marker column (usually 2 or 3 spaces away). */
    while(line > 0) {
        unsigned end = line - 1;
        if(end > 0 && text[end] == '\n' && text[end - 1] == '\r')
            --end;
        line = LineStart(text, end);
        ListItem previous;
        if(!ReadItem(text, styles, line, end, &previous)) {
            if(line < end && !Blank(text[line]))
                break;
            continue;
        }
        if(indent && previous.column == item->column) {
            target = previous.contentColumn;
            break;
        }
        if(previous.column < item->column) {
            if(!indent)
                target = previous.column;
            break;
        }
    }
    if((indent && target <= item->column) || (!indent && item->column == 0) ||
       target > sizeof(edit->text) / sizeof(edit->text[0]))
        return false;
    edit->start = item->line;
    edit->end = item->mark;
    edit->length = target;
    edit->caret = item->line + target + (caret >= item->mark ? caret - item->mark : 0);
    for(unsigned i = 0; i < target; ++i)
        edit->text[i] = ' ';
    /* A nested ordered list must start at 1 to interrupt its parent's text. */
    if(indent && item->ordered && item->number != 1 &&
       target + 1 < sizeof(edit->text) / sizeof(edit->text[0])) {
        unsigned digits = item->markEnd - item->mark - 1;
        edit->end += digits;
        edit->text[edit->length++] = '1';
        if(caret > item->mark)
            edit->caret = caret < edit->end ? item->line + target + 1
                                           : edit->caret - digits + 1;
    }
    return true;
}

bool MarkdownListEdit(const uint16_t *text, unsigned length, unsigned caret,
                      MarkdownListAction action, MarkdownEdit *edit)
{
    unsigned line = LineStart(text, caret), end = caret;
    while(end < length && !Break(text[end]))
        ++end;
    /* Ordinary Return and Tab need no parser work when the line cannot be a list. */
    unsigned mark = line;
    while(mark < end && Blank(text[mark]))
        ++mark;
    if(mark == end || (text[mark] != '-' && text[mark] != '+' && text[mark] != '*' &&
                      !(text[mark] >= '0' && text[mark] <= '9')))
        return false;
    uint16_t *styles = MarkdownStyles(text, length);
    if(styles == NULL)
        return false;
    ListItem item;
    bool handled = false;
    if(ReadItem(text, styles, line, end, &item)) {
        edit->length = 0;
        handled = action == kContinueList
            ? ContinueItem(text, &item, end, caret, edit)
            : ChangeIndent(text, styles, &item, caret, action == kIndentList, edit);
        /* A Tab with no valid sibling is a no-op, rather than turning a list
           marker into an indented code block. */
        if(!handled && action != kContinueList) {
            edit->start = edit->end = edit->caret = caret;
            handled = true;
        }
    }
    free(styles);
    return handled;
}
