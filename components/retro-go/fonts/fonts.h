#include "../rg_gui.h"

/**
 * This file can be edited to add fonts to retro-go.
 * To create new fonts you can use font_converter.py located in the tools folder.
 */
// extern const rg_font_t font_VeraBold14;
#ifndef ENABLE_CHINESE_12
extern const rg_font_t font_Chinese24;
#else
extern const rg_font_t font_Chinese12;
#endif
enum {
    // RG_FONT_VERA_14,
    #ifndef ENABLE_CHINESE_12
    RG_FONT_CHINESE_24,
    #else
    RG_FONT_CHINESE_12,
    #endif
    RG_FONT_MAX,
};

static const rg_font_t *fonts[RG_FONT_MAX] = {
    // &font_VeraBold14,
    #ifndef ENABLE_CHINESE_12
    &font_Chinese24,
    #else
    &font_Chinese12,
    #endif
};
