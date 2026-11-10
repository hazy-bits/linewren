#include "bundled-fonts.h"

#include <Files.h>
#include <Folders.h>
#include <Fonts.h>
#include <OSUtils.h>
#include <Resources.h>
#include <Script.h>
#include <stdio.h>

enum { kFirstFontResource = 200 };

const ConstStr255Param kBundledFontNames[kBundledFontCount] = {
    "\pAtkinson Hyperlegible", "\pLinewren Humanist", "\pLinewren Grotesk"
};

static FSSpec gSuitcases[kBundledFontCount];
static Boolean gActivated[kBundledFontCount];

static unsigned long BigEndianLong(const unsigned char *bytes)
{
    return (unsigned long)bytes[0] << 24 | (unsigned long)bytes[1] << 16 |
           (unsigned long)bytes[2] << 8 | bytes[3];
}

static OSErr WriteSuitcase(short resourceID, const FSSpec *file, Boolean *created)
{
    SetResLoad(false);
    Handle resource = Get1Resource('LwFt', resourceID);
    SetResLoad(true);
    if(resource == NULL)
        return resNotFound;

    unsigned char header[128] = {0};
    ReadPartialResource(resource, 0, header, sizeof(header));
    OSErr error = ResError();
    long forkOffset = 128 + ((BigEndianLong(header + 83) + 127) & ~127UL);
    long offset = forkOffset;
    long remaining = BigEndianLong(header + 87);
    if(error == noErr && (remaining <= 0 || offset + remaining > GetResourceSizeOnDisk(resource)))
        error = paramErr;
    if(error == noErr) {
        FSpCreateResFile(file, 'DMOV', 'FFIL', smSystemScript);
        error = ResError();
        *created = error == noErr;
    }

    short ref;
    if(error == noErr)
        error = FSpOpenRF(file, fsRdWrPerm, &ref);
    if(error == noErr) {
        char buffer[4096];
        while(remaining > 0 && error == noErr) {
            long size = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
            ReadPartialResource(resource, offset, buffer, size);
            error = ResError();
            if(error == noErr) {
                long written = size;
                error = FSWrite(ref, &written, buffer);
                if(error == noErr && written != size)
                    error = ioErr;
            }
            remaining -= size;
            offset += size;
        }
        if(error == noErr)
            error = SetEOF(ref, offset - forkOffset);
        OSErr closeError = FSClose(ref);
        if(error == noErr)
            error = closeError;
    }
    ReleaseResource(resource);
    return error;
}

void BundledFontsActivate(void)
{
    short volume;
    long directory;
    if(FindFolder(kOnSystemDisk, kTemporaryFolderType, kCreateFolder,
                  &volume, &directory) != noErr)
        return;

    unsigned long stamp = TickCount();
    for(unsigned i = 0; i < kBundledFontCount; ++i) {
        short family;
        GetFNum(kBundledFontNames[i], &family);
        if(family != 0)
            continue;

        Str63 name;
        FSSpec *file = &gSuitcases[i];
        OSErr error;
        do {
            name[0] = snprintf((char *)name + 1, sizeof(name) - 1,
                               "LinewrenFont-%08lx-%u", stamp++, i);
            error = FSMakeFSSpec(volume, directory, name, file);
        } while(error == noErr);
        if(error != fnfErr)
            continue;

        Boolean created = false;
        if(WriteSuitcase(kFirstFontResource + i, file, &created) == noErr &&
           FMActivateFonts(file, NULL, NULL, 0) == noErr)
            gActivated[i] = true;
        else if(created)
            FSpDelete(file);
    }
}

void BundledFontsDeactivate(void)
{
    for(unsigned i = 0; i < kBundledFontCount; ++i) {
        if(gActivated[i]) {
            FMDeactivateFonts(&gSuitcases[i], NULL, NULL, 0);
            FSpDelete(&gSuitcases[i]);
            gActivated[i] = false;
        }
    }
}
