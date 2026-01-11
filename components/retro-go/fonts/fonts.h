#include "../rg_gui.h"

/**
 * This file can be edited to add fonts to retro-go.
 * To create new fonts you can use font_converter.py located in the tools folder.
 */
extern const rg_font_t font_VeraBold14;
extern const rg_font_t font_Chinese24;

enum {
    RG_FONT_VERA_14,
    RG_FONT_CHINESE_24,
    RG_FONT_MAX,
};

static const rg_font_t *fonts[RG_FONT_MAX] = {
    &font_VeraBold14,
    &font_Chinese24,
};
