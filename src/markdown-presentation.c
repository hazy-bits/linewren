#include "markdown-presentation.h"

short MarkdownPointSize(unsigned heading, short bodyPoints)
{
    static const short adjustment[] = {0, kTitlePoints - kBodyPoints, 3, 2, 1, 0, -1};
    return bodyPoints + adjustment[heading];
}

MarkdownPresentation MarkdownDisplay(unsigned style, const EditorTheme *theme, short bodyPoints)
{
    MarkdownPresentation display = {
        normal, MarkdownPointSize(style & kMarkdownHeadingMask, bodyPoints), theme->text
    };
    if(style & kMarkdownMarker) {
        display.color = theme->marker;
        if(style & kMarkdownHeadingMask)
            display.face = condense;
    } else {
        if(style & kMarkdownBold)
            display.face |= bold;
        if(style & kMarkdownItalic)
            display.face |= italic;
        if(style & kMarkdownLink)
            display.face |= underline;
        if(style & kMarkdownCode)
            display.color = theme->page.red < 0x8000
                ? (RGBColor){0xcdcd, 0xbcbc, 0xa4a4} : (RGBColor){0x6868, 0x4545, 0x3535};
        else if(style & kMarkdownMuted)
            display.color = (RGBColor){
                (3u * theme->text.red + theme->marker.red) / 4,
                (3u * theme->text.green + theme->marker.green) / 4,
                (3u * theme->text.blue + theme->marker.blue) / 4
            };
    }
    return display;
}
