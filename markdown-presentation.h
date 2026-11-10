#ifndef LINEWREN_MARKDOWN_PRESENTATION_H
#define LINEWREN_MARKDOWN_PRESENTATION_H

#include "editor.h"
#include "markdown.h"

enum {
    kBodyPoints = 13,
    kTitlePoints = 18,
    kMarkdownDisplayMask = kMarkdownList - 1
};

typedef struct {
    Style face;
    short points;
    RGBColor color;
} MarkdownPresentation;

short MarkdownPointSize(unsigned heading, short bodyPoints);
MarkdownPresentation MarkdownDisplay(unsigned style, const EditorTheme *theme, short bodyPoints);

#endif
