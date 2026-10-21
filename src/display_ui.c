#define _POSIX_C_SOURCE 200809L
#include "display_ui.h"
#include "boot_logo.h"
#include "oled_bitmap.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* Monochrome 128x64. Each screen is drawn into the framebuffer, then sent with
   one flush; oled_flush only transmits the columns that changed.

   Layout grid
     idle:    0-8 header (date | link state)   10 rule   11-49 time
              51-52 seconds bar                56-62 footer prompt
     result:  0-15 greeting / title            17 rule   20-49 icon + big word
              53-59 detail line                62-63 countdown bar           */

#define HOLD_MS        3000
#define HOLD_ALERT_MS  4000
enum {ICON_NONE,ICON_CHECK,ICON_OUT,ICON_CLOCK,ICON_SPIN,ICON_DOUBLE,ICON_BANG,ICON_CROSS,ICON_STAR,ICON_INFO,ICON_CARD};

static long long mono_ms(void){
    struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);
    return (long long)ts.tv_sec*1000+ts.tv_nsec/1000000;
}

/* ---------- primitives ---------- */
static void px(display_t*d,int x,int y,int on){oled_pixel(&d->oled,x,y,on);}
static void hline(display_t*d,int x0,int x1,int y){for(int x=x0;x<=x1;x++)px(d,x,y,1);}
static void fill_rect(display_t*d,int x0,int y0,int x1,int y1,int on){for(int y=y0;y<=y1;y++)for(int x=x0;x<=x1;x++)px(d,x,y,on);}
/* Bresenham line stamped with a small disc: half=1 gives a 3 px stroke. */
/* Burn-in guard: move the whole idle frame sideways by -1, 0 or +1 pixel. */
static void shift_frame(display_t*d,int dx){
    if(!dx)return;
    for(int page=0;page<8;page++){
        uint8_t *row=d->oled.fb+page*OLED_WIDTH;
        if(dx>0){memmove(row+dx,row,(size_t)(OLED_WIDTH-dx));memset(row,0,(size_t)dx);}
        else{memmove(row,row-dx,(size_t)(OLED_WIDTH+dx));memset(row+OLED_WIDTH+dx,0,(size_t)-dx);}
    }
}
static void label(display_t*d,int top,const char*s,int centered){
    int w=oled_bitmap_width(s);
    int x=centered?(OLED_WIDTH-w)/2:3;
    if(x<1)x=1;
    oled_bitmap_text(&d->oled,s,x,top);
}

/* ---------- fitted TrueType text ---------- */
/* Draw text as large as possible inside a box, centred on its ink. Tries one
   line first, then a two-line split at the space nearest the middle. */
static int fit_block(display_t*d,const char*s,int x0,int x1,int y0,int y1,int max_size,int min_size){
    const int bw=x1-x0+1,bh=y1-y0+1,gap=3;
    const char *split=NULL;
    {   /* space closest to the middle of the string */
        size_t n=strlen(s);int best=1<<30;
        for(const char*p=s;*p;p++)if(*p==' '){
            int dist=(int)(p-s)-(int)n/2;if(dist<0)dist=-dist;
            if(dist<best){best=dist;split=p;}
        }
    }
    char a[48]={0},b[48]={0};
    if(split){snprintf(a,sizeof a,"%.*s",(int)(split-s),s);snprintf(b,sizeof b,"%s",split+1);}
    for(int size=max_size;size>=min_size;size--){
        int w,up,down;
        if(ttf_measure(d->font,size,s,&w,&up,&down))break;
        if(w<=bw&&up+down<=bh){
            int base=y0+(bh-(up+down))/2+up;
            ttf_text(d->font,&d->oled,x0+(bw-w)/2,base,size,s,105);
            return size;
        }
        if(split){
            int wa,ua,da,wb,ub,db;
            if(ttf_measure(d->font,size,a,&wa,&ua,&da)||ttf_measure(d->font,size,b,&wb,&ub,&db))break;
            int up2=ua>ub?ua:ub,down2=da>db?da:db,line=up2+down2;
            if(wa<=bw&&wb<=bw&&2*line+gap<=bh){
                int top=y0+(bh-(2*line+gap))/2;
                ttf_text(d->font,&d->oled,x0+(bw-wa)/2,top+up2,size,a,105);
                ttf_text(d->font,&d->oled,x0+(bw-wb)/2,top+line+gap+up2,size,b,105);
                return size;
            }
        }
    }
    /* Bounded bitmap fallback: never draws past the box. */
    int chars=(bw+1)/6;if(chars>40)chars=40;if(chars<1)return 0;
    char t[44];snprintf(t,sizeof t,"%.*s",chars,s);
    int w=oled_bitmap_width(t);
    oled_bitmap_text(&d->oled,t,x0+(bw-w)/2,y0+(bh-7)/2);
    return 0;
}

/* ---------- confirmation pages ---------- */
struct page_spec{const char*big;const char*sub;int icon,invert,hold,timestamp;};
static struct page_spec spec_for(display_kind_t k){
    switch(k){
    case DISPLAY_IN:        return (struct page_spec){"IN","CLOCKED IN",ICON_CHECK,0,HOLD_MS,1};
    case DISPLAY_OUT:       return (struct page_spec){"OUT","CLOCKED OUT",ICON_OUT,0,HOLD_MS,1};
    case DISPLAY_LATE:      return (struct page_spec){"LATE","CLOCKED IN",ICON_CLOCK,0,HOLD_MS,1};
    case DISPLAY_PENDING:   return (struct page_spec){"SAVED","CONFIRMING...",ICON_SPIN,0,HOLD_MS,0};
    case DISPLAY_DUPLICATE: return (struct page_spec){"ALREADY","RECORDED",ICON_DOUBLE,0,HOLD_MS,0};
    case DISPLAY_REVIEW:    return (struct page_spec){"SEE MANAGER","SWIPE SAVED",ICON_BANG,1,HOLD_ALERT_MS,0};
    case DISPLAY_VACATION:  return (struct page_spec){"VACATION","NO PUNCH NEEDED",ICON_INFO,0,HOLD_MS,0};
    case DISPLAY_TEST:      return (struct page_spec){"TEST","NO PUNCH RECORDED",ICON_INFO,0,HOLD_MS,0};
    case DISPLAY_MANAGER:   return (struct page_spec){"MANAGER","NO PUNCH RECORDED",ICON_STAR,0,HOLD_MS,0};
    case DISPLAY_APPROVED:  return (struct page_spec){"APPROVED","ACTION AUTHORIZED",ICON_CHECK,0,HOLD_MS,0};
    case DISPLAY_ENROLL:    return (struct page_spec){"CARD READ","NO PUNCH RECORDED",ICON_CARD,0,HOLD_MS,0};
    case DISPLAY_REJECT:    return (struct page_spec){"NOT RECORDED","SEE MANAGER",ICON_CROSS,1,HOLD_ALERT_MS,0};
    default:                return (struct page_spec){"","",ICON_NONE,0,HOLD_MS,0};
    }
}
static void greeting(char*out,size_t n,const char*name){
    if(!name||!*name){snprintf(out,n,"Hello");return;}
    int lower=0;for(const char*p=name;*p;p++)if(islower((unsigned char)*p))lower=1;
    snprintf(out,n,"%s %.24s",lower?"Hi":"HI",name);
}
/* Common 128x64 layout: bitmap header (y=2), rule (y=12),
   fitted TrueType centre (y=16..47), rule (y=50), bitmap footer (y=55).
   Both idle and outcome pages use the exact same bands. */
static void result_page(display_t*d,display_kind_t kind,const char*top,const char*big,const char*sub,int icon,int invert,int hold){
    oled_clear(&d->oled);
    char heading[22];snprintf(heading,sizeof heading,"%.20s",top?top:"");
    label(d,2,heading,1);
    hline(d,2,125,12);
    fit_block(d,big,2,125,16,47,31,10);
    hline(d,2,125,50);
    if(sub&&*sub){char footer[22];snprintf(footer,sizeof footer,"%.20s",sub);label(d,55,footer,1);}
    (void)icon;(void)invert;
    long long now=mono_ms();
    d->msg_active=1;d->msg_kind=(int)kind;d->msg_invert=0;d->msg_spin=0;
    d->msg_start=now;d->msg_until=now+hold;d->msg_bar=-1;
    oled_flush(&d->oled);
}

int display_open(display_t*d,const char*i2c,unsigned addr,const char*font,const char*id){
    memset(d,0,sizeof(*d));d->oled.fd=-1;d->last_dim=-1;
    snprintf(d->clock_id,sizeof d->clock_id,"%s",id);
    d->font=ttf_open(font);if(!d->font)return -1;
    if(oled_open(&d->oled,i2c,addr)<0){ttf_close(d->font);d->font=NULL;return -1;}
    /* Never display the idle clock before the startup logo. */
    d->ready=1;display_boot_logo(d);return 0;
}
void display_boot_logo(display_t*d){
    if(!d->ready)return;
    memcpy(d->oled.fb,boot_logo,sizeof boot_logo);
    oled_flush(&d->oled);
}
void display_close(display_t*d){if(d->ready)oled_close(&d->oled);ttf_close(d->font);d->font=NULL;d->ready=0;}
void display_idle(display_t*d){display_idle_status(d,0,1);}

void display_wifi_status(display_t*d,int associated,int dbm,int bars){
 d->wifi_associated=associated;d->wifi_dbm=dbm;d->wifi_bars=bars;
}
static void link_badge(display_t*d,int online){
 /* Signal bars depict RF strength, not reachability of the Primary server. */
 if(d->wifi_associated){
  for(int i=0;i<4;i++){
   int x=112+i*4,top=8-(i+1)*2;
   if(i<d->wifi_bars)fill_rect(d,x,top,x+2,8,1);
   else fill_rect(d,x,8,x+2,8,1);
  }
  if(!online)px(d,108,2,1); /* small dot: backend currently unreachable */
 }else{
  const char*t="NO WIFI";int w=oled_bitmap_width(t);
  fill_rect(d,125-w-3,0,125,8,1);
  oled_bitmap_text_on(&d->oled,t,125-w-1,1,0);
 }
}
void display_idle_status(display_t*d,int pending,int online){
    if(!d->ready)return;
    d->msg_active=0;
    time_t now=time(NULL);struct tm tm;localtime_r(&now,&tm);
    char date[24],footer[24];strftime(date,sizeof date,"%a %b %d",&tm);
    oled_clear(&d->oled);
    oled_bitmap_text(&d->oled,date,2,2);
    link_badge(d,online);
    hline(d,2,125,12);

    /* Fixed digit cells from the TTF renderer; no phantom leading hour.
       Fit the clock between the two shared separators. */
    int digit_h=ttf_clock_digit_height(d->font,32);
    int baseline=digit_h>0?16+(32+digit_h)/2:45;
    if(baseline>47)baseline=47;
    ttf_clock_fixed(d->font,&d->oled,64,baseline,32,
                    tm.tm_hour%12?tm.tm_hour%12:12,tm.tm_min,tm.tm_hour>=12,(tm.tm_sec%2)==0);
    hline(d,2,125,50);

    if(pending>0){
        int n=pending>999?999:pending;
        if(online)snprintf(footer,sizeof footer,"SENDING %d SWIPE%s",n,n==1?"":"S");
        else snprintf(footer,sizeof footer,"%d SAVED ON CLOCK",n);
        label(d,55,footer,1);
    }else label(d,55,"TAP YOUR CARD",1);
    shift_frame(d,d->shift);
    oled_flush(&d->oled);
}

void display_result(display_t*d,display_kind_t kind,const char*text){
    if(!d->ready)return;
    struct page_spec p=spec_for(kind);
    char top[40],sub[64];
    if(kind==DISPLAY_REJECT)snprintf(top,sizeof top,"%.30s",text&&*text?text:"CARD ERROR");
    else if(kind==DISPLAY_MANAGER)snprintf(top,sizeof top,"Hello");
    else if(kind==DISPLAY_APPROVED)snprintf(top,sizeof top,"APPROVED");
    else if(kind==DISPLAY_ENROLL)snprintf(top,sizeof top,"ENROLLMENT");
    else greeting(top,sizeof top,text);
    if(p.timestamp){
        time_t now=time(NULL);struct tm tm;localtime_r(&now,&tm);
        int h=tm.tm_hour%12?tm.tm_hour%12:12;
        snprintf(sub,sizeof sub,"%s %d:%02d %s",p.sub,h,tm.tm_min,tm.tm_hour>=12?"PM":"AM");
    }else if(kind==DISPLAY_REJECT&&text&&!strcmp(text,"SCAN BUSY"))snprintf(sub,sizeof sub,"TAP AGAIN"); /* the employee can fix this one */
    else snprintf(sub,sizeof sub,"%s",p.sub);
    result_page(d,kind,top,p.big,sub,p.icon,p.invert,p.hold);
}
void display_message(display_t*d,const char*heading,const char*details){
    if(!d->ready)return;
    result_page(d,DISPLAY_INFO,heading?heading:"",details?details:"",NULL,ICON_NONE,0,HOLD_MS);
}
void display_swipe(display_t*d,const char*name,const char*action,int queued){
    (void)action;(void)queued;display_result(d,DISPLAY_PENDING,name);
}
int display_busy(display_t*d){
    if(!d->ready||!d->msg_active)return 0;
    if(mono_ms()<d->msg_until)return 1;
    d->msg_active=0;return 0;
}
void display_tick(display_t*d){
    if(!d->ready||!d->msg_active)return;
    if(mono_ms()>=d->msg_until)d->msg_active=0;
}


void display_aging(display_t*d,int contrast){
    if(!d->ready)return;
    /* Idle frames move one pixel left/right every four minutes (see shift_frame). */
    time_t now=time(NULL);d->shift=(int)((now/240)%3)-1;
    if(contrast!=d->last_dim){
        if(oled_contrast(&d->oled,(unsigned)contrast)==0){
            d->last_dim=contrast;
            fprintf(stderr,"OLED contrast applied: %d\n",contrast);
        }else fprintf(stderr,"OLED contrast write failed: %d\n",contrast);
    }
}
