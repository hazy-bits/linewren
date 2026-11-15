#include "classic-spelling.h"

/* KOI8-R orders Cyrillic letters differently from Unicode. */
static const uint16_t koi8Letters[] = u"юабцдефгхийклмнопярстужвьызшэщчъ";

uint16_t ClassicSpellingDecode(ClassicSpellingLanguage language, unsigned char byte)
{
    if(language != kSpellingRussian || byte < 0x80)
        return byte;
    if(byte >= 0xc0)
        return koi8Letters[byte & 31] - (byte >= 0xe0 ? 32 : 0);
    return byte == 0xa3 ? 0x0451 : byte == 0xb3 ? 0x0401 : 0xfffd;
}

static unsigned RussianLetter(uint16_t ch)
{
    if(ch == 0x0451 || ch == 0x0401)
        return ch == 0x0451 ? 0xa3 : 0xb3;
    if(ch < 0x0410 || ch > 0x044f)
        return 0;
    bool upper = ch < 0x0430;
    uint16_t lower = upper ? ch + 32 : ch;
    for(unsigned i = 0; i < 32; ++i)
        if(koi8Letters[i] == lower)
            return (upper ? 0xe0 : 0xc0) + i;
    return 0;
}

static bool Letter(uint16_t ch)
{
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
           (ch >= 0xc0 && ch <= 0xff && ch != 0xd7 && ch != 0xf7);
}

static bool Separator(uint16_t ch)
{
    return ch == 0x00a0 || ch == 0x1680 || (ch >= 0x2000 && ch <= 0x206f && ch != 0x2019) ||
           ch == 0x3000 || (ch < 0x80 && !Letter(ch) &&
                            !(ch >= '0' && ch <= '9') && ch != '\'' && ch != '_');
}

bool ClassicSpellingWord(ClassicSpellingLanguage language, const uint16_t *text,
                         unsigned length, unsigned *offset,
                         unsigned *start, char word[kSpellingWordSize])
{
    while(*offset < length) {
        while(*offset < length && Separator(text[*offset]))
            ++*offset;
        unsigned first = *offset;
        while(*offset < length && !Separator(text[*offset]))
            ++*offset;
        unsigned end = *offset;
        while(first < end && (text[first] == '\'' || text[first] == 0x2019))
            ++first;
        while(end > first && (text[end - 1] == '\'' || text[end - 1] == 0x2019))
            --end;
        unsigned count = end - first;
        if(count == 0 || count >= kSpellingWordSize)
            continue;
        unsigned i = 0;
        for(; i < count; ++i) {
            uint16_t ch = text[first + i];
            if(ch == 0x2019)
                ch = '\'';
            unsigned byte = language == kSpellingRussian ? RussianLetter(ch) :
                (Letter(ch) || ch == '\'') ? ch : 0;
            if(byte == 0)
                break;
            word[i] = (char)byte;
        }
        if(i != count)
            continue;
        word[count] = 0;
        *start = first;
        *offset = end;
        return true;
    }
    return false;
}
