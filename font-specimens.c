#include "mac-toolbox.h"
#include "font-specimens.h"

#include "bundled-fonts.h"

#include <Fonts.h>
#include <Memory.h>
#include <Menus.h>
#include <TextUtils.h>
#include <stdio.h>
#include <string.h>

static const UniChar kSample[] =
    u"# A quiet place to write\r\r"
    u"Markdown on a classic Mac. The quick brown fox jumps over the lazy dog. "
    u"Read it slowly, then look at markdown and macbook.\r\r"
    u"**Bold for emphasis**, *italic for a quieter aside*, and ***both together***.\r\r"
    u"0123456789  |  O0  Il1  rn m  AV To\r"
    u"Caf\u00e9, na\u00efve, d\u00e9j\u00e0 vu.\r";

static Boolean HasFourFaces(short family)
{
    FMFont faces[4];
    for(short style = normal; style <= (bold | italic); ++style) {
        FMFontStyle intrinsic;
        if(FMGetFontFromFontFamilyInstance(family, style, &faces[style], &intrinsic) != noErr ||
           (intrinsic & (bold | italic)) != style)
            return false;
        for(short previous = normal; previous < style; ++previous) {
            if(faces[previous] == faces[style])
                return false;
        }
    }
    return true;
}

static Boolean FamiliesInFontMenu(void)
{
    MenuRef menu = NewMenu(135, "\pSpecimen fonts");
    if(menu == NULL)
        return false;
    AppendResMenu(menu, 'FOND');
    unsigned found = 0;
    for(short item = 1; item <= CountMenuItems(menu); ++item) {
        Str255 name;
        GetMenuItemText(menu, item, name);
        for(unsigned i = 0; i < kBundledFontCount; ++i) {
            if(EqualString(name, kBundledFontNames[i], false, true))
                found |= 1u << i;
        }
    }
    DisposeMenu(menu);
    return found == (1u << kBundledFontCount) - 1;
}

/* Capture the pixels drawn by Linewren in the guest window's content area. */
static Boolean Capture(FILE *out, WindowRef window)
{
    Rect bounds = WindowCanvas(window);
    unsigned width = bounds.right - bounds.left, height = bounds.bottom - bounds.top;
    SetPortWindowPort(window);
    fprintf(out, "P6\n%u %u\n255\n", width, height);
    unsigned char row[3 * width];
    Boolean written = true;
    for(unsigned y = 0; y < height && written; ++y) {
        for(unsigned x = 0; x < width; ++x) {
            RGBColor color;
            GetCPixel(bounds.left + x, bounds.top + y, &color);
            row[3 * x] = color.red >> 8;
            row[3 * x + 1] = color.green >> 8;
            row[3 * x + 2] = color.blue >> 8;
        }
        written = fwrite(row, 1, sizeof(row), out) == sizeof(row);
    }
    return written;
}

int FontSpecimensRun(WindowRef window, Editor *editor, void (*draw)(WindowRef, unsigned))
{
    FILE *out = fopen("out", "wb");
    if(out == NULL)
        return 1;

    short families[kBundledFontCount];
    OSStatus error = noErr;
    unsigned familyIndex = 0;
    short pointSize = 0;
    const char *step = "resolve all four real typefaces";
    for(unsigned i = 0; i < kBundledFontCount; ++i) {
        familyIndex = i;
        GetFNum(kBundledFontNames[i], &families[i]);
        if(families[i] == 0 || !HasFourFaces(families[i])) {
            error = paramErr;
            goto done;
        }
    }

    step = "find the activated families in the native font menu";
    if(!FamiliesInFontMenu()) {
        error = resNotFound;
        goto done;
    }

    step = "open the specimen in Linewren";
    Handle source = NewHandle(sizeof(kSample) - sizeof(UniChar));
    if(source == NULL) {
        error = memFullErr;
        goto done;
    }
    memcpy(*source, kSample, sizeof(kSample) - sizeof(UniChar));
    if((error = EditorTakeText(editor, source)) != noErr) {
        DisposeHandle(source);
        goto done;
    }
    EditorActivate(editor, false);
    SetWTitle(window, "\pLinewren Font Specimen");

    for(unsigned theme = 0; theme < 2; ++theme) {
        for(unsigned i = 0; i < kBundledFontCount; ++i) {
            familyIndex = i;
            for(short points = 12; points <= 14; ++points) {
                pointSize = points;
                step = "set the document typeface and size";
                EditorTypography typography = EditorDefaultTypography();
                typography.family = families[i];
                typography.points = points;
                if(!EditorSetTypography(editor, typography)) {
                    error = paramErr;
                    goto done;
                }
                InvalidateWindow(window);
                draw(window, theme);
                step = "draw the styled ATSUI document";
                if((error = EditorSpecimenLayoutStatus(editor)) != noErr)
                    goto done;
                step = "verify the editor canvas matches its theme";
                RGBColor page;
                GetCPixel(WindowCanvas(window).right - 27, 19, &page);
                if(theme == 0 ? page.red < 0xd000 : page.red > 0x4000) {
                    error = paramErr;
                    goto done;
                }
                step = "capture the 13-point QuickDraw specimen";
                if(points == 13 && !Capture(out, window)) {
                    error = ioErr;
                    goto done;
                }
            }
        }
    }

done:
    EditorSetTypography(editor, EditorDefaultTypography());
    if(error != noErr)
        fprintf(out, "FAIL: %s (%ld), %.*s at %d pt\n", step, (long)error,
                kBundledFontNames[familyIndex][0], kBundledFontNames[familyIndex] + 1, pointSize);
    else
        fprintf(out, "PASS: three families, four real faces each, 12-14 pt in both themes\n");
    return fclose(out) != 0 || error != noErr;
}
