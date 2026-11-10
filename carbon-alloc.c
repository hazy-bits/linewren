#include <MacMemory.h>
#include <errno.h>
#include <reent.h>
#include <stdlib.h>
#include <string.h>

/* Retro68's Carbon realloc reads the Classic MemErr low-memory address. */
void *_realloc_r(struct _reent *reent, void *block, size_t size)
{
    if(size == 0) {
        free(block);
        return NULL;
    }
    void *replacement = malloc(size);
    if(replacement == NULL) {
        reent->_errno = ENOMEM;
        return NULL;
    }
    if(block != NULL) {
        size_t oldSize = GetPtrSize(block);
        memcpy(replacement, block, size < oldSize ? size : oldSize);
        free(block);
    }
    return replacement;
}

void *LinewrenCarbonRealloc(void *block, size_t size)
{
    return _realloc_r(_REENT, block, size);
}
