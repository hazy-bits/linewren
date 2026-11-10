#ifndef LINEWREN_MARKDOWN_H
#define LINEWREN_MARKDOWN_H

#include <stdbool.h>
#include <stdint.h>

enum {
    kMarkdownHeadingMask = 7,
    kMarkdownBold = 1 << 3,
    kMarkdownItalic = 1 << 4,
    kMarkdownLink = 1 << 5,
    kMarkdownCode = 1 << 6,
    kMarkdownMuted = 1 << 7,
    kMarkdownMarker = 1 << 8,
    kMarkdownList = 1 << 9,
    kMarkdownTask = 1 << 10
};

/* One style per UTF-16 unit. Caller frees it. */
uint16_t *MarkdownStyles(const uint16_t *text, unsigned length);
typedef struct {
    unsigned start, end, level;
} MarkdownHeading;

/* Optional outline from the same parse. Caller frees both arrays. */
uint16_t *MarkdownStylesWithOutline(const uint16_t *text, unsigned length,
                                    MarkdownHeading **headings, unsigned *count);

typedef enum { kContinueList, kIndentList, kOutdentList } MarkdownListAction;

typedef struct {
    unsigned start, end, caret, length;
    uint16_t text[128];
} MarkdownEdit;

bool MarkdownListEdit(const uint16_t *text, unsigned length, unsigned caret,
                      MarkdownListAction action, MarkdownEdit *edit);

typedef enum { kHeadingPrefix, kQuotePrefix, kListPrefix, kTaskPrefix } MarkdownPrefix;
bool MarkdownPrefixEdit(const uint16_t *text, const uint16_t *styles, unsigned length,
                        unsigned caret, MarkdownPrefix prefix, MarkdownEdit *edit);
bool MarkdownToggleTask(const uint16_t *text, const uint16_t *styles, unsigned length,
                        unsigned at, MarkdownEdit *edit);
unsigned MarkdownLinkAt(const uint16_t *text, unsigned length, unsigned at,
                        uint16_t *url, unsigned capacity);

#endif
