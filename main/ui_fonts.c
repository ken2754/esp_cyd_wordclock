#include "ui_fonts.h"

const lv_font_t *ui_fonts_get(font_style_t style)
{
    switch (style) {
        case FONT_STYLE_MODERN_SANS:
#if defined(CONFIG_LV_FONT_MONTSERRAT_28) && CONFIG_LV_FONT_MONTSERRAT_28
            return &lv_font_montserrat_28;
#else
            return &lv_font_montserrat_24;
#endif

        case FONT_STYLE_BOLD_DISPLAY:
#if defined(CONFIG_LV_FONT_MONTSERRAT_32) && CONFIG_LV_FONT_MONTSERRAT_32
            return &lv_font_montserrat_32;
#elif defined(CONFIG_LV_FONT_MONTSERRAT_28) && CONFIG_LV_FONT_MONTSERRAT_28
            return &lv_font_montserrat_28;
#else
            return &lv_font_montserrat_24;
#endif

        case FONT_STYLE_ELEGANT_SERIF:
#if defined(CONFIG_LV_FONT_MONTSERRAT_24) && CONFIG_LV_FONT_MONTSERRAT_24
            return &lv_font_montserrat_24;
#else
            return &lv_font_montserrat_16;
#endif

        case FONT_STYLE_RETRO_MONO:
#if defined(CONFIG_LV_FONT_UNSCII_16) && CONFIG_LV_FONT_UNSCII_16
            return &lv_font_unscii_16;
#else
            return &lv_font_montserrat_24;
#endif

        default:
            return &lv_font_montserrat_28;
    }
}

const char *ui_fonts_get_name(font_style_t style)
{
    switch (style) {
        case FONT_STYLE_MODERN_SANS:    return "Modern Sans";
        case FONT_STYLE_BOLD_DISPLAY:   return "Bold Display";
        case FONT_STYLE_ELEGANT_SERIF:  return "Compact Serif";
        case FONT_STYLE_RETRO_MONO:     return "Retro Monospace";
        default:                        return "Standard";
    }
}
