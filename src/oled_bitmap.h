#ifndef TC_OLED_BITMAP_H
#define TC_OLED_BITMAP_H
#include "oled.h"
int oled_bitmap_width(const char *s);
void oled_bitmap_text(oled_t *o,const char *s,int x,int y);
/* on=0 draws dark pixels, for labels on a lit (inverse) background. */
void oled_bitmap_text_on(oled_t *o,const char *s,int x,int y,int on);
#endif
