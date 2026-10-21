#ifndef TC_OLED_TTF_H
#define TC_OLED_TTF_H
#include "oled.h"
typedef struct ttf ttf_t;
ttf_t *ttf_open(const char *path);
void ttf_close(ttf_t *font);
/* x is left margin; baseline_y is font baseline. ASCII and UTF-8 ASCII subset.
   Glyphs render through the monochrome hinter; threshold (0..255) applies only
   if FreeType returns a grayscale bitmap. */
int ttf_text(ttf_t *font,oled_t *oled,int x,int baseline_y,
             int px_size,const char *text,unsigned threshold);
int ttf_width(ttf_t *font,int px_size,const char *text);
/* Advance width plus ink extent above and below the baseline (any may be NULL). */
int ttf_measure(ttf_t *font,int px_size,const char *text,int *width,int *above,int *below);
/* Stable 12-hour HH:MM footprint and separate AM/PM, centred in given band. */
int ttf_clock_fixed(ttf_t *font,oled_t *oled,int center_x,int baseline_y,
                    int max_px_size,int hour12,int minute,int is_pm,int colon_visible);
/* Ink height of the digits at the size ttf_clock_fixed will choose. */
int ttf_clock_digit_height(ttf_t *font,int max_px_size);
#endif
