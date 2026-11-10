#include "document.h"

#include <string.h>

static unsigned Fold(unsigned ch)
{
    return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
}

static int HasExtension(const unsigned char *name, size_t length, const char *extension)
{
    size_t suffix = strlen(extension);
    if(length <= suffix || name[length - suffix - 1] != '.')
        return 0;
    for(size_t i = 0; i < suffix; ++i)
        if(Fold(name[length - suffix + i]) != (unsigned char)extension[i])
            return 0;
    return 1;
}

DocumentKind DocumentKindForName(const unsigned char *name, size_t length, uint32_t finderType)
{
    static const char *const markdown[] = {"md", "markdown", "mdown", "mkd"};
    static const char *const text[] = {
        "txt", "text", "log", "csv", "tsv", "json", "xml", "html", "htm",
        "yaml", "yml", "toml", "ini", "cfg", "conf", "css", "js", "c", "h",
        "cc", "cpp", "m", "mm", "py", "rb", "sh", "pl", "php", "java",
        "rs", "go", "swift", "tex", "rst", "org"
    };
    for(size_t i = 0; i < sizeof(markdown) / sizeof(*markdown); ++i)
        if(HasExtension(name, length, markdown[i]))
            return kDocumentMarkdown;
    if(finderType == UINT32_C(0x54455854)) /* 'TEXT' */
        return kDocumentText;
    for(size_t i = 0; i < sizeof(text) / sizeof(*text); ++i)
        if(HasExtension(name, length, text[i]))
            return kDocumentText;
    return kDocumentUnsupported;
}

int DocumentNameMatches(const unsigned char *name, size_t length,
                        const unsigned char *query, size_t queryLength)
{
    if(queryLength == 0)
        return 1;
    if(queryLength > length)
        return 0;
    for(size_t start = 0; start + queryLength <= length; ++start) {
        size_t i = 0;
        while(i < queryLength && Fold(name[start + i]) == Fold(query[i]))
            ++i;
        if(i == queryLength)
            return 1;
    }
    return 0;
}

int DocumentMatchesAt(DocumentQuery query, size_t offset)
{
    if(query.queryLength == 0 || offset > query.length || query.queryLength > query.length - offset)
        return 0;
    for(size_t i = 0; i < query.queryLength; ++i)
        if(Fold(query.text[offset + i]) != Fold(query.query[i]))
            return 0;
    return 1;
}

size_t DocumentNextMatch(DocumentQuery query, size_t offset)
{
    if(query.queryLength != 0 && query.queryLength <= query.length)
        for(size_t at = offset; at <= query.length - query.queryLength; ++at)
            if(DocumentMatchesAt(query, at))
                return at;
    return query.length;
}

DocumentMatch DocumentFind(DocumentQuery query, size_t offset, int backward)
{
    DocumentMatch match = {query.length, 0, 0};
    size_t first = query.length, last = query.length;
    for(size_t at = DocumentNextMatch(query, 0); at < query.length;
        at = DocumentNextMatch(query, at + query.queryLength)) {
        if(++match.count == 1)
            first = at;
        last = at;
        if(backward ? at < offset : match.ordinal == 0 && at >= offset) {
            match.offset = at;
            match.ordinal = match.count;
        }
    }
    if(match.count != 0 && match.ordinal == 0) {
        match.offset = backward ? last : first;
        match.ordinal = backward ? match.count : 1;
    }
    return match;
}

int DocumentIsSpace(uint16_t ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' || ch == 0x00a0 ||
           ch == 0x1680 || (ch >= 0x2000 && ch <= 0x200a) || ch == 0x2028 ||
           ch == 0x2029 || ch == 0x202f || ch == 0x205f || ch == 0x3000;
}

size_t DocumentWordCount(const uint16_t *text, size_t length)
{
    return DocumentWordCountStyled(text, NULL, length, 0);
}

size_t DocumentCharacterCount(const uint16_t *text, size_t length)
{
    size_t count = length;
    for(size_t i = 1; i < length; ++i)
        if(text[i - 1] >= 0xd800 && text[i - 1] <= 0xdbff &&
           text[i] >= 0xdc00 && text[i] <= 0xdfff)
            --count;
    return count;
}

size_t DocumentWordCountStyled(const uint16_t *text, const uint16_t *styles,
                               size_t length, uint16_t ignored)
{
    size_t words = 0;
    int inside = 0;
    for(size_t i = 0; i < length; ++i) {
        if(DocumentIsSpace(text[i]) || (styles != NULL && (styles[i] & ignored) != 0)) {
            inside = 0;
        } else if(!inside) {
            ++words;
            inside = 1;
        }
    }
    return words;
}
