#ifndef LINEWREN_TEXT_FILES_H
#define LINEWREN_TEXT_FILES_H

#include <Files.h>
#include <stdint.h>

enum { kTextFileLimit = 192 * 1024 };
enum { kTextTooLargeErr = -32001, kTextMixedEndingsErr = -32002 };

typedef enum { kTextLF, kTextCR, kTextCRLF, kTextMixed } TextLineEnding;
typedef struct {
    TextLineEnding endings;
    Boolean bom, macRoman;
} TextFileFormat;

Boolean SameBytes(Handle left, Handle right);
OSErr ReadTextFile(const FSSpec *file, Handle *bytes);
OSErr DecodeTextFile(Handle bytes, Handle *text);
OSErr DecodeTextWithFormat(Handle bytes, Handle *text, TextFileFormat *format);
OSErr EncodeTextWithFormat(const uint16_t *input, unsigned length, TextFileFormat format, Handle *bytes);
OSErr EncodeTextUTF8(const uint16_t *input, unsigned length, Handle *bytes);
#if !TARGET_API_MAC_CARBON
OSErr RenameClassicFile(FSSpec *file, ConstStr255Param name, ScriptCode script);
OSErr CreateClassicTextFile(FSSpec *file, Handle bytes, ScriptCode script);
#endif
/* A NULL expected snapshot requires a new filename. */
OSErr WriteTextFile(const FSSpec *file, Handle bytes, Handle expected);

#endif
