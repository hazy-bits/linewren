#ifndef LINEWREN_SPELLING_H
#define LINEWREN_SPELLING_H

#include <CFArray.h>
#include <CFString.h>
#include <Files.h>

Boolean SpellingAvailable(void);
CFStringRef SpellingDefaultLanguage(void);
CFStringRef SpellingCopyFileLanguage(const FSSpec *file);
Boolean SpellingSaveFileLanguage(const FSSpec *file, CFStringRef language);
CFArrayRef SpellingCopyLanguages(void);
Boolean SpellingLanguageSupported(CFStringRef language);
OSStatus SpellingNext(CFStringRef text, CFIndex offset, CFStringRef language, CFRange *match);
CFArrayRef SpellingSuggestions(CFStringRef word, CFStringRef language);

#endif
