#ifndef TC_DISPLAY_UI_H
#define TC_DISPLAY_UI_H
#include "oled_ttf.h"

/* What a card presentation produced. The kind picks the icon, wording, hold
   time and whether the page is drawn inverse (attention states). */
typedef enum {
    DISPLAY_IN,         /* text: first name */
    DISPLAY_OUT,        /* text: first name */
    DISPLAY_LATE,       /* text: first name */
    DISPLAY_PENDING,    /* text: first name; saved locally, waiting for the server */
    DISPLAY_DUPLICATE,  /* text: first name */
    DISPLAY_REVIEW,     /* text: first name; stored but not classified */
    DISPLAY_VACATION, /* recognized card, no attendance */
    DISPLAY_TEST,       /* text: first name or ""; attendance disabled */
    DISPLAY_MANAGER,    /* text ignored */
    DISPLAY_ENROLL,     /* text ignored */
    DISPLAY_APPROVED,   /* manager system action */
    DISPLAY_REJECT,     /* text: reason title, e.g. "INVALID CARD"; nothing stored */
    DISPLAY_INFO        /* via display_message */
} display_kind_t;

typedef struct {
    oled_t oled;ttf_t*font;int ready;char clock_id[50];int shift;int last_dim;int hour24;
    /* Active confirmation page (monotonic milliseconds). */
    int msg_active,msg_kind,msg_invert,msg_bar,msg_spin;
    int wifi_dbm,wifi_bars,wifi_associated;
    long long msg_start,msg_until;
} display_t;

int display_open(display_t*d,const char*i2c,unsigned addr,const char*ttf,const char*id);
void display_close(display_t*d);
void display_idle(display_t*d);
void display_idle_status(display_t*d,int pending,int online);
void display_wifi_status(display_t*d,int associated,int dbm,int bars);
void display_result(display_t*d,display_kind_t kind,const char*text);
void display_swipe(display_t*d,const char*name,const char*action,int queued);
void display_message(display_t*d,const char*heading,const char*details);
/* Animate the active confirmation (countdown bar, pending spinner). Cheap to call
   every loop: it only touches the panel when a pixel actually changes. */
void display_tick(display_t*d);
/* Nonzero while a confirmation page is still being held on screen. */
int display_busy(display_t*d);
void display_aging(display_t*d,int contrast);
void display_boot_logo(display_t*d);
#endif
