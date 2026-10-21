#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include "rc522.h"
#include <sys/timex.h>
#include "display_ui.h"
#include "wifi_signal.h"
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sqlite3.h>
#include <curl/curl.h>
#include <pthread.h>
#include <stdatomic.h>
static volatile sig_atomic_t quitting=0;

static void stop(int s){(void)s;quitting=1;}

static _Thread_local sqlite3 *db=NULL;
static atomic_int server_online=0;
static atomic_int dim_enabled=1,dim_start=1140,dim_end=420,dim_normal=127,dim_low=24;
static char configured_clock_id[49]="";
static struct curl_slist *with_clock_id(struct curl_slist *h){char field[80];snprintf(field,sizeof field,"X-Clock-ID: %s",configured_clock_id);return curl_slist_append(h,field);}

/* Only the newest, freshly presented card may cause a classification sound.
   Old offline uploads never trigger delayed audible feedback. */
static pthread_mutex_t feedback_lock=PTHREAD_MUTEX_INITIALIZER;
static char feedback_event[40]="";
static time_t feedback_started=0;
static char feedback_first_name[40]="";
static atomic_int feedback_result=0; /* 1 IN, 2 OUT, 3 LATE, 4 ERROR, 5 DUPLICATE (silent) */
static void feedback_watch(const char *event,const char *first_name){
 pthread_mutex_lock(&feedback_lock);
 snprintf(feedback_event,sizeof feedback_event,"%s",event);
 feedback_started=time(NULL);
 snprintf(feedback_first_name,sizeof feedback_first_name,"%.19s",first_name?first_name:"");
 atomic_store(&feedback_result,0);
 pthread_mutex_unlock(&feedback_lock);
}
static void feedback_reply(const char *event,int result){
 pthread_mutex_lock(&feedback_lock);
 if(!strcmp(event,feedback_event)&&time(NULL)-feedback_started<=5){
   atomic_store(&feedback_result,result);
   feedback_event[0]=0;
 }
 pthread_mutex_unlock(&feedback_lock);
}

/* A completed card refresh or successful swipe upload proves recent connectivity. */
static pthread_mutex_t frame_lock=PTHREAD_MUTEX_INITIALIZER;
static uint8_t shared_frame[OLED_BYTES];
static int shared_frame_ready=0;
static unsigned long frame_generation=0;
static atomic_int preview_urgent=0;
static display_t disp;
/* Used by preview telemetry before the queue networking helpers. */
static size_t discard(char *p,size_t a,size_t b,void *u);
/* Publish the same monochrome framebuffer sent to the SSD1309.
   Telemetry is best effort, never deletes or delays a swipe. */
static void publish_oled(const char *base,const char *key,const char *clock,const uint8_t *frame){
 /* Each publish is a SQLite write on the SD card. Send only when the frame changed,
    plus a 60 s heartbeat so the dashboard timestamp stays fresh. */
 static uint8_t last_frame[OLED_BYTES];static int have_last=0;static time_t last_sent=0;
 time_t now=time(NULL);
 if(have_last&&!memcmp(last_frame,frame,OLED_BYTES)&&now-last_sent<60)return;
 memcpy(last_frame,frame,OLED_BYTES);have_last=1;last_sent=now;
 char hex[OLED_BYTES*2+1];
 static const char h[]="0123456789abcdef";
 for(unsigned i=0;i<OLED_BYTES;i++){
  hex[i*2]=h[frame[i]>>4];
  hex[i*2+1]=h[frame[i]&15];
 }
 hex[OLED_BYTES*2]=0;
 char url[768],header[256];
 snprintf(url,sizeof url,"%s/api/v1/clock/preview",base);
 snprintf(header,sizeof header,"X-Device-Key: %s",key);
 CURL *c=curl_easy_init();if(!c)return;
 struct curl_slist *hs=curl_slist_append(NULL,header);
 hs=curl_slist_append(with_clock_id(hs),"Content-Type: application/json");
 char body[2300];
 snprintf(body,sizeof body,"{\"clock_id\":\"%s\",\"frame\":\"%s\"}",clock,hex);
 curl_easy_setopt(c,CURLOPT_URL,url);
 curl_easy_setopt(c,CURLOPT_HTTPHEADER,hs);
 curl_easy_setopt(c,CURLOPT_POSTFIELDS,body);
 curl_easy_setopt(c,CURLOPT_TIMEOUT_MS,650L);
 curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,discard);
 curl_easy_perform(c);
 curl_slist_free_all(hs);curl_easy_cleanup(c);
}

/* GPIO character-device v2 (Linux >=5.10). No v1 compatibility dependency. */
static int gpio_line(const char *chip,unsigned line,int output,const char *label){
    int fd=open(chip,O_RDONLY|O_CLOEXEC);
    if(fd<0)return -1;
    struct gpio_v2_line_request req={0};
    req.num_lines=1;
    req.offsets[0]=line;
    req.config.flags=output?GPIO_V2_LINE_FLAG_OUTPUT:GPIO_V2_LINE_FLAG_INPUT;
    snprintf(req.consumer,sizeof req.consumer,"timeclock-%s",label);
    int rc=ioctl(fd,GPIO_V2_GET_LINE_IOCTL,&req);
    int saved=errno;
    close(fd);
    if(rc<0){errno=saved;return -1;}
    return req.fd;
}
static int gpio_output(const char *chip,unsigned line){return gpio_line(chip,line,1,"buzzer");}
static void gpio_set(int fd,int value){
    if(fd<0)return;
    struct gpio_v2_line_values v={.mask=1,.bits=value?1U:0U};
    if(ioctl(fd,GPIO_V2_LINE_SET_VALUES_IOCTL,&v)<0)perror("buzzer set");
}

static void pause_ms(int ms){
    struct timespec t={.tv_sec=ms/1000,.tv_nsec=(long)(ms%1000)*1000000};
    while(nanosleep(&t,&t)&&errno==EINTR&&!quitting){}
}
/* Nonblocking GPIO buzzer patterns traced from the supplied 44.1 kHz WAVs.
 * An active buzzer reproduces on/off timing, not the WAV pitch or timbre.
 * Each row is an alternating ON/OFF duration in milliseconds. */
#include "buzzer_patterns.h"
struct beep_state {int fd;const struct buzzer_pattern *pattern;unsigned index;int active;struct timespec until;};
static struct beep_state buzzer_state;
static struct timespec deadline_ms(int ms){
 struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);
 t.tv_nsec+=(long)ms*1000000L;t.tv_sec+=t.tv_nsec/1000000000L;
 t.tv_nsec%=1000000000L;return t;
}
static int elapsed(struct timespec d){struct timespec n;clock_gettime(CLOCK_MONOTONIC,&n);
 return n.tv_sec>d.tv_sec||(n.tv_sec==d.tv_sec&&n.tv_nsec>=d.tv_nsec);
}
static void play_pattern(int fd,const struct buzzer_pattern *pattern){
 if(fd<0)return;
 gpio_set(fd,0);
 buzzer_state=(struct beep_state){.fd=fd,.pattern=pattern,.index=0,.active=1};
 if(pattern&&pattern->length){gpio_set(fd,1);buzzer_state.until=deadline_ms(pattern->durations[0]);}
 else buzzer_state.active=0;
}
static void beep_tick(void){
 struct beep_state *b=&buzzer_state;
 if(!b->active||!elapsed(b->until))return;
 ++b->index;
 if(b->index>=b->pattern->length){gpio_set(b->fd,0);b->active=0;return;}
 gpio_set(b->fd,(b->index%2)==0);
 b->until=deadline_ms(b->pattern->durations[b->index]);
}
static void capture_frame(void){
 if(!disp.ready)return;
 pthread_mutex_lock(&frame_lock);
 memcpy(shared_frame,disp.oled.fb,OLED_BYTES);shared_frame_ready=1;frame_generation++;
 pthread_mutex_unlock(&frame_lock);
}


static int sql(const char *s){char *e=NULL;int ok=sqlite3_exec(db,s,0,0,&e)==SQLITE_OK;if(!ok){fprintf(stderr,"sqlite: %s\n",e?e:"error");sqlite3_free(e);}return ok;}
static int safe_id(const char *s,size_t max){size_t n=strlen(s);if(!n||n>max)return 0;for(;*s;s++)if(!isalnum((unsigned char)*s)&&!strchr("-_:.+",*s))return 0;return 1;}
static int local_card(const char *uid,char *name,size_t capacity){
 sqlite3_stmt *q=NULL;int valid=0;name[0]=0;
 if(sqlite3_prepare_v2(db,"SELECT first_name,attendance_mode FROM card_cache WHERE uid=?1 AND active=1",-1,&q,NULL)==SQLITE_OK){
    sqlite3_bind_text(q,1,uid,-1,SQLITE_TRANSIENT);
    if(sqlite3_step(q)==SQLITE_ROW){valid=sqlite3_column_int(q,1)==1?2:1;const unsigned char *n=sqlite3_column_text(q,0);snprintf(name,capacity,"%s",n?(const char*)n:"");}
 }sqlite3_finalize(q);return valid;
}
/* Presence is tracked for each UID, not just the last UID polled. */
#define PRESENT_SLOTS 16
struct uid_presence {char uid[24];struct timespec seen;int active;int enrolled;};
static struct uid_presence presence[PRESENT_SLOTS];
static double seconds_since(struct timespec a,struct timespec b){
 return (double)(a.tv_sec-b.tv_sec)+(double)(a.tv_nsec-b.tv_nsec)/1000000000.0;
}
static int first_presentation(const char *uid,struct timespec now){
 int slot=-1;
 for(int i=0;i<PRESENT_SLOTS;i++){
  if(presence[i].uid[0]&&!strcmp(presence[i].uid,uid)){
   if(seconds_since(now,presence[i].seen)>=1.0){presence[i].active=0;presence[i].enrolled=0;}
   presence[i].seen=now;
   if(presence[i].active)return 0;
   presence[i].active=1;return 1;
  }
  if(!presence[i].active && (slot<0 || !presence[i].uid[0] || seconds_since(now,presence[i].seen)>seconds_since(now,presence[slot].seen)))slot=i;
 }
 if(slot<0)return 0; /* Full, continuously occupied field: fail closed. */
 snprintf(presence[slot].uid,sizeof presence[slot].uid,"%s",uid);
 presence[slot].seen=now;presence[slot].active=1;presence[slot].enrolled=0;return 1;
}
static void mark_enrolled(const char *uid){for(int i=0;i<PRESENT_SLOTS;i++)if(presence[i].active&&!strcmp(presence[i].uid,uid)){presence[i].enrolled=1;return;}}
static void expire_presentations(struct timespec now){
 for(int i=0;i<PRESENT_SLOTS;i++)if(presence[i].active && seconds_since(now,presence[i].seen)>=1.0){presence[i].active=0;presence[i].enrolled=0;}
}
/* The agent never opens the authority database, including on the primary's reader.
   The same authenticated scan API is used by every clock. */
static size_t scan_collect(char*p,size_t sz,size_t count,void *user){
 struct bounded {char data[256];size_t n;} *b=user;size_t n=sz*count;
 if(n>sizeof b->data-b->n-1)return 0;
 memcpy(b->data+b->n,p,n);
 b->n+=n;
 b->data[b->n]=0;
 return n;
}
static int remote_scan(const char *base,const char *key,const char *uid,int *manager){
 char url[768],header[256],body[180];
 if(snprintf(url,sizeof url,"%s/api/v1/clock/scan",base)>=(int)sizeof url)return 0;
 snprintf(header,sizeof header,"X-Device-Key: %s",key);
 snprintf(body,sizeof body,"{\"clock_id\":\"%s\",\"card_uid\":\"%s\"}",configured_clock_id,uid);
 CURL*c=curl_easy_init();if(!c)return 0;
 struct curl_slist*h=with_clock_id(curl_slist_append(NULL,header));h=curl_slist_append(h,"Content-Type: application/json");
 curl_easy_setopt(c,CURLOPT_URL,url);curl_easy_setopt(c,CURLOPT_HTTPHEADER,h);
 curl_easy_setopt(c,CURLOPT_POSTFIELDS,body);curl_easy_setopt(c,CURLOPT_TIMEOUT_MS,1200L);
 /* Small bounded response, no credentials or employee records. */
 struct bounded {char data[256];size_t n;} result={{0},0};
 curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,scan_collect);curl_easy_setopt(c,CURLOPT_WRITEDATA,&result);
 CURLcode rc=curl_easy_perform(c);long http=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&http);
 curl_slist_free_all(h);curl_easy_cleanup(c);
 if(rc!=CURLE_OK||http!=200)return 0;
 if(strstr(result.data,"\"result\":\"APPROVED\""))return 3;
 if(strstr(result.data,"\"result\":\"ENROLLED\""))return 2;
 if(strstr(result.data,"\"result\":\"MANAGER\"")){*manager=1;return 1;}
 return 1;
}
/* The RFID loop never performs HTTP. Scan decisions run in an independent
 * worker, preserving enrollment priority before any attendance action. */
#define SCAN_SLOTS 32
struct scan_slot {char uid[24];struct timespec started;int state;int result;int manager;};
static struct scan_slot scans[SCAN_SLOTS];
static pthread_mutex_t scan_lock=PTHREAD_MUTEX_INITIALIZER;
static int enqueue_scan(const char *uid,struct timespec now){
 int chosen=-1;
 pthread_mutex_lock(&scan_lock);
 for(int i=0;i<SCAN_SLOTS;i++)if(scans[i].state==0){chosen=i;break;}
 if(chosen>=0){snprintf(scans[chosen].uid,sizeof scans[chosen].uid,"%s",uid);
  scans[chosen].started=now;scans[chosen].result=0;scans[chosen].manager=0;scans[chosen].state=1;}
 pthread_mutex_unlock(&scan_lock);
 return chosen;
}
struct scan_worker_args {const char *base,*key;};
static void *scan_worker(void *v){
 const struct scan_worker_args *args=v;
 while(!quitting){
  int slot=-1;char uid[24]={0};
  pthread_mutex_lock(&scan_lock);
  for(int i=0;i<SCAN_SLOTS;i++)if(scans[i].state==1){slot=i;scans[i].state=2;snprintf(uid,sizeof uid,"%s",scans[i].uid);break;}
  pthread_mutex_unlock(&scan_lock);
  if(slot<0){pause_ms(20);continue;}
  int manager=0,result=remote_scan(args->base,args->key,uid,&manager);
  pthread_mutex_lock(&scan_lock);
  if(scans[slot].state==2){scans[slot].manager=manager;scans[slot].result=result;scans[slot].state=3;}
  else if(scans[slot].state==4)scans[slot].state=0;
  pthread_mutex_unlock(&scan_lock);
 }
 return NULL;
}
static int pending_count(void){sqlite3_stmt*s=NULL;int n=-1;if(sqlite3_prepare_v2(db,"SELECT COUNT(*) FROM pending",-1,&s,NULL)==SQLITE_OK&&sqlite3_step(s)==SQLITE_ROW)n=sqlite3_column_int(s,0);sqlite3_finalize(s);return n;}
/* Synchronized if systemd-timesyncd says so, or if the kernel clock is not flagged
   unsynchronized (chrony, ntpsec and ntpd all clear STA_UNSYNC via adjtimex). */
static int time_synchronized(void){
 if(access("/run/systemd/timesync/synchronized",F_OK)==0)return 1;
 struct timex tx;memset(&tx,0,sizeof tx);
 int state=adjtimex(&tx);
 return state!=-1&&state!=TIME_ERROR&&!(tx.status&STA_UNSYNC);
}
/* Local IN/OUT inference only needs today; keep a few days and drop the rest. */

static int save(const char *uid,const char *clock,char out_event[40]){unsigned char bytes[16];int r=open("/dev/urandom",O_RDONLY);if(r<0)return 0;ssize_t n=read(r,bytes,sizeof bytes);close(r);if(n!=(ssize_t)sizeof bytes)return 0;char id[33],stamp[40];for(int i=0;i<16;i++)snprintf(id+2*i,3,"%02x",bytes[i]);time_t now=time(0);struct tm t;gmtime_r(&now,&t);strftime(stamp,sizeof stamp,"%Y-%m-%dT%H:%M:%SZ",&t);sqlite3_stmt *s=0;int ok=sqlite3_prepare_v2(db,"INSERT INTO pending(event_id,clock_id,card_uid,swiped_at) VALUES(?1,?2,?3,?4)",-1,&s,0)==SQLITE_OK;if(ok){sqlite3_bind_text(s,1,id,-1,SQLITE_TRANSIENT);sqlite3_bind_text(s,2,clock,-1,SQLITE_TRANSIENT);sqlite3_bind_text(s,3,uid,-1,SQLITE_TRANSIENT);sqlite3_bind_text(s,4,stamp,-1,SQLITE_TRANSIENT);ok=sqlite3_step(s)==SQLITE_DONE;}sqlite3_finalize(s);if(ok)snprintf(out_event,40,"%s",id);return ok;}
static size_t discard(char *p,size_t a,size_t b,void*u){(void)p;(void)u;return a*b;}
static void unknown_card(const char *uid){
 sqlite3_stmt *q=NULL;
 int ok=sqlite3_prepare_v2(db,"INSERT OR IGNORE INTO unknown_pending(uid,first_seen) VALUES(?1,strftime('%Y-%m-%dT%H:%M:%fZ','now'))",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,uid,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}
 sqlite3_finalize(q);
 if(!ok)fprintf(stderr,"unknown card sighting not retained: %s\n",sqlite3_errmsg(db));
}
static void flush_unknown(const char *base,const char *key,const char *clock){
 char url[768],header[256];if(snprintf(url,sizeof url,"%s/api/v1/clock/unknown",base)>=(int)sizeof url)return;
 snprintf(header,sizeof header,"X-Device-Key: %s",key);
 CURL *c=curl_easy_init();if(!c)return;
 struct curl_slist *h=curl_slist_append(NULL,header);h=curl_slist_append(with_clock_id(h),"Content-Type: application/json");
 curl_easy_setopt(c,CURLOPT_URL,url);curl_easy_setopt(c,CURLOPT_HTTPHEADER,h);
 curl_easy_setopt(c,CURLOPT_TIMEOUT_MS,1500L);curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,discard);
 sqlite3_stmt *q=NULL;
 if(sqlite3_prepare_v2(db,"SELECT uid FROM unknown_pending LIMIT 10",-1,&q,NULL)==SQLITE_OK){
   while(!quitting && sqlite3_step(q)==SQLITE_ROW){
     const char *uid=(const char *)sqlite3_column_text(q,0);if(!uid)continue;
     char body[300];snprintf(body,sizeof body,"{\"clock_id\":\"%s\",\"card_uid\":\"%s\"}",clock,uid);
     curl_easy_setopt(c,CURLOPT_POSTFIELDS,body);
     CURLcode rc=curl_easy_perform(c);long status=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&status);
     if(rc!=CURLE_OK||status!=200)break;
     sqlite3_stmt *del=NULL;
     if(sqlite3_prepare_v2(db,"DELETE FROM unknown_pending WHERE uid=?1",-1,&del,NULL)==SQLITE_OK){
       sqlite3_bind_text(del,1,uid,-1,SQLITE_TRANSIENT);sqlite3_step(del);
     }sqlite3_finalize(del);
   }
 }sqlite3_finalize(q);curl_slist_free_all(h);curl_easy_cleanup(c);
}
static int mark_rejected(const char *id,long status,const char *reason){
    sqlite3_stmt *q=NULL;int ok=sqlite3_prepare_v2(db,
        "INSERT INTO rejected(event_id,clock_id,card_uid,swiped_at,http_status,reason,rejected_at) "
        "SELECT event_id,clock_id,card_uid,swiped_at,?2,?3,strftime('%Y-%m-%dT%H:%M:%fZ','now') "
        "FROM pending WHERE event_id=?1",-1,&q,NULL)==SQLITE_OK;
    if(ok){sqlite3_bind_text(q,1,id,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,2,(int)status);
        sqlite3_bind_text(q,3,reason,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}
    sqlite3_finalize(q);
    if(ok){q=NULL;ok=sqlite3_prepare_v2(db,"DELETE FROM pending WHERE event_id=?1",-1,&q,NULL)==SQLITE_OK;
        if(ok){sqlite3_bind_text(q,1,id,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);}
    return ok;
}
struct buffer{char*data;size_t len;};static size_t collect(char*p,size_t n,size_t z,void*u){struct buffer*b=u;size_t k=n*z;if(b->len+k>1048576)return 0;char*t=realloc(b->data,b->len+k+1);if(!t)return 0;b->data=t;memcpy(b->data+b->len,p,k);b->len+=k;b->data[b->len]=0;return k;}
static int flush(const char *base,const char *key){
    char url[768];if(snprintf(url,sizeof url,"%s/api/v1/swipes",base)>=(int)sizeof url)return -1;
    CURL*c=curl_easy_init();if(!c)return -1;
    struct curl_slist*h=NULL;char hdr[256];snprintf(hdr,sizeof hdr,"X-Device-Key: %s",key);
    h=curl_slist_append(h,hdr);h=curl_slist_append(with_clock_id(h),"Content-Type: application/json");
    curl_easy_setopt(c,CURLOPT_HTTPHEADER,h);curl_easy_setopt(c,CURLOPT_URL,url);
    curl_easy_setopt(c,CURLOPT_TIMEOUT,3L);curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,collect);
    sqlite3_stmt*s=NULL;int sent=0;
    if(sqlite3_prepare_v2(db,"SELECT event_id,clock_id,card_uid,swiped_at FROM pending ORDER BY rowid LIMIT 30",-1,&s,NULL)==SQLITE_OK){
        while(!quitting && sqlite3_step(s)==SQLITE_ROW){
            const char *a=(const char*)sqlite3_column_text(s,0),*b=(const char*)sqlite3_column_text(s,1);
            const char *d=(const char*)sqlite3_column_text(s,2),*e=(const char*)sqlite3_column_text(s,3);
            char body[512];snprintf(body,sizeof body,"{\"event_id\":\"%s\",\"clock_id\":\"%s\",\"card_uid\":\"%s\",\"swiped_at\":\"%s\"}",a,b,d,e);
            struct buffer reply={0};curl_easy_setopt(c,CURLOPT_WRITEDATA,&reply);
            curl_easy_setopt(c,CURLOPT_POSTFIELDS,body);
            CURLcode rc=curl_easy_perform(c);long http=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&http);
            if(rc!=CURLE_OK){free(reply.data);fprintf(stderr,"sync transport error: %s\n",curl_easy_strerror(rc));sent=-1;break;}
            if(http==200){
                int feedback=0;
                if(reply.data){if(strstr(reply.data,"\"classification\":\"DUPLICATE\""))feedback=5;
                 else if(strstr(reply.data,"\"classification\":\"IN_LATE\""))feedback=3;
                 else if(strstr(reply.data,"\"classification\":\"IN\""))feedback=1;
                 else if(strstr(reply.data,"\"classification\":\"OUT\""))feedback=2;
                 else feedback=4;}
                free(reply.data);
                sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(db,"DELETE FROM pending WHERE event_id=?1",-1,&q,NULL)==SQLITE_OK;
                if(ok){sqlite3_bind_text(q,1,a,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}
                sqlite3_finalize(q);if(!ok){sent=-1;break;}if(feedback)feedback_reply(a,feedback);sent++;continue;
            }
            /* Bad event is permanent. Auth failure, wrong endpoint, rate limits and server issues are not. */
            free(reply.data);
            if(http==400||http==409||http==422){
                char event_id[90];snprintf(event_id,sizeof event_id,"%s",a);
                if(!sql("SAVEPOINT reject_event;")){sent=-1;break;}
                if(mark_rejected(event_id,http,"server rejected event")){
                    if(!sql("RELEASE reject_event;")){sent=-1;break;}
                    fprintf(stderr,"REJECTED event %s (HTTP %ld), inspect local rejected table\n",event_id,http);
                    continue;
                }
                sql("ROLLBACK TO reject_event;");sql("RELEASE reject_event;");sent=-1;break;
            }
            fprintf(stderr,"sync HTTP %ld; retaining pending events\n",http);sent=-1;break;
        }
    }else sent=-1;
    sqlite3_finalize(s);curl_slist_free_all(h);curl_easy_cleanup(c);return sent;
}


/* Persist a verified per-clock font choice for existing OLED hot-reload logic.
   Avoid unnecessary SD writes when a periodic settings fetch is unchanged. */
static void apply_remote_font(const char *name){
 static const char *allowed[]={"DejaVuSansMono.ttf","DejaVuSans.ttf","DejaVuSerif.ttf","DejaVuSansMono-Bold.ttf","DejaVuSans-Bold.ttf","DejaVuSerif-Bold.ttf"};
 int valid=0;for(size_t i=0;i<sizeof allowed/sizeof allowed[0];i++)if(!strcmp(name,allowed[i]))valid=1;
 if(!valid)return;
 char path[256];snprintf(path,sizeof path,"/usr/share/fonts/truetype/dejavu/%s",name);
 if(access(path,R_OK)!=0)return;
 const char *dest="/var/lib/timeclock/display-font.txt",*tmp="/var/lib/timeclock/display-font.txt.tmp";
 FILE *f=fopen(dest,"r");if(f){char old[80]={0};if(fgets(old,sizeof old,f)){old[strcspn(old,"\r\n")]=0;if(!strcmp(old,name)){fclose(f);return;}}fclose(f);}
 f=fopen(tmp,"w");if(!f)return;
 if(fprintf(f,"%s\n",name)<0||fclose(f)!=0){unlink(tmp);return;}
 if(rename(tmp,dest)!=0)unlink(tmp);
}
static void refresh_dim_settings(const char *base,const char *key){
 char url[768],hdr[256];snprintf(url,sizeof url,"%s/api/v1/clock/dim",base);snprintf(hdr,sizeof hdr,"X-Device-Key: %s",key);
 CURL*c=curl_easy_init();if(!c)return;struct curl_slist*h=with_clock_id(curl_slist_append(NULL,hdr));struct buffer b={0};
 curl_easy_setopt(c,CURLOPT_URL,url);curl_easy_setopt(c,CURLOPT_HTTPHEADER,h);curl_easy_setopt(c,CURLOPT_TIMEOUT_MS,1000L);curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,collect);curl_easy_setopt(c,CURLOPT_WRITEDATA,&b);
 CURLcode err=curl_easy_perform(c);long status=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&status);
 curl_slist_free_all(h);curl_easy_cleanup(c);
 if(err==CURLE_OK&&status==200&&b.data){int e,st,en,n,d;
  if(sscanf(b.data,"{\"enabled\":%d,\"start_min\":%d,\"end_min\":%d,\"normal_contrast\":%d,\"dim_contrast\":%d}",&e,&st,&en,&n,&d)==5&&e>=0&&e<=1&&st>=0&&st<1440&&en>=0&&en<1440&&st!=en&&n>=1&&n<=255&&d>=1&&d<=n){
    atomic_store(&dim_enabled,e);atomic_store(&dim_start,st);atomic_store(&dim_end,en);atomic_store(&dim_normal,n);atomic_store(&dim_low,d);
    const char *key=strstr(b.data,",\"font\":\"");
    if(key){char name[80]={0};if(sscanf(key,",\"font\":\"%79[^\"]\"",name)==1)apply_remote_font(name);}
  }
 }free(b.data);
}
static int refresh_cache(const char *base,const char *key){char url[768];snprintf(url,sizeof url,"%s/api/v1/clock/cards",base);CURL*c=curl_easy_init();if(!c)return 0;char hdr[256];snprintf(hdr,sizeof hdr,"X-Device-Key: %s",key);struct curl_slist*h=with_clock_id(curl_slist_append(NULL,hdr));struct buffer b={0};curl_easy_setopt(c,CURLOPT_HTTPHEADER,h);curl_easy_setopt(c,CURLOPT_URL,url);curl_easy_setopt(c,CURLOPT_TIMEOUT,4L);curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,collect);curl_easy_setopt(c,CURLOPT_WRITEDATA,&b);CURLcode rc=curl_easy_perform(c);long status=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&status);curl_slist_free_all(h);curl_easy_cleanup(c);if(rc!=CURLE_OK||status!=200){free(b.data);return 0;}if(!b.data){b.data=calloc(1,1);if(!b.data)return 0;}int ok=sql("BEGIN IMMEDIATE; DELETE FROM card_cache;");
 sqlite3_stmt *insert=NULL;
 if(ok)ok=sqlite3_prepare_v2(db,
  "INSERT INTO card_cache(uid,active,first_name,attendance_mode) VALUES(?1,1,?2,?3)",-1,&insert,NULL)==SQLITE_OK;
 char*saveptr=NULL;char*record=strtok_r(b.data,"\n",&saveptr);
 while(ok&&record){
     char *tab=strchr(record,'\t');if(!tab){ok=0;break;}*tab=0;
     const char *uid=record,*name=tab+1;char *mode_col=strchr((char*)name,'\t');
     int vacation_only=0;
     if(mode_col){*mode_col++=0;if(!strcmp(mode_col,"vacation_only"))vacation_only=1;else if(strcmp(mode_col,"punch_required")){ok=0;break;}}
     size_t n=strlen(uid);
     if(n!=8&&n!=14&&n!=20){fprintf(stderr,"skipping malformed card cache UID length: %zu\n",n);record=strtok_r(NULL,"\n",&saveptr);continue;}
     int valid=1;for(size_t i=0;i<n;i++)if(!((uid[i]>='0'&&uid[i]<='9')||(uid[i]>='A'&&uid[i]<='F'))){valid=0;break;}
     if(!valid||strlen(name)>64){fprintf(stderr,"skipping malformed card cache row\n");record=strtok_r(NULL,"\n",&saveptr);continue;}
     sqlite3_reset(insert);
     sqlite3_clear_bindings(insert);
     sqlite3_bind_text(insert,1,uid,-1,SQLITE_TRANSIENT);
     sqlite3_bind_text(insert,2,name,-1,SQLITE_TRANSIENT);
     sqlite3_bind_int(insert,3,vacation_only);
     ok=sqlite3_step(insert)==SQLITE_DONE;
     record=strtok_r(NULL,"\n",&saveptr);
 }
 sqlite3_finalize(insert);
 if(ok)ok=sql("COMMIT;");else sql("ROLLBACK;");free(b.data);return ok;}
static void flush_rejected(const char *base,const char *key){
 char url[768],header[256];if(snprintf(url,sizeof url,"%s/api/v1/clock/rejected",base)>=(int)sizeof url)return;
 snprintf(header,sizeof header,"X-Device-Key: %s",key);CURL*c=curl_easy_init();if(!c)return;
 struct curl_slist*h=curl_slist_append(NULL,header);h=curl_slist_append(with_clock_id(h),"Content-Type: application/x-www-form-urlencoded");
 curl_easy_setopt(c,CURLOPT_URL,url);curl_easy_setopt(c,CURLOPT_HTTPHEADER,h);curl_easy_setopt(c,CURLOPT_TIMEOUT_MS,1500L);curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,discard);
 sqlite3_stmt*q=NULL;
 if(sqlite3_prepare_v2(db,"SELECT event_id,clock_id,card_uid,swiped_at,http_status,rejected_at FROM rejected WHERE synced_at IS NULL ORDER BY rejected_at LIMIT 10",-1,&q,NULL)==SQLITE_OK){
  while(!quitting && sqlite3_step(q)==SQLITE_ROW){
   const char*event=(const char*)sqlite3_column_text(q,0),*clock=(const char*)sqlite3_column_text(q,1),*uid=(const char*)sqlite3_column_text(q,2),*swiped=(const char*)sqlite3_column_text(q,3),*rejected=(const char*)sqlite3_column_text(q,5);
   if(!event||!clock||!uid||!swiped||!rejected)continue;
   char*e=curl_easy_escape(c,event,0),*cl=curl_easy_escape(c,clock,0),*u=curl_easy_escape(c,uid,0),*sw=curl_easy_escape(c,swiped,0),*r=curl_easy_escape(c,rejected,0);
   if(!e||!cl||!u||!sw||!r){curl_free(e);curl_free(cl);curl_free(u);curl_free(sw);curl_free(r);break;}
   char body[1024];snprintf(body,sizeof body,"event_id=%s&clock_id=%s&card_uid=%s&swiped_at=%s&http_status=%d&rejected_at=%s",e,cl,u,sw,sqlite3_column_int(q,4),r);
   curl_free(e);curl_free(cl);curl_free(u);curl_free(sw);curl_free(r);
   curl_easy_setopt(c,CURLOPT_POSTFIELDS,body);CURLcode rc=curl_easy_perform(c);long status=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&status);
   if(rc!=CURLE_OK||status!=200)break;
   sqlite3_stmt*upd=NULL;if(sqlite3_prepare_v2(db,"UPDATE rejected SET synced_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE event_id=?1",-1,&upd,NULL)==SQLITE_OK){sqlite3_bind_text(upd,1,event,-1,SQLITE_TRANSIENT);sqlite3_step(upd);}sqlite3_finalize(upd);
  }
 }sqlite3_finalize(q);curl_slist_free_all(h);curl_easy_cleanup(c);
}
struct network_args {const char *base,*key,*clock,*queue_path;};
static void *network_worker(void *v){
 const struct network_args *a=v;
 if(sqlite3_open(a->queue_path,&db)!=SQLITE_OK){fprintf(stderr,"network SQLite open failed\n");return NULL;}
 sqlite3_busy_timeout(db,1000);
 /* Queue is the sole copy of unsent punches, so keep its writes durable. */
 if(!sql("PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;")){sqlite3_close(db);db=NULL;return NULL;}
 time_t last_cache=0,last_preview=0;
 unsigned long last_published_generation=0;
 while(!quitting){
  time_t now=time(NULL);
  if(now-last_cache>=30){
   int ok=refresh_cache(a->base,a->key);atomic_store(&server_online,ok);
   /* Display settings must refresh even if card-cache synchronization fails. */
   if(!quitting)refresh_dim_settings(a->base,a->key);
   if(!quitting)flush_unknown(a->base,a->key,a->clock);
   if(!quitting)flush_rejected(a->base,a->key);
   last_cache=now;
  }
  if(quitting)break;
  int sent=flush(a->base,a->key);
  if(sent<0)atomic_store(&server_online,0);
  else if(sent>0)atomic_store(&server_online,1);
  uint8_t frame[OLED_BYTES];int ready;unsigned long generation;
  pthread_mutex_lock(&frame_lock);ready=shared_frame_ready;generation=frame_generation;
  if(ready)memcpy(frame,shared_frame,sizeof frame);
  pthread_mutex_unlock(&frame_lock);
  /* Idle framebuffer changes each second; throttle preview writes, not physical OLED updates.
     Immediate publish for message transitions is handled through frame generation and message state. */
  if(!quitting && ready && (now-last_preview>=15 || (generation!=last_published_generation && now-last_preview>=1 && atomic_load(&preview_urgent)))){publish_oled(a->base,a->key,a->clock,frame);last_preview=now;last_published_generation=generation;atomic_store(&preview_urgent,0);}
  /* Short waits permit clean shutdown; network work never blocks RFID polling. */
  for(int i=0;i<10&&!quitting;i++)pause_ms(100);
 }
 sqlite3_close(db);db=NULL;return NULL;
}
/* Show a useful boot identity without waiting for networking or the server. */

static void process_scan(const char *uid,int scan_result,int is_manager,int attendance_enabled,
                         const char *clock,int buzzer){
     if(scan_result==3){display_result(&disp,DISPLAY_APPROVED,NULL);play_pattern(buzzer,&buzz_manager);atomic_store(&preview_urgent,1);capture_frame();return;}
     if(scan_result==2){
       {
         mark_enrolled(uid);
         fprintf(stderr,"Enrollment UID scanned: %s\n",uid);
         display_result(&disp,DISPLAY_ENROLL,NULL);
         play_pattern(buzzer,&buzz_ok);
       }
       atomic_store(&preview_urgent,1);capture_frame();return;
     }
     
     {
       char employee[70];int known=local_card(uid,employee,sizeof employee);
       if(is_manager||(known&&!strcmp(employee,"__MANAGER__"))){fprintf(stderr,"MANAGER CARD %s (no attendance)\n",uid);display_result(&disp,DISPLAY_MANAGER,NULL);play_pattern(buzzer,&buzz_manager);}
       else if(!attendance_enabled){
         fprintf(stderr,"RFID card %s (attendance disabled)\n",uid);
         display_result(&disp,DISPLAY_TEST,known?employee:"");
         play_pattern(buzzer,&buzz_ok);
       }
       else if(known==2){fprintf(stderr,"VACATION-ONLY card %s (no attendance)\n",uid);display_result(&disp,DISPLAY_VACATION,employee);play_pattern(buzzer,&buzz_ok);}
       else if(!known){unknown_card(uid);fprintf(stderr,"INVALID card %s\n",uid);display_result(&disp,DISPLAY_REJECT,"INVALID CARD");play_pattern(buzzer,&buzz_fail);}
       else if(!time_synchronized()){fprintf(stderr,"CLOCK UNSYNCED %s\n",uid);display_result(&disp,DISPLAY_REJECT,"CLOCK NOT SYNCED");play_pattern(buzzer,&buzz_fail);}
       else {char new_event[40]={0};if(!save(uid,clock,new_event)){fprintf(stderr,"QUEUE FAILURE %s\n",uid);display_result(&disp,DISPLAY_REJECT,"SAVE FAILED");play_pattern(buzzer,&buzz_fail);}
       else{
          feedback_watch(new_event,employee);
          fprintf(stderr,"SAVED LOCALLY %s (classification pending server)\n",uid);
          display_result(&disp,DISPLAY_PENDING,employee);
       }}
     }
}
#include "version.h"
int main(int argc,char**argv){if(argc<8){fprintf(stderr,"Usage: %s QUEUE_DB CLOCK_ID SERVER_URL KEY SPI_DEV GPIOCHIP BUZZER_OFFSET [I2C_DEV] [TTF_FONT] [I2C_ADDR_HEX]\n",argv[0]);return 2;}const char*dbpath=argv[1],*clock=argv[2],*base=argv[3],*key=argv[4];
 /* "-" means read DEVICE_KEY from the environment so the key is not visible in ps. */
 if(!strcmp(key,"-")){key=getenv("DEVICE_KEY");if(!key){fprintf(stderr,"DEVICE_KEY not set\n");return 2;}}
 snprintf(configured_clock_id,sizeof configured_clock_id,"%s",clock);
 if((strncmp(base,"http://127.0.0.1:",17) && strncmp(base,"https://",8))||strlen(key)<16||!safe_id(clock,48))return 2;
 int spi=rc522_open(argv[5],250000);if(spi<0){perror("RC522 SPI");return 1;}int buzzer=-1;if(strcmp(argv[7],"none")){buzzer=gpio_output(argv[6],(unsigned)strtoul(argv[7],NULL,10));if(buzzer<0){perror("buzzer GPIO");close(spi);return 1;}}if(sqlite3_open(dbpath,&db)!=SQLITE_OK)return 1;sqlite3_busy_timeout(db,3000);/* Preserve FULL on the source-of-truth offline swipe queue. */if(!sql("PRAGMA journal_mode=WAL;PRAGMA synchronous=FULL;CREATE TABLE IF NOT EXISTS pending(event_id TEXT PRIMARY KEY,clock_id TEXT NOT NULL,card_uid TEXT NOT NULL,swiped_at TEXT NOT NULL);CREATE TABLE IF NOT EXISTS card_cache(uid TEXT PRIMARY KEY COLLATE NOCASE,active INTEGER NOT NULL,first_name TEXT NOT NULL DEFAULT '',attendance_mode INTEGER NOT NULL DEFAULT 0 CHECK(attendance_mode IN (0,1)));CREATE TABLE IF NOT EXISTS unknown_pending(uid TEXT PRIMARY KEY COLLATE NOCASE,first_seen TEXT NOT NULL);CREATE TABLE IF NOT EXISTS rejected(event_id TEXT PRIMARY KEY,clock_id TEXT NOT NULL,card_uid TEXT NOT NULL,swiped_at TEXT NOT NULL,http_status INTEGER NOT NULL,reason TEXT NOT NULL,rejected_at TEXT NOT NULL,synced_at TEXT);"))return 1;
 int attendance_enabled=!getenv("ATTENDANCE_ENABLED") || strcmp(getenv("ATTENDANCE_ENABLED"),"no");
 fprintf(stderr,"Attendance recording: %s\n",attendance_enabled?"ENABLED":"DISABLED (enrollment/display only)");
 curl_global_init(CURL_GLOBAL_DEFAULT);
 if(argc>=10){unsigned a=argc>10?(unsigned)strtoul(argv[10],NULL,0):0x3C;if(display_open(&disp,argv[8],a,argv[9],clock)<0)fprintf(stderr,"OLED unavailable; RFID still operates\n");}
 signal(SIGINT,stop);signal(SIGTERM,stop);
 /* display_open() has already sent the boot logo as the first content frame.
    Hold it for two seconds, then transition directly to the idle clock. */
 if(disp.ready){
  for(int j=0;j<20&&!quitting;j++)pause_ms(100);
  if(!quitting)display_idle_status(&disp,0,0);
 }
 fprintf(stderr,"Clock started: %s; RC522 SPI; buzzer GPIO %s/%s\n",clock,argv[6],argv[7]);time_t last_display=0;int online=0;
 struct network_args na={base,key,clock,dbpath};pthread_t net_thread;
 if(pthread_create(&net_thread,NULL,network_worker,&na)!=0){fprintf(stderr,"network thread failed\n");return 1;}
 struct scan_worker_args scan_args={base,key};pthread_t scan_thread;
 if(pthread_create(&scan_thread,NULL,scan_worker,&scan_args)!=0){fprintf(stderr,"scan worker failed\n");quitting=1;pthread_join(net_thread,NULL);return 1;}
 
 int read_failure=0;time_t font_checked=0,wifi_checked=0;char loaded_font[100]="";
 while(!quitting){beep_tick();display_tick(&disp);int result=atomic_exchange(&feedback_result,0);
   if(result){
     char first_name[40];
     pthread_mutex_lock(&feedback_lock);
     snprintf(first_name,sizeof first_name,"%s",feedback_first_name);
     pthread_mutex_unlock(&feedback_lock);
     if(result==1){play_pattern(buzzer,&buzz_ok);display_result(&disp,DISPLAY_IN,first_name);}
     else if(result==2){play_pattern(buzzer,&buzz_out);display_result(&disp,DISPLAY_OUT,first_name);}
     else if(result==3){play_pattern(buzzer,&buzz_late);display_result(&disp,DISPLAY_LATE,first_name);}
     else if(result==4){play_pattern(buzzer,&buzz_fail);display_result(&disp,DISPLAY_REVIEW,first_name);}
     else if(result==5){display_result(&disp,DISPLAY_DUPLICATE,first_name);fprintf(stderr,"Duplicate swipe confirmed; no attendance sound\n");}
     last_display=0;
     atomic_store(&preview_urgent,1);
   }
   /* A completed HTTP classification is applied on this thread. Timed-out
    * requests use the same offline fallback as the former 1.2s call. */
   struct {char uid[24];int result,manager;} completed[SCAN_SLOTS];int ncompleted=0;
   struct timespec scan_clock;clock_gettime(CLOCK_MONOTONIC,&scan_clock);
   pthread_mutex_lock(&scan_lock);
   for(int i=0;i<SCAN_SLOTS;i++){
     if(scans[i].state==3 || ((scans[i].state==1 || scans[i].state==2) && seconds_since(scan_clock,scans[i].started)>=1.5)){
       snprintf(completed[ncompleted].uid,sizeof completed[ncompleted].uid,"%.23s",scans[i].uid);
       completed[ncompleted].result=scans[i].state==3?scans[i].result:0;
       completed[ncompleted].manager=scans[i].state==3?scans[i].manager:0;
       /* State 4 is abandoned and reserved for its in-flight worker only.
        * A queued slot can be freed immediately; it has no owner. */
       ncompleted++;scans[i].state=scans[i].state==2?4:0;
     }
   }
   pthread_mutex_unlock(&scan_lock);
   for(int i=0;i<ncompleted;i++)process_scan(completed[i].uid,completed[i].result,
      completed[i].manager,attendance_enabled,clock,buzzer);
   time_t now=time(0);online=atomic_load(&server_online);
   if(disp.ready && (wifi_checked==0||now-wifi_checked>=10)){
     wifi_checked=now;struct tc_wifi wifi=tc_wifi_read();
     if(disp.wifi_associated!=wifi.associated || disp.wifi_bars!=wifi.bars){
       display_wifi_status(&disp,wifi.associated,wifi.dbm,wifi.bars);last_display=0;
     }else disp.wifi_dbm=wifi.dbm;
   }
   if(disp.ready && now-font_checked>=5){font_checked=now;FILE*f=fopen("/var/lib/timeclock/display-font.txt","r");if(f){char name[100]={0};if(fgets(name,sizeof name,f)){name[strcspn(name,"\r\n")]=0;
     if(strcmp(name,loaded_font)&&strchr(name,'/')==NULL&&strchr(name,'\\')==NULL&&strncmp(name,"custom-",7)!=0){char file[256];snprintf(file,sizeof file,"/usr/share/fonts/truetype/dejavu/%s",name);ttf_t*next=ttf_open(file);if(next){ttf_t*old=disp.font;disp.font=next;ttf_close(old);snprintf(loaded_font,sizeof loaded_font,"%s",name);last_display=0;}}}fclose(f);}}

   if(disp.ready){
       struct tm local;localtime_r(&now,&local);
       int minute=local.tm_hour*60+local.tm_min,st=atomic_load(&dim_start),en=atomic_load(&dim_end);
       int active=atomic_load(&dim_enabled)&&(st<en?(minute>=st&&minute<en):(minute>=st||minute<en));
       display_aging(&disp,active?atomic_load(&dim_low):atomic_load(&dim_normal));
       if(!display_busy(&disp) && now!=last_display){
           display_idle_status(&disp,pending_count(),online);last_display=now;
       }
   }

   char uid[24]={0};int read=rc522_poll(spi,uid);
   struct timespec scan_now;clock_gettime(CLOCK_MONOTONIC,&scan_now);
   expire_presentations(scan_now);
   if(read==-2){fprintf(stderr,"RC522 failure: stopping agent for systemd recovery\n");read_failure=1;quitting=1;break;}
   if(read<0){pause_ms(100);continue;}
   if(read==1){
     int fresh=first_presentation(uid,scan_now);
     /* Enrollment takes priority, even when this UID is already present.
        A scan cannot create an attendance event. */
     if(fresh){
       if(atomic_load(&server_online)){
         if(enqueue_scan(uid,scan_now)<0){
           fprintf(stderr,"Remote scan queue full: refusing attendance until next presentation\n");
           display_result(&disp,DISPLAY_REJECT,"SCAN BUSY");play_pattern(buzzer,&buzz_fail);
         }else{display_message(&disp,"CARD READ","VERIFYING...");
           atomic_store(&preview_urgent,1);last_display=0;}
       }else process_scan(uid,0,0,attendance_enabled,clock,buzzer);
     }
   }
   if(display_busy(&disp))atomic_store(&preview_urgent,1);
   capture_frame();pause_ms(buzzer_state.active?2:20);}

 quitting=1;gpio_set(buzzer,0);buzzer_state.active=0;
 pthread_join(scan_thread,NULL);pthread_join(net_thread,NULL);if(disp.ready){display_message(&disp,"SHUTTING DOWN","PLEASE WAIT");capture_frame();pause_ms(850);}display_close(&disp);gpio_set(buzzer,0);if(buzzer>=0)close(buzzer);close(spi);sqlite3_close(db);curl_global_cleanup();return read_failure?1:0;}
