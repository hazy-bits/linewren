#ifndef LINEWREN_TEXT_FILES_H
#define LINEWREN_TEXT_FILES_H

#include <Files.h>
#include <stdint.h>

enum { kTextFileLimit = 192 * 1024 };

Boolean SameBytes(Handle left, Handle right);
OSErr ReadTextFile(const FSSpec *file, Handle *bytes);
OSErr DecodeTextFile(Handle bytes, Handle *text);
OSErr EncodeTextUTF8(const uint16_t *input, unsigned length, Handle *bytes);
/* A NULL expected snapshot requires a new filename. */
OSErr WriteTextFile(const FSSpec *file, Handle bytes, Handle expected);

#endif
