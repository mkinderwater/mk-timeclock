#include "oled_ttf.h"
#include "oled_bitmap.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdlib.h>

/* Glyphs are rasterized with FreeType's monochrome hinter. On a 1-bit panel this
   gives even, pixel-aligned stems, where thresholding the anti-aliased bitmap
   produced ragged, uneven strokes at small sizes. Grayscale bitmaps are still
   handled (threshold) in case a font or FreeType build returns them. */
#define TTF_LOAD_MEASURE (FT_LOAD_DEFAULT|FT_LOAD_TARGET_MONO)
#define TTF_LOAD_DRAW    (FT_LOAD_RENDER|FT_LOAD_MONOCHROME|FT_LOAD_TARGET_MONO)

struct ttf{
    FT_Library lib;FT_Face face;
    /* Clock fit cache: the fitted size depends only on the font and max size. */
    int clk_max,clk_size,clk_digit_w,clk_colon_w,clk_digit_h;
};
ttf_t *ttf_open(const char *path){
    ttf_t*t=calloc(1,sizeof(*t));if(!t)return NULL;
    if(FT_Init_FreeType(&t->lib)){free(t);return NULL;}
    if(FT_New_Face(t->lib,path,0,&t->face)){FT_Done_FreeType(t->lib);free(t);return NULL;}
    return t;
}
void ttf_close(ttf_t*t){if(t){FT_Done_Face(t->face);FT_Done_FreeType(t->lib);free(t);}}

int ttf_measure(ttf_t*t,int size,const char*text,int*width,int*above,int*below){
    if(!t||!text||size<1||FT_Set_Pixel_Sizes(t->face,0,(FT_UInt)size))return -1;
    int w=0,up=0,down=0;
    for(const unsigned char*c=(const unsigned char*)text;*c;c++){
        if(*c>=128)continue;
        if(FT_Load_Char(t->face,*c,TTF_LOAD_DRAW))continue;
        FT_GlyphSlot g=t->face->glyph;
        if(g->bitmap.rows){
            int top=g->bitmap_top,bottom=(int)g->bitmap.rows-g->bitmap_top;
            if(top>up)up=top;
            if(bottom>down)down=bottom;
        }
        w+=(int)(g->advance.x>>6);
    }
    if(width)*width=w;
    if(above)*above=up;
    if(below)*below=down;
    return 0;
}
int ttf_width(ttf_t*t,int size,const char*text){
    if(!t||FT_Set_Pixel_Sizes(t->face,0,(FT_UInt)size))return -1;
    int width=0;for(const unsigned char*c=(const unsigned char*)text;*c;c++){
        if(*c>=128)continue;
        if(!FT_Load_Char(t->face,*c,TTF_LOAD_MEASURE))width+=(int)(t->face->glyph->advance.x>>6);
    }return width;
}
int ttf_text(ttf_t*t,oled_t*o,int x,int baseline,int size,const char*text,unsigned threshold){
    if(!t||FT_Set_Pixel_Sizes(t->face,0,(FT_UInt)size))return -1;
    for(const unsigned char*c=(const unsigned char*)text;*c;c++){
        if(*c>=128)continue;
        if(FT_Load_Char(t->face,*c,TTF_LOAD_DRAW))continue;
        FT_GlyphSlot g=t->face->glyph;FT_Bitmap *b=&g->bitmap;
        for(unsigned row=0;row<b->rows;row++){
            /* FT bitmaps generally have positive pitch, but negative is legal. */
            const unsigned char *scan=b->buffer+(b->pitch>=0?(int)row*b->pitch:
                ((int)b->rows-1-(int)row)*(-b->pitch));
            for(unsigned col=0;col<b->width;col++){
                int lit=0;
                if(b->pixel_mode==FT_PIXEL_MODE_MONO)lit=(scan[col>>3]>>(7-(col&7)))&1;
                else if(b->pixel_mode==FT_PIXEL_MODE_GRAY)lit=scan[col]>=threshold;
                if(lit)oled_pixel(o,x+g->bitmap_left+(int)col,baseline-g->bitmap_top+(int)row,1);
            }
        }
        x+=(int)(g->advance.x>>6);
    }
    return 0;
}

/* Fit four digit cells, a colon cell and the AM/PM tag into the panel width. */
static int clock_fit(ttf_t*t,int max_size){
    if(t->clk_max==max_size&&t->clk_size)return 0;
    const int gap=1, margin=3, am_gap=3;
    const int am_width=oled_bitmap_width("AM");  /* AM and PM share a footprint */
    for(int size=max_size;size>=10;size--){
        int digit_w=0,digit_h=0;
        for(char ch='0';ch<='9';ch++){
            char str[]={ch,0};int w,up;
            if(ttf_measure(t,size,str,&w,&up,NULL))return -1;
            if(w>digit_w)digit_w=w;
            if(up>digit_h)digit_h=up;
        }
        int colon_w=ttf_width(t,size,":");
        if(colon_w<0)return -1;
        int total=4*digit_w+colon_w+4*gap+am_gap+am_width;
        if(total<=OLED_WIDTH-2*margin){
            t->clk_max=max_size;t->clk_size=size;t->clk_digit_w=digit_w;
            t->clk_colon_w=colon_w;t->clk_digit_h=digit_h;return 0;
        }
    }
    return -1;
}
int ttf_clock_digit_height(ttf_t*t,int max_size){
    if(!t||clock_fit(t,max_size))return -1;
    return t->clk_digit_h;
}

/* Fixed digit cells prevent minute-dependent movement. 12-hour times occupy
   H:MM or HH:MM as displayed: do not reserve an invisible leading hour cell.
   Unlike the kid clock's 256px panel, this 128px screen centres the combined
   visible time and AM/PM tag inside the available width. */
int ttf_clock_fixed(ttf_t *t,oled_t *o,int center_x,int baseline,int max_size,
                    int hour12,int minute,int is_pm,int colon_visible){
    int h24=is_pm<0;
    if(!t||!o||hour12<(h24?0:1)||hour12>(h24?23:12)||minute<0||minute>59)return -1;
    if(clock_fit(t,max_size))return -1;
    const int gap=1, am_gap=3;
    const int size=t->clk_size, digit_w=t->clk_digit_w, colon_w=t->clk_colon_w;
    const int hour_digits=h24||hour12>=10?2:1;
    const int cells=hour_digits+3; /* hour digit(s), colon, two minute digits */
    const int time_width=(hour_digits+2)*digit_w+colon_w+(cells-1)*gap;
    const int tag_width=oled_bitmap_width("AM");
    const int total=time_width+(h24?0:am_gap+tag_width);
    int x=center_x-total/2;
    char digits[6];
    if(hour_digits==2)snprintf(digits,sizeof digits,"%02d:%02d",hour12,minute);
    else snprintf(digits,sizeof digits,"%d:%02d",hour12,minute);
    for(int i=0;i<cells;i++){
        const int colon=digits[i]==':';
        const int cell=colon?colon_w:digit_w;
        if(!colon||colon_visible){
            char glyph[]={digits[i],0};
            int actual=ttf_width(t,size,glyph);
            if(actual<0)return -1;
            /* Centre each rendered glyph within its stable cell. */
            int inset=(cell-actual)/2;
            if(ttf_text(t,o,x+inset,baseline,size,glyph,110))return -1;
        }
        x+=cell+gap;
    }
    x+=am_gap-gap;
    if(!h24)oled_bitmap_text(o,is_pm?"PM":"AM",x,baseline-t->clk_digit_h);
    return 0;
}
