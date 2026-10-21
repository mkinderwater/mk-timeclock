#ifndef TC_OLED_H
#define TC_OLED_H
#include <stdint.h>
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_BYTES (OLED_WIDTH*OLED_HEIGHT/8)
typedef struct {int fd; uint8_t fb[OLED_BYTES]; uint8_t prev[OLED_BYTES]; int have_prev;} oled_t;
int oled_open(oled_t *o,const char *dev,unsigned address);
int oled_initialize(oled_t *o);
void oled_clear(oled_t *o);
void oled_pixel(oled_t *o,int x,int y,int on);
int oled_flush(oled_t *o);
int oled_contrast(oled_t *o,unsigned value);
void oled_close(oled_t *o);
#endif
