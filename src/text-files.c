#include "text-files.h"

#include <Memory.h>
#include <Script.h>
#include <OSUtils.h>
#include <UnicodeConverter.h>
#include <stdint.h>
#include <string.h>

Boolean SameBytes(Handle left, Handle right)
{
    long length = GetHandleSize(left);
    return length == GetHandleSize(right) &&
           (length == 0 || memcmp(*left, *right, length) == 0);
}

OSErr ReadTextFile(const FSSpec *file, Handle *bytes)
{
    short ref;
    long length;
    OSErr error = FSpOpenDF(file, fsRdPerm, &ref);
    *bytes = NULL;
    if(error != noErr)
        return error;
    error = GetEOF(ref, &length);
    if(error == noErr && length > kTextFileLimit)
        error = kTextTooLargeErr;
    if(error == noErr) {
        *bytes = NewHandle(length);
        if(*bytes == NULL)
            error = memFullErr;
        else if(length != 0) {
            HLock(*bytes);
            error = FSRead(ref, &length, **bytes);
            HUnlock(*bytes);
        }
    }
    OSErr closeError = FSClose(ref);
    if(error == noErr)
        error = closeError;
    if(error != noErr && *bytes != NULL) {
        DisposeHandle(*bytes);
        *bytes = NULL;
    }
    return error;
}

OSErr DecodeTextWithFormat(Handle bytes, Handle *text, TextFileFormat *format)
{
    long length = GetHandleSize(bytes), used = 0;
    *text = NewHandle(length * sizeof(uint16_t));
    if(*text == NULL)
        return memFullErr;

    const uint8_t *input = (const uint8_t *)*bytes;
    uint16_t *output = (uint16_t *)**text;
    format->bom = !format->macRoman && length >= 3 && memcmp(input, "\xef\xbb\xbf", 3) == 0;
    format->endings = kTextLF;
    Boolean sawEnding = false;
    long i = format->bom ? 3 : 0;
    if(format->macRoman && length != 0) {
        TextToUnicodeInfo converter;
        OSStatus status = CreateTextToUnicodeInfoByEncoding(kTextEncodingMacRoman, &converter);
        if(status != noErr)
            goto invalid;
        ByteCount read, written;
        HLock(bytes);
        HLock(*text);
        status = ConvertFromTextToUnicode(converter, length, *bytes, 0, 0, NULL, NULL, NULL,
                                          length * sizeof(*output), &read, &written, (UniChar *)**text);
        DisposeTextToUnicodeInfo(&converter);
        HUnlock(*text);
        HUnlock(bytes);
        if(status != noErr || read != length)
            goto invalid;
        output = (uint16_t *)**text;
        length = written / sizeof(*output);
    }
    while(i < length) {
        uint32_t code = format->macRoman ? output[i++] : input[i++];
        unsigned extra;
        uint32_t minimum;
        if(format->macRoman || code < 0x80) {
            extra = 0;
            minimum = 0;
        } else if(code >= 0xc2 && code <= 0xdf) {
            code &= 0x1f;
            extra = 1;
            minimum = 0x80;
        } else if(code >= 0xe0 && code <= 0xef) {
            code &= 0x0f;
            extra = 2;
            minimum = 0x800;
        } else if(code >= 0xf0 && code <= 0xf4) {
            code &= 7;
            extra = 3;
            minimum = 0x10000;
        } else {
            goto invalid;
        }
        while(extra-- != 0) {
            if(i == length || (input[i] & 0xc0) != 0x80)
                goto invalid;
            code = (code << 6) | (input[i++] & 0x3f);
        }
        if(code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff) || code == 0)
            goto invalid;
        if(code == '\r' || code == '\n') {
            TextLineEnding ending = code == '\r' ? kTextCR : kTextLF;
            if(code == '\r' && i < length && (format->macRoman ? output[i] : input[i]) == '\n') {
                ending = kTextCRLF;
                ++i;
            }
            if(!sawEnding)
                format->endings = ending;
            else if(format->endings != ending)
                format->endings = kTextMixed;
            sawEnding = true;
        }
        if(code <= 0xffff) {
            output[used++] = code == '\n' ? '\r' : (uint16_t)code;
        } else {
            code -= 0x10000;
            output[used++] = 0xd800 | (code >> 10);
            output[used++] = 0xdc00 | (code & 0x3ff);
        }
    }
    SetHandleSize(*text, used * sizeof(uint16_t));
    if(MemError() == noErr)
        return noErr;
    DisposeHandle(*text);
    *text = NULL;
    return memFullErr;

invalid:
    DisposeHandle(*text);
    *text = NULL;
    return kTextMalformedInputErr;
}

OSErr DecodeTextFile(Handle bytes, Handle *text)
{
    TextFileFormat format = {0};
    return DecodeTextWithFormat(bytes, text, &format);
}

OSErr EncodeTextWithFormat(const uint16_t *input, unsigned length, TextFileFormat format, Handle *bytes)
{
    long used = format.bom && !format.macRoman ? 3 : 0;
    *bytes = NULL;
    if(length > kTextFileLimit)
        return kTextTooLargeErr;
    if(format.endings == kTextMixed)
        return kTextMixedEndingsErr;
    unsigned capacity = length * 3 + used;
    if(capacity > kTextFileLimit)
        capacity = kTextFileLimit;
    *bytes = NewHandle(capacity);
    if(*bytes == NULL)
        return memFullErr;
    uint8_t *output = (uint8_t *)**bytes;
    if(used != 0)
        memcpy(output, "\xef\xbb\xbf", 3);
    /* Convert the legacy encoding, then restore the file's line endings. */
    if(format.macRoman) {
        UnicodeToTextInfo converter;
        OSStatus status = CreateUnicodeToTextInfoByEncoding(kTextEncodingMacRoman, &converter);
        if(status != noErr) {
            DisposeHandle(*bytes);
            *bytes = NULL;
            return status;
        }
        ByteCount read = 0, written = 0;
        HLock(*bytes);
        if(length != 0)
            status = ConvertFromUnicodeToText(converter, length * sizeof(*input), input,
                                              0, 0, NULL, NULL, NULL, capacity, &read, &written, **bytes);
        DisposeUnicodeToTextInfo(&converter);
        output = (uint8_t *)**bytes;
        if(status == noErr && read == length * sizeof(*input)) {
            used = written;
            if(format.endings == kTextCRLF) {
                for(ByteCount j = 0; j < written; ++j)
                    used += output[j] == '\r';
                if(used > capacity) {
                    status = kTextTooLargeErr;
                } else {
                    for(long at = used; written != 0;) {
                        uint8_t ch = output[--written];
                        if(ch == '\r')
                            output[--at] = '\n';
                        output[--at] = ch;
                    }
                }
            } else if(format.endings == kTextLF) {
                for(long j = 0; j < used; ++j)
                    if(output[j] == '\r')
                        output[j] = '\n';
            }
        } else {
            status = kTextUnsupportedEncodingErr;
        }
        HUnlock(*bytes);
        if(status == noErr) {
            SetHandleSize(*bytes, used);
            status = MemError();
        }
        if(status != noErr) {
            DisposeHandle(*bytes);
            *bytes = NULL;
        }
        return status;
    }
    unsigned i;
    for(i = 0; i < length; ++i) {
        uint32_t code = input[i];
        if(code >= 0xd800 && code <= 0xdbff && i + 1 < length &&
           input[i + 1] >= 0xdc00 && input[i + 1] <= 0xdfff) {
            code = 0x10000 + ((code - 0xd800) << 10) + (input[i + 1] - 0xdc00);
            ++i;
        }
        if(code == 0 || (code >= 0xd800 && code <= 0xdfff)) {
            DisposeHandle(*bytes);
            *bytes = NULL;
            return kTextMalformedInputErr;
        }
        unsigned width = code < 0x80 ? 1 : code < 0x800 ? 2 : code < 0x10000 ? 3 : 4;
        if(code == '\r' && format.endings == kTextCRLF)
            width = 2;
        if(used + width > capacity)
            break;
        if(code < 0x80) {
            output[used++] = code == '\r' && format.endings == kTextLF ? '\n' : (uint8_t)code;
            if(width == 2)
                output[used++] = '\n';
            continue;
        }
        if(code >= 0x10000) {
            output[used++] = 0xf0 | (code >> 18);
            output[used++] = 0x80 | ((code >> 12) & 0x3f);
        } else if(code >= 0x800) {
            output[used++] = 0xe0 | (code >> 12);
        }
        output[used++] = (code < 0x800 ? 0xc0 : 0x80) | ((code >> 6) & 0x3f);
        output[used++] = 0x80 | (code & 0x3f);
    }
    if(i == length) {
        SetHandleSize(*bytes, used);
        if(MemError() == noErr)
            return noErr;
    }
    DisposeHandle(*bytes);
    *bytes = NULL;
    return i == length ? memFullErr : kTextTooLargeErr;
}

OSErr EncodeTextUTF8(const uint16_t *input, unsigned length, Handle *bytes)
{
    return EncodeTextWithFormat(input, length, (TextFileFormat){0}, bytes);
}

static OSErr CopyResourceFork(const FSSpec *from, const FSSpec *to)
{
    short source, target;
    long remaining;
    OSErr error = FSpOpenRF(from, fsRdPerm, &source);
    if(error == eofErr || error == fnfErr)
        return noErr;
    if(error != noErr)
        return error;
    error = GetEOF(source, &remaining);
    if(error == noErr && remaining != 0) {
        error = FSpOpenRF(to, fsWrPerm, &target);
        if(error == noErr) {
            char buffer[1024];
            while(remaining > 0 && error == noErr) {
                long count = remaining > (long)sizeof(buffer) ? (long)sizeof(buffer) : remaining;
                error = FSRead(source, &count, buffer);
                remaining -= count;
                if(error == noErr)
                    error = FSWrite(target, &count, buffer);
            }
            OSErr closeError = FSClose(target);
            if(error == noErr)
                error = closeError;
        }
    }
    OSErr closeError = FSClose(source);
    return error == noErr ? closeError : error;
}

#if !TARGET_API_MAC_CARBON
OSErr RenameClassicFile(FSSpec *file, ConstStr255Param name, ScriptCode script)
{
    if(script != smCyrillic) {
        OSErr error = FSpRename(file, name);
        if(error == noErr)
            memcpy(file->name, name, name[0] + 1);
        return error;
    }
    FSRef original, renamed;
    OSErr error = FSpMakeFSRef(file, &original);
    if(error != noErr)
        return error;
    TextToUnicodeInfo converter;
    error = CreateTextToUnicodeInfoByEncoding(kTextEncodingMacCyrillic, &converter);
    if(error != noErr)
        return error;
    UniChar text[63];
    ByteCount read, written;
    error = ConvertFromTextToUnicode(converter, name[0], (const char *)name + 1, 0,
        0, NULL, NULL, NULL, sizeof(text), &read, &written, text);
    DisposeTextToUnicodeInfo(&converter);
    if(error == noErr)
        error = FSRenameUnicode(&original, written / sizeof(*text), text,
                                kTextEncodingMacCyrillic, &renamed);
    if(error == noErr) {
        FSSpec result;
        error = FSGetCatalogInfo(&renamed, kFSCatInfoNone, NULL, NULL, &result, NULL);
        if(error == noErr)
            *file = result;
    }
    return error;
}
#endif

static OSErr WriteFile(const FSSpec *file, Handle bytes, Handle expected,
                       ScriptCode script, FSSpec *created)
{
#if TARGET_API_MAC_CARBON
    (void)script;
#endif
    FSSpec temp = *file;
    unsigned long serial = TickCount();
    OSErr error = dupFNErr;
    short ref;

    /* FSpCreate claims the name without replacing an existing file. */
    static const Str63 temporaryName = "\pLinewren-save-00000000.tmp";
    for(unsigned attempt = 0; attempt < 16 && error == dupFNErr; ++attempt) {
        unsigned long token = serial + attempt;
        memcpy(temp.name, temporaryName, temporaryName[0] + 1);
        for(unsigned digit = 0; digit < 8; ++digit)
            temp.name[sizeof("Linewren-save-") + digit] =
                "0123456789abcdef"[(token >> (28 - 4 * digit)) & 15];
        error = FSpCreate(&temp, 'Lnwr', 'TEXT', smSystemScript);
    }
    if(error != noErr)
        return error;

    error = FSpOpenDF(&temp, fsWrPerm, &ref);
    if(error == noErr) {
        long length = GetHandleSize(bytes);
        if(length != 0) {
            HLock(bytes);
            error = FSWrite(ref, &length, *bytes);
            HUnlock(bytes);
        }
        OSErr closeError = FSClose(ref);
        if(error == noErr)
            error = closeError;
    }
    if(error == noErr && expected != NULL) {
        Handle current;
        error = ReadTextFile(file, &current);
        if(error == noErr) {
            if(!SameBytes(expected, current))
                error = fBsyErr;
            DisposeHandle(current);
        }
        if(error == noErr)
            error = CopyResourceFork(file, &temp);
    }
    if(error == noErr)
        error = FlushVol(NULL, file->vRefNum);
    if(error == noErr) {
        if(expected != NULL)
            error = FSpExchangeFiles(file, &temp);
#if !TARGET_API_MAC_CARBON
        else if(script == smCyrillic)
            error = RenameClassicFile(&temp, file->name, script);
#endif
        else {
            error = FSpRename(&temp, file->name);
            if(error == noErr)
                memcpy(temp.name, file->name, file->name[0] + 1);
        }
    }
    if(error == noErr && created != NULL)
        *created = temp;
    if(error != noErr || expected != NULL)
        FSpDelete(&temp);
    if(error == noErr)
        FlushVol(NULL, file->vRefNum);
    return error;
}


OSErr WriteTextFile(const FSSpec *file, Handle bytes, Handle expected)
{
    return WriteFile(file, bytes, expected, smRoman, NULL);
}

#if !TARGET_API_MAC_CARBON
OSErr CreateClassicTextFile(FSSpec *file, Handle bytes, ScriptCode script)
{
    return WriteFile(file, bytes, NULL, script, file);
}
#endif
