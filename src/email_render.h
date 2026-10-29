#ifndef TIMECLOCK_EMAIL_RENDER_H
#define TIMECLOCK_EMAIL_RENDER_H
/* Email templates: {{token}} substitution and the one way templates are read. */
#include <stdlib.h>
#include <string.h>
#include "common.h"

struct email_field {const char *key; const char *value;};

/* Replace every field key in source. Returns malloc'd text (max 32 KiB) or NULL. */
static char *email_render(const char *source,const struct email_field *fields,size_t count){
 if(!source)return NULL;
 size_t cap=32768,pos=0;char *out=malloc(cap);if(!out)return NULL;
 for(const char *p=source;*p;){
  size_t k;
  for(k=0;k<count;k++){
   size_t len=strlen(fields[k].key);
   if(len&&strncmp(p,fields[k].key,len)==0){
    const char *v=fields[k].value?fields[k].value:"";size_t n=strlen(v);
    if(n>=cap-pos){free(out);return NULL;}
    memcpy(out+pos,v,n);pos+=n;p+=len;break;
   }
  }
  if(k==count){if(pos+1>=cap){free(out);return NULL;}out[pos++]=*p++;}
 }
 out[pos]=0;return out;
}

/* 1 enabled, 0 disabled, -1 missing, -2 SQL failure. */
static inline int template_state(sqlite3 *d,const char *id){
 long long enabled=0;
 int rc=db_int_checked(d,&enabled,"SELECT enabled<>0 FROM email_templates WHERE id=?1","t",id);
 return rc==1?(enabled?1:0):rc==0?-1:-2;
}

/* Raw subject and body of an enabled template, malloc'd for the caller to free.
   Returns 1 loaded, 0 missing or disabled, -1 error. */
static inline int template_load(sqlite3 *d,const char *id,char **subject,char **body){
 *subject=*body=NULL;
 sqlite3_stmt *q=db_prepare(d,"SELECT subject,body FROM email_templates WHERE id=?1 AND enabled=1","t",id);
 if(!q)return -1;
 int rc=sqlite3_step(q),r=rc==SQLITE_DONE?0:-1;
 if(rc==SQLITE_ROW){
  *subject=strdup(col_text(q,0));*body=strdup(col_text(q,1));
  r=*subject&&*body?1:-1;
  if(r<0){free(*subject);free(*body);*subject=*body=NULL;}
 }
 sqlite3_finalize(q);return r;
}

/* Render an enabled template into the caller's buffers.
   1 rendered; 0 missing or disabled; -1 rendering failed or result too long.
   The buffers are changed only on 1, so callers keep their default text otherwise.
   Text is never truncated. */
static inline int template_render(sqlite3 *d,const char *id,const struct email_field *fields,size_t n,
                           char *subject,size_t subject_cap,char *message,size_t message_cap){
 char *ts=NULL,*tb=NULL;int r=template_load(d,id,&ts,&tb);
 if(r<=0)return r;
 char *rs=email_render(ts,fields,n),*rb=email_render(tb,fields,n);free(ts);free(tb);
 r=-1;
 if(rs&&rb&&strlen(rs)<subject_cap&&strlen(rb)<message_cap){
  snprintf(subject,subject_cap,"%s",rs);snprintf(message,message_cap,"%s",rb);r=1;
 }
 free(rs);free(rb);return r;
}
#endif
