#ifndef LINEWREN_BUNDLED_FONTS_H
#define LINEWREN_BUNDLED_FONTS_H

#include <Types.h>

enum { kBundledFontCount = 3 };
extern const ConstStr255Param kBundledFontNames[kBundledFontCount];

void BundledFontsActivate(void);
void BundledFontsDeactivate(void);

#endif
