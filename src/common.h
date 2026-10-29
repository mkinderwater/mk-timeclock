#ifndef MK_TIMECLOCK_COMMON_H
#define MK_TIMECLOCK_COMMON_H
/* Shared helpers for timeclock-server and timeclock-mailer.
   One implementation of each rule: validation, escaping, SQLite access, email queue. */
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sqlite3.h>

/* ---------- Validation ---------- */

/* Strict YYYY-MM-DD: exact format, year 2000-2100, and a real calendar day. */
static inline int date_valid(const char *v){
 if(!v||strlen(v)!=10||v[4]!='-'||v[7]!='-')return 0;
 for(int i=0;i<10;i++)if(i!=4&&i!=7&&!isdigit((unsigned char)v[i]))return 0;
 int y=(v[0]-'0')*1000+(v[1]-'0')*100+(v[2]-'0')*10+(v[3]-'0');
 int m=(v[5]-'0')*10+(v[6]-'0'),d=(v[8]-'0')*10+(v[9]-'0');
 if(y<2000||y>2100||m<1||m>12||d<1||d>31)return 0;
 /* Noon avoids DST transitions that happen at local midnight. */
 struct tm t={0};t.tm_year=y-1900;t.tm_mon=m-1;t.tm_mday=d;t.tm_hour=12;t.tm_isdst=-1;
 time_t e=mktime(&t);struct tm out;
 if(e==(time_t)-1||!localtime_r(&e,&out))return 0;
 return out.tm_year==y-1900&&out.tm_mon==m-1&&out.tm_mday==d;
}

/* Strict HH:MM, 00:00 to 23:59. */
static inline int time_valid(const char *v){
 return v&&strlen(v)==5&&v[0]>='0'&&v[0]<='2'&&isdigit((unsigned char)v[1])&&v[2]==':'&&
        v[3]>='0'&&v[3]<='5'&&isdigit((unsigned char)v[4])&&strcmp(v,"24:00")<0;
}

/* One address rule for every recipient: 3-250 bytes, exactly one '@' with text
   on both sides, no spaces or control characters. */
static inline int email_address_ok(const char *v){
 if(!v)return 0;
 size_t n=strlen(v);if(n<3||n>250)return 0;
 const char *at=strchr(v,'@');
 if(!at||at==v||!at[1]||strchr(at+1,'@'))return 0;
 for(const unsigned char *p=(const unsigned char*)v;*p;p++)if(*p<=32||*p==127)return 0;
 return 1;
}

/* ---------- Output escaping ---------- */

static inline void json_text(sqlite3_str *o,const char *s){
 sqlite3_str_appendchar(o,1,'"');
 for(const unsigned char *p=(const unsigned char*)(s?s:"");*p;p++){
  switch(*p){
   case '"':sqlite3_str_appendall(o,"\\\"");break;
   case '\\':sqlite3_str_appendall(o,"\\\\");break;
   case '\n':sqlite3_str_appendall(o,"\\n");break;
   case '\r':sqlite3_str_appendall(o,"\\r");break;
   case '\t':sqlite3_str_appendall(o,"\\t");break;
   default:
    if(*p<32||*p==127)sqlite3_str_appendf(o,"\\u%04x",*p);
    else sqlite3_str_appendchar(o,1,(char)*p);
  }
 }
 sqlite3_str_appendchar(o,1,'"');
}

static inline void html_text(sqlite3_str *o,const char *s){
 for(;s&&*s;s++){
  switch(*s){
   case '&':sqlite3_str_appendall(o,"&amp;");break;
   case '<':sqlite3_str_appendall(o,"&lt;");break;
   case '>':sqlite3_str_appendall(o,"&gt;");break;
   case '\'':sqlite3_str_appendall(o,"&#39;");break;
   case '"':sqlite3_str_appendall(o,"&quot;");break;
   default:sqlite3_str_appendchar(o,1,*s);
  }
 }
}

/* ---------- SQLite ---------- */

/* Column text, never NULL. */
static inline const char *col_text(sqlite3_stmt *q,int col){
 const unsigned char *v=sqlite3_column_text(q,col);
 return v?(const char*)v:"";
}

/* Bind parameters ?1..?n from a type string:
   t text (NULL binds NULL), i int, I int64 (long long), n NULL. */
static inline int db_bindv(sqlite3_stmt *q,const char *types,va_list ap){
 int rc=SQLITE_OK;
 for(int i=0;types&&types[i]&&rc==SQLITE_OK;i++){
  switch(types[i]){
   case 't':{const char *s=va_arg(ap,const char*);rc=s?sqlite3_bind_text(q,i+1,s,-1,SQLITE_TRANSIENT):sqlite3_bind_null(q,i+1);break;}
   case 'i':rc=sqlite3_bind_int(q,i+1,va_arg(ap,int));break;
   case 'I':rc=sqlite3_bind_int64(q,i+1,va_arg(ap,long long));break;
   case 'n':rc=sqlite3_bind_null(q,i+1);break;
   default:rc=SQLITE_MISUSE;
  }
 }
 return rc==SQLITE_OK;
}

/* Prepare and bind. Returns NULL on failure (statement already finalized). */
static inline sqlite3_stmt *db_prepare(sqlite3 *d,const char *sql,const char *types,...){
 sqlite3_stmt *q=NULL;
 if(sqlite3_prepare_v2(d,sql,-1,&q,NULL)!=SQLITE_OK){sqlite3_finalize(q);return NULL;}
 va_list ap;va_start(ap,types);int ok=db_bindv(q,types,ap);va_end(ap);
 if(!ok){sqlite3_finalize(q);return NULL;}
 return q;
}

/* Run a statement that returns no rows. Returns rows changed, or -1 on error. */
static inline int db_run(sqlite3 *d,const char *sql,const char *types,...){
 sqlite3_stmt *q=NULL;
 if(sqlite3_prepare_v2(d,sql,-1,&q,NULL)!=SQLITE_OK){sqlite3_finalize(q);return -1;}
 va_list ap;va_start(ap,types);int ok=db_bindv(q,types,ap);va_end(ap);
 int rc=ok?sqlite3_step(q):SQLITE_ERROR;
 sqlite3_finalize(q);
 return rc==SQLITE_DONE?sqlite3_changes(d):-1;
}

/* First column of the first row as an integer; fallback if no row or error. */
static inline long long db_int(sqlite3 *d,long long fallback,const char *sql,const char *types,...){
 sqlite3_stmt *q=NULL;
 if(sqlite3_prepare_v2(d,sql,-1,&q,NULL)!=SQLITE_OK){sqlite3_finalize(q);return fallback;}
 va_list ap;va_start(ap,types);int ok=db_bindv(q,types,ap);va_end(ap);
 long long v=fallback;
 if(ok&&sqlite3_step(q)==SQLITE_ROW&&sqlite3_column_type(q,0)!=SQLITE_NULL)v=sqlite3_column_int64(q,0);
 sqlite3_finalize(q);return v;
}

/* Checked integer query: 1 value, 0 missing/NULL, -1 SQLite error. */
static inline int db_int_checked(sqlite3 *d,long long *out,const char *sql,const char *types,...){
 sqlite3_stmt *q=NULL;
 if(sqlite3_prepare_v2(d,sql,-1,&q,NULL)!=SQLITE_OK){sqlite3_finalize(q);return -1;}
 va_list ap;va_start(ap,types);int ok=db_bindv(q,types,ap);va_end(ap);
 int rc=ok?sqlite3_step(q):SQLITE_ERROR,result=-1;
 if(rc==SQLITE_ROW){if(sqlite3_column_type(q,0)==SQLITE_NULL)result=0;
  else{if(out)*out=sqlite3_column_int64(q,0);result=1;}}
 else if(rc==SQLITE_DONE)result=0;
 sqlite3_finalize(q);return result;
}

/* First column of the first row copied into out (empty if no row).
   Returns 1 row found, 0 none, -1 error. */
static inline int db_text(sqlite3 *d,char *out,size_t cap,const char *sql,const char *types,...){
 if(cap)out[0]=0;
 sqlite3_stmt *q=NULL;
 if(sqlite3_prepare_v2(d,sql,-1,&q,NULL)!=SQLITE_OK){sqlite3_finalize(q);return -1;}
 va_list ap;va_start(ap,types);int ok=db_bindv(q,types,ap);va_end(ap);
 int rc=ok?sqlite3_step(q):SQLITE_ERROR,r=-1;
 if(rc==SQLITE_ROW){if(cap)snprintf(out,cap,"%s",col_text(q,0));r=1;}
 else if(rc==SQLITE_DONE)r=0;
 sqlite3_finalize(q);return r;
}

/* ---------- Email queue ---------- */

/* The only way mail enters the queue. event_id deduplicates (NULL = always new);
   csv is an optional attachment. Returns 1 queued, 0 already queued, -1 refused/error. */
static inline int email_enqueue(sqlite3 *d,const char *to,const char *subject,const char *body,
                                const char *event_id,const char *csv){
 if(!email_address_ok(to)||!subject||!body)return -1;
 /* Only a keyed message may be silently skipped as a duplicate. */
 int n=db_run(d,event_id?"INSERT INTO email_queue(recipient,subject,body,status,next_attempt,event_id,csv_attachment) "
                         "VALUES(?1,?2,?3,'pending',strftime('%Y-%m-%dT%H:%M:%fZ','now'),?4,?5) ON CONFLICT(event_id) DO NOTHING"
                        :"INSERT INTO email_queue(recipient,subject,body,status,next_attempt,event_id,csv_attachment) "
                         "VALUES(?1,?2,?3,'pending',strftime('%Y-%m-%dT%H:%M:%fZ','now'),?4,?5)",
              "ttttt",to,subject,body,event_id,csv?csv:"");
 return n<0?-1:n>0;
}
#endif
