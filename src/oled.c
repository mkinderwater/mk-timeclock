#define _POSIX_C_SOURCE 200809L
#include "oled.h"
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
static int tx(int fd,uint8_t control,const uint8_t *v,unsigned n){
    uint8_t b[33]; if(n>32) return -1;
    b[0]=control;memcpy(b+1,v,n);
    return write(fd,b,n+1)==(ssize_t)n+1?0:-1;
}
static int command(oled_t *o,uint8_t v){return tx(o->fd,0x00,&v,1);}
int oled_open(oled_t *o,const char *device,unsigned addr){
    memset(o,0,sizeof(*o));o->fd=-1;
    if(addr>0x7f)return -1;
    o->fd=open(device,O_RDWR|O_CLOEXEC);if(o->fd<0)return -1;
    if(ioctl(o->fd,I2C_SLAVE,addr)<0){oled_close(o);return -1;}
    return oled_initialize(o);
}
int oled_initialize(oled_t *o){
    /* SSD1309, page addressing. Panel orientation and charge pump configuration
       must be verified on the actual module. No GPIO reset is controlled here. */
    static const uint8_t seq[]={0xAE,0xD5,0x80,0xA8,0x3F,0xD3,0x00,
        0x40,0x20,0x02,0xA1,0xC8,0xDA,0x12,0x81,0x7F,
        0xD9,0xF1,0xDB,0x3C,0xA4,0xA6,0x2E,0xAF};
    for(unsigned i=0;i<sizeof(seq);i++) if(command(o,seq[i])<0)return -1;
    o->have_prev=0;oled_clear(o);return oled_flush(o);
}
void oled_clear(oled_t *o){memset(o->fb,0,sizeof(o->fb));}
void oled_pixel(oled_t *o,int x,int y,int on){
    if(x<0||x>=OLED_WIDTH||y<0||y>=OLED_HEIGHT)return;
    uint8_t *b=&o->fb[(y>>3)*OLED_WIDTH+x],m=(uint8_t)(1u<<(y&7));
    if(on)*b|=m;else *b&=(uint8_t)~m;
}
int oled_flush(oled_t *o){
    if(o->fd<0)return -1;
    for(int page=0;page<8;page++){
        uint8_t *cur=o->fb+page*128,*old=o->prev+page*128;
        int first=0,last=127;
        if(o->have_prev){
            /* Send only the changed column span. A seconds tick or countdown step
               then costs a few bytes on the bus instead of a full 128-byte page,
               which keeps RC522 polling responsive on a 100 kHz I2C bus. */
            while(first<128&&cur[first]==old[first])first++;
            if(first==128)continue;
            while(last>first&&cur[last]==old[last])last--;
        }
        /* SSD1309 page addressing: page, then column low/high nibble. */
        if(command(o,(uint8_t)(0xB0|page))<0||command(o,(uint8_t)(0x00|(first&0x0F)))<0||
           command(o,(uint8_t)(0x10|(first>>4)))<0)return -1;
        for(int x=first;x<=last;x+=16){
            unsigned n=(unsigned)(last-x+1);if(n>16)n=16;
            if(tx(o->fd,0x40,cur+x,n)<0)return -1;
        }
    }
    memcpy(o->prev,o->fb,OLED_BYTES);o->have_prev=1;return 0;
}
void oled_close(oled_t *o){if(o->fd>=0)close(o->fd);o->fd=-1;}

int oled_contrast(oled_t *o,unsigned value){
    if(!o||o->fd<0||value>255)return -1;
    /* SSD1309: contrast alone produces little visible dimming on this panel.
       Use the initialized precharge/VCOMH levels at normal brightness. */
    uint8_t precharge=value>=96?0xF1:value>=32?0x22:0x11;
    uint8_t vcomh=value>=96?0x3C:value>=32?0x34:0x00;
    static const uint8_t cmd[]={0x81,0xD9,0xDB};
    const uint8_t param[]={(uint8_t)value,precharge,vcomh};
    for(unsigned i=0;i<3;i++){
        if(command(o,cmd[i])<0||command(o,param[i])<0)return -1;
    }
    return 0;
}
