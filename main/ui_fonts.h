#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FONT_STYLE_MODERN_SANS = 0,    // Montserrat 28 / clean modern
    FONT_STYLE_BOLD_DISPLAY,       // Montserrat 32/36 bold
    FONT_STYLE_ELEGANT_SERIF,      // Georgia / Serif style (or Montserrat 24 elegant)
    FONT_STYLE_RETRO_MONO,         // Monospace / Typewriter / Unscii
    FONT_STYLE_COUNT
} font_style_t;

const lv_font_t *ui_fonts_get(font_style_t style);
const char *ui_fonts_get_name(font_style_t style);

#ifdef __cplusplus
}
#endif
