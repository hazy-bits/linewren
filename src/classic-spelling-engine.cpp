#include "classic-spelling.h"
#include "myspell.hxx"

#include <Memory.h>
#include <Resources.h>
#include <cstdlib>
#include <cstring>
#include <new>

static MySpell *checker;
static ClassicSpellingLanguage loadedLanguage;

bool ClassicSpellingLoad(ClassicSpellingLanguage language)
{
    if(checker != nullptr && loadedLanguage == language)
        return true;
    ClassicSpellingUnload();

    /* Include the resource handles and temporary allocations in the budget. */
    if(FreeMem() < (language == kSpellingRussian ? 6L : 5L) * 1024 * 1024)
        return false;

    short id = language == kSpellingRussian ? 130 : 128;
    Handle aff = Get1Resource('Spel', id);
    Handle dic = Get1Resource('Spel', id + 1);
    FILE *rules = nullptr;
    FILE *words = nullptr;
    if(aff != nullptr && dic != nullptr) {
        HLock(aff);
        HLock(dic);
        rules = fmemopen(*aff, GetHandleSize(aff), "r");
        words = fmemopen(*dic, GetHandleSize(dic), "r");
        if(rules != nullptr && words != nullptr)
            checker = new(std::nothrow) MySpell(rules, words);
    }
    if(words != nullptr)
        fclose(words);
    if(rules != nullptr)
        fclose(rules);
    if(dic != nullptr)
        ReleaseResource(dic);
    if(aff != nullptr)
        ReleaseResource(aff);
    if(checker != nullptr && !checker->loaded())
        ClassicSpellingUnload();
    loadedLanguage = language;
    return checker != nullptr;
}

void ClassicSpellingUnload(void)
{
    delete checker;
    checker = nullptr;
}

bool ClassicSpellingAccepts(const char *word)
{
    return checker != nullptr && checker->spell(word) != 0;
}

unsigned ClassicSpellingSuggest(const char *word, char suggestions[][kSpellingWordSize])
{
    if(checker == nullptr)
        return 0;
    char **choices = nullptr;
    int count = checker->suggest(&choices, word);
    unsigned used = 0;
    for(int i = 0; i < count; ++i) {
        size_t length = strlen(choices[i]);
        if(used < kSpellingSuggestions && length < kSpellingWordSize)
            memcpy(suggestions[used++], choices[i], length + 1);
        free(choices[i]);
    }
    free(choices);
    return used;
}
