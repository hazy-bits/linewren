#include "document.h"
#include "markdown.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static DocumentKind Kind(const char *name, unsigned type)
{
    const unsigned char *bytes = (const unsigned char *)name;
    unsigned length = 0;
    while(bytes[length] != 0)
        ++length;
    return DocumentKindForName(bytes, length, type);
}

static void CheckMarkdownActions(void)
{
    static const uint16_t note[] = u"# Heading\r\r- [ ] Task\r- Plain";
    unsigned length = sizeof(note) / sizeof(*note) - 1;
    uint16_t *styles = MarkdownStyles(note, length);
    assert(styles != NULL);
    MarkdownEdit edit;
    assert(MarkdownPrefixEdit(note, styles, length, 3, kHeadingPrefix, &edit));
    assert(edit.start == 0 && edit.end == 2 && edit.length == 0);
    assert(MarkdownToggleTask(note, styles, length, 15, &edit));
    assert(edit.start == 14 && edit.end == 15 && edit.text[0] == 'x');
    assert(!MarkdownToggleTask(note, styles, length, 17, &edit));
    assert(MarkdownPrefixEdit(note, styles, length, 17, kTaskPrefix, &edit));
    assert(edit.start == 13 && edit.end == 17 && edit.length == 0);
    assert(MarkdownPrefixEdit(note, styles, length, 25, kTaskPrefix, &edit));
    assert(edit.start == 24 && edit.end == 24 && edit.length == 4);
    free(styles);

    static const uint16_t links[] = u"[label][ref]\r\r[ref]: https://example.com\r\r`https://example.net`";
    static const uint16_t expected[] = u"https://example.com";
    uint16_t url[64];
    length = sizeof(links) / sizeof(*links) - 1;
    assert(MarkdownLinkAt(links, length, 2, url, 64) == sizeof(expected) / sizeof(*expected) - 1);
    assert(memcmp(url, expected, sizeof(expected) - sizeof(*expected)) == 0);
    assert(MarkdownLinkAt(links, length, length - 3, url, 64) == 0);
    assert(MarkdownLinkAt(links, length, 2, url, 4) == 0);
}

static void CheckDocumentSearch(void)
{
    static const uint16_t text[] = u"aAaA\rAa";
    static const uint16_t needle[] = u"Aa";
    DocumentQuery query = {text, needle, 7, 2};
    DocumentMatch match = DocumentFind(query, 0, 0);
    assert(match.count == 3 && match.ordinal == 1 && match.offset == 0);
    match = DocumentFind(query, 1, 0);
    assert(match.count == 3 && match.ordinal == 2 && match.offset == 2);
    match = DocumentFind(query, 7, 0);
    assert(match.ordinal == 1 && match.offset == 0);
    match = DocumentFind(query, 5, 1);
    assert(match.ordinal == 2 && match.offset == 2);
    match = DocumentFind(query, 0, 1);
    assert(match.ordinal == 3 && match.offset == 5);
    assert(DocumentNextMatch(query, 3) == 5);
    assert(DocumentNextMatch(query, SIZE_MAX) == query.length);
    assert(DocumentMatchesAt(query, 1)); /* Explicit selections may overlap. */
    assert(!DocumentMatchesAt(query, 6));
    query.queryLength = 0;
    match = DocumentFind(query, 0, 0);
    assert(match.count == 0 && match.ordinal == 0 && match.offset == query.length);
    query = (DocumentQuery){u"Éé", u"é", 2, 1};
    assert(DocumentNextMatch(query, 0) == 1);
}

static void CheckOutline(void)
{
    static const uint16_t note[] =
        u"# First\r\r```\r# ignored\r```\r\rSecond *heading*\r------\r\r### Third";
    MarkdownHeading *headings;
    unsigned count;
    uint16_t *styles = MarkdownStylesWithOutline(note, sizeof(note) / sizeof(*note) - 1,
                                                 &headings, &count);
    assert(styles != NULL && count == 3);
    assert(headings[0].start == 2 && headings[0].level == 1 && headings[0].end == 7);
    assert(headings[1].level == 2 && note[headings[1].start] == 'S' &&
           note[headings[1].end - 1] == 'g');
    assert(headings[2].level == 3 && note[headings[2].start] == 'T');
    free(headings);
    free(styles);
}

int main(void)
{
    CheckMarkdownActions();
    CheckDocumentSearch();
    CheckOutline();
    assert(Kind("note.md", 0) == kDocumentMarkdown);
    assert(Kind("NOTE.MARKDOWN", 0) == kDocumentMarkdown);
    assert(Kind("draft.mdown", 0) == kDocumentMarkdown);
    assert(Kind("plain.txt", 0) == kDocumentText);
    assert(Kind("Source.C", 0) == kDocumentText);
    assert(Kind("Read Me", 0x54455854) == kDocumentText);
    assert(Kind("picture.jpg", 0) == kDocumentUnsupported);
    assert(Kind("not-md", 0) == kDocumentUnsupported);
    assert(DocumentNameMatches((const unsigned char *)"Long Note.md", 12,
                               (const unsigned char *)"note", 4));
    assert(!DocumentNameMatches((const unsigned char *)"Long Note.md", 12,
                                (const unsigned char *)"draft", 5));

    static const uint16_t empty[] = u" \t\r\n";
    static const uint16_t prose[] = u"one  two\rthree\tfour";
    static const uint16_t unicode[] = u"na\u00efve caf\u00e9 \u041c\u0430\u0440\u043a\u043e";
    static const uint16_t heading[] = u"# Heading";
    static const uint16_t headingStyles[] = {1, 0, 0, 0, 0, 0, 0, 0, 0};
    static const uint16_t characters[] = u"A\U0001f642\r\u00e9";
    assert(DocumentCharacterCount(characters, 0) == 0);
    assert(DocumentCharacterCount(characters, sizeof(characters) / sizeof(*characters) - 1) == 4);
    assert(DocumentWordCount(empty, sizeof(empty) / sizeof(*empty) - 1) == 0);
    assert(DocumentWordCount(prose, sizeof(prose) / sizeof(*prose) - 1) == 4);
    assert(DocumentWordCount(unicode, sizeof(unicode) / sizeof(*unicode) - 1) == 3);
    assert(DocumentWordCountStyled(heading, headingStyles,
                                   sizeof(heading) / sizeof(*heading) - 1, 1) == 1);
    return 0;
}
