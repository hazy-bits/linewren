#ifndef LINEWREN_CLASSIC_SPELLING_H
#define LINEWREN_CLASSIC_SPELLING_H

#include <stdbool.h>
#include <stdint.h>

enum { kSpellingWordSize = 100, kSpellingSuggestions = 6 };

typedef enum { kSpellingEnglish, kSpellingRussian } ClassicSpellingLanguage;

#ifdef __cplusplus
extern "C" {
#endif
bool ClassicSpellingLoad(ClassicSpellingLanguage language);
void ClassicSpellingUnload(void);
bool ClassicSpellingAccepts(const char *word);
unsigned ClassicSpellingSuggest(const char *word, char suggestions[][kSpellingWordSize]);
#ifdef __cplusplus
}
#endif

/* Iterate over prose while keeping UTF-16 offsets for drawing and editing. */
bool ClassicSpellingWord(ClassicSpellingLanguage language, const uint16_t *text,
                         unsigned length, unsigned *offset,
                         unsigned *start, char word[kSpellingWordSize]);

uint16_t ClassicSpellingDecode(ClassicSpellingLanguage language, unsigned char byte);

#endif
