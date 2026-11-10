#ifndef LINEWREN_DOCUMENT_H
#define LINEWREN_DOCUMENT_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    kDocumentUnsupported,
    kDocumentText,
    kDocumentMarkdown
} DocumentKind;

/* Names are byte strings so this policy stays testable on the host. */
DocumentKind DocumentKindForName(const unsigned char *name, size_t length, uint32_t finderType);
int DocumentNameMatches(const unsigned char *name, size_t length,
                        const unsigned char *query, size_t queryLength);

/* Case-insensitive for ASCII; exact for other UTF-16 units. */
typedef struct {
    const uint16_t *text, *query;
    size_t length, queryLength;
} DocumentQuery;

typedef struct {
    size_t offset, ordinal, count;
} DocumentMatch;

int DocumentMatchesAt(DocumentQuery query, size_t offset);
/* Returns query.length when no match exists at or after offset. */
size_t DocumentNextMatch(DocumentQuery query, size_t offset);
/* Selects among non-overlapping matches, wrapping at either end.
   Forward is inclusive of offset; backward is exclusive. Ordinals start at 1. */
DocumentMatch DocumentFind(DocumentQuery query, size_t offset, int backward);

int DocumentIsSpace(uint16_t ch);
size_t DocumentWordCount(const uint16_t *text, size_t length);
size_t DocumentCharacterCount(const uint16_t *text, size_t length);
size_t DocumentWordCountStyled(const uint16_t *text, const uint16_t *styles,
                               size_t length, uint16_t ignored);

#endif
