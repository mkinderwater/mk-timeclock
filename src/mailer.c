#define _POSIX_C_SOURCE 200809L
#include <sqlite3.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/wait.h>
#include <sys/stat.h>

static volatile sig_atomic_t stopping=0;
static void on_stop(int s){(void)s;stopping=1;}
struct payload {char *data;size_t length,offset;};
static size_t read_payload(char *p,size_t size,size_t nmemb,void *arg){
 struct payload *m=arg;size_t cap=size*nmemb,rem=m->length-m->offset;
 if(cap>rem)cap=rem;
 memcpy(p,m->data+m->offset,cap);m->offset+=cap;return cap;
}
static int safe_address(const char *v){
 if(!v||!strchr(v,'@')||strlen(v)>250)return 0;
 for(;*v;v++)if(*v=='\r'||*v=='\n'||(unsigned char)*v<32)return 0;
 return 1;
}
/* Subject header: CR/LF become spaces. Pure ASCII is sent as-is; anything else is
   RFC 2047 encoded (UTF-8, base64) in words of at most 42 input bytes, never splitting
   a UTF-8 sequence, and folded so each header line stays under 78 characters. */
static char *encode_subject(const char *s){
 size_t n=strlen(s);if(n>998)return NULL;
 char *clean=malloc(n+1);if(!clean)return NULL;
 int ascii=1;
 for(size_t i=0;i<n;i++){unsigned char ch=(unsigned char)s[i];clean[i]=(ch=='\r'||ch=='\n')?' ':(char)ch;if(ch>=127||(ch<32&&ch!='\r'&&ch!='\n'))ascii=0;}
 clean[n]=0;
 if(ascii)return clean;
 static const char b64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 size_t cap=n*2+(n/42+2)*20+16;char *out=malloc(cap);if(!out){free(clean);return NULL;}
 size_t o=0,i=0;
 while(i<n){
  size_t take=n-i<42?n-i:42;
  /* back off so the chunk does not end inside a multi-byte character */
  while(take>0&&i+take<n&&((unsigned char)clean[i+take]&0xC0)==0x80)take--;
  if(take==0)take=n-i<42?n-i:42;
  if(o)o+=(size_t)snprintf(out+o,cap-o,"\r\n ");
  o+=(size_t)snprintf(out+o,cap-o,"=?UTF-8?B?");
  for(size_t k=0;k<take;k+=3){
   unsigned v=(unsigned char)clean[i+k]<<16;
   if(k+1<take)v|=(unsigned char)clean[i+k+1]<<8;
   if(k+2<take)v|=(unsigned char)clean[i+k+2];
   out[o++]=b64[(v>>18)&63];out[o++]=b64[(v>>12)&63];
   out[o++]=k+1<take?b64[(v>>6)&63]:'=';out[o++]=k+2<take?b64[v&63]:'=';
  }
  o+=(size_t)snprintf(out+o,cap-o,"?=");
  i+=take;
 }
 out[o]=0;free(clean);return out;
}
/* A database-specific random identity, shared by all queue entries and retained on restore. */
static char mail_install_id[33];
static int load_install_id(sqlite3 *db){
 if(sqlite3_exec(db,"CREATE TABLE IF NOT EXISTS lite_install_identity(id INTEGER PRIMARY KEY CHECK(id=1),value TEXT NOT NULL CHECK(length(value)=32)); INSERT OR IGNORE INTO lite_install_identity(id,value) VALUES(1,lower(hex(randomblob(16))));",NULL,NULL,NULL)!=SQLITE_OK)return 0;
 sqlite3_stmt *q=NULL;int ok=0;
 if(sqlite3_prepare_v2(db,"SELECT value FROM lite_install_identity WHERE id=1",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW){
  const unsigned char *v=sqlite3_column_text(q,0);
  if(v&&strlen((const char*)v)==32){memcpy(mail_install_id,v,33);ok=1;}
 }
 sqlite3_finalize(q);return ok;
}
static void message_id(const char *from,const char *token,char *out,size_t cap){
 const char *at=strrchr(from,'@');const char *domain=at?at+1:"";
 /* The sender address has already passed safe_address. */
 if(!*domain||strlen(domain)>190){if(cap)out[0]=0;return;}
 snprintf(out,cap,"<mk-timeclock.%s@%s>",token,domain);
}
/* Encode every message part as MIME base64. This avoids relying on the SMTP
   server advertising 8BITMIME and keeps UTF-8 and CSV attachments identical
   across the single queue transport. Output wraps at 76 columns. */
static int append_base64(char *out,size_t cap,size_t *off,const char *src){
 static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 size_t len=strlen(src),col=0;
 for(size_t i=0;i<len;i+=3){
  unsigned v=(unsigned char)src[i]<<16;
  size_t rem=len-i;
  if(rem>1)v|=(unsigned char)src[i+1]<<8;
  if(rem>2)v|=(unsigned char)src[i+2];
  if(*off+7>=cap)return -1;
  out[(*off)++]=alphabet[(v>>18)&63];out[(*off)++]=alphabet[(v>>12)&63];
  out[(*off)++]=rem>1?alphabet[(v>>6)&63]:'=';
  out[(*off)++]=rem>2?alphabet[v&63]:'=';
  col+=4;
  if(col==76){out[(*off)++]='\r';out[(*off)++]='\n';col=0;}
 }
 if(col){if(*off+3>=cap)return -1;out[(*off)++]='\r';out[(*off)++]='\n';}
 out[*off]=0;return 0;
}
static int append_base64_bytes(char *out,size_t cap,size_t *off,const unsigned char *src,size_t len){
 static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
 size_t col=0;
 for(size_t i=0;i<len;i+=3){
  unsigned v=(unsigned char)src[i]<<16;
  size_t rem=len-i;
  if(rem>1)v|=(unsigned char)src[i+1]<<8;
  if(rem>2)v|=(unsigned char)src[i+2];
  if(*off+7>=cap)return -1;
  out[(*off)++]=alphabet[(v>>18)&63];out[(*off)++]=alphabet[(v>>12)&63];
  out[(*off)++]=rem>1?alphabet[(v>>6)&63]:'=';
  out[(*off)++]=rem>2?alphabet[v&63]:'=';
  col+=4;
  if(col==76){out[(*off)++]='\r';out[(*off)++]='\n';col=0;}
 }
 if(col){if(*off+3>=cap)return -1;out[(*off)++]='\r';out[(*off)++]='\n';}
 out[*off]=0;return 0;
}

/* Build the queued payroll workbook in a temporary private file. The CSV remains
   the durable source in email_queue, so SMTP retries create the same contents. */
static int make_payroll_book(const char *csv,const char *begin,const char *end,unsigned char **bytes,size_t *size){
 *bytes=NULL;*size=0;
 char input[]="/tmp/mk-payroll-in-XXXXXX",output[]="/tmp/mk-payroll-out-XXXXXX";
 int fi=mkstemp(input),fo=-1;
 if(fi<0)return -1;
 fo=mkstemp(output);if(fo<0){close(fi);unlink(input);return -1;}
 fchmod(fi,0600);fchmod(fo,0600);close(fo);
 size_t len=strlen(csv),written=0;
 while(written<len){ssize_t n=write(fi,csv+written,len-written);if(n<=0){close(fi);unlink(input);unlink(output);return -1;}written+=(size_t)n;}
 close(fi);
 const char *script=access("/opt/icubedev-timeclock/bin/timeclock-payroll-xlsx.py",R_OK)==0?"/opt/icubedev-timeclock/bin/timeclock-payroll-xlsx.py":"timeclock-payroll-xlsx.py";
 pid_t pid=fork();
 if(pid==0){execlp("python3","python3",script,input,output,begin,end,(char*)NULL);_exit(127);}
 int status=0;int ok=pid>0&&waitpid(pid,&status,0)==pid&&WIFEXITED(status)&&WEXITSTATUS(status)==0;
 unlink(input);
 if(!ok){unlink(output);return -1;}
 FILE *f=fopen(output,"rb");if(!f){unlink(output);return -1;}
 if(fseek(f,0,SEEK_END)!=0){fclose(f);unlink(output);return -1;}
 long sz=ftell(f);if(sz<=0||sz>12000000||fseek(f,0,SEEK_SET)!=0){fclose(f);unlink(output);return -1;}
 unsigned char *buf=malloc((size_t)sz);if(!buf){fclose(f);unlink(output);return -1;}
 ok=fread(buf,1,(size_t)sz,f)==(size_t)sz;
 fclose(f);unlink(output);
 if(!ok){free(buf);return -1;}
 *bytes=buf;*size=(size_t)sz;return 0;
}
static char *build_message(const char *from,const char *from_name,const char *to,const char *subject,const char *body,const unsigned char *attachment,size_t attachment_len,const char *filename,const char *message_token){
 char *safe=encode_subject(subject);if(!safe)return NULL;
 char display[600]="";
 if(from_name&&*from_name){
  size_t n=strlen(from_name),o=0;int ascii=1;
  if(n>200){free(safe);return NULL;}
  for(size_t i=0;i<n;i++){unsigned char c=(unsigned char)from_name[i];if(c<32||c==127){free(safe);return NULL;}if(c>=128)ascii=0;}
  if(ascii){display[o++]='"';for(size_t i=0;i<n;i++){if(from_name[i]=='"'||from_name[i]=='\\')display[o++]='\\';display[o++]=from_name[i];}display[o++]='"';display[o++]=' ';display[o]=0;}
  else{char *encoded=encode_subject(from_name);if(!encoded){free(safe);return NULL;}snprintf(display,sizeof display,"%s ",encoded);free(encoded);}
 }
 static const char *days[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
 static const char *months[]={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
 time_t now=time(NULL);struct tm tm;localtime_r(&now,&tm);
 char zone[8];strftime(zone,sizeof zone,"%z",&tm);
 char date[64];snprintf(date,sizeof date,"%s, %02d %s %04d %02d:%02d:%02d %s",days[tm.tm_wday],tm.tm_mday,months[tm.tm_mon],tm.tm_year+1900,tm.tm_hour,tm.tm_min,tm.tm_sec,zone);
 char mid[320];message_id(from,message_token,mid,sizeof mid);
 int attach=attachment&&attachment_len>0;
 /* Base64 expansion is at most 4/3 plus line breaks; 2x leaves ample
    room for the envelope and all MIME boundaries, with overflow checks. */
 size_t bl=strlen(body),cl=attach?attachment_len:0,sl=strlen(safe),fl=strlen(from),tl=strlen(to);
 if(bl>(SIZE_MAX-4096)/2||cl>(SIZE_MAX-4096)/2||cl>(SIZE_MAX-4096)/2-bl||sl>SIZE_MAX-4096-(bl+cl)*2||fl>SIZE_MAX-4096-(bl+cl)*2-sl||tl>SIZE_MAX-4096-(bl+cl)*2-sl-fl){free(safe);return NULL;}
 size_t cap=(bl+cl)*2+sl+fl+tl+4096;
 char *out=malloc(cap);if(!out){free(safe);return NULL;}
 int n=snprintf(out,cap,"Date: %s\r\nMessage-ID: %s\r\nFrom: %s<%s>\r\nTo: <%s>\r\nSubject: %s\r\nMIME-Version: 1.0\r\nContent-Type: %s\r\n%s",date,mid,display,from,to,safe,attach?"multipart/mixed; boundary=\"mkpayrollcsv25\"":"text/plain; charset=UTF-8",attach?"\r\n--mkpayrollcsv25\r\nContent-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: base64\r\n\r\n":"Content-Transfer-Encoding: base64\r\n\r\n");
 free(safe);if(n<0||(size_t)n>=cap){free(out);return NULL;}
 size_t off=(size_t)n;
 if(append_base64(out,cap,&off,body)!=0){free(out);return NULL;}
 if(attach){
  const char *mime="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
  n=snprintf(out+off,cap-off,"\r\n--mkpayrollcsv25\r\nContent-Type: %s; name=\"%s\"\r\nContent-Disposition: attachment; filename=\"%s\"\r\nContent-Transfer-Encoding: base64\r\n\r\n",mime,filename,filename);
  if(n<0||(size_t)n>=cap-off){free(out);return NULL;}off+=(size_t)n;
  if(append_base64_bytes(out,cap,&off,attachment,attachment_len)!=0){free(out);return NULL;}
  n=snprintf(out+off,cap-off,"\r\n--mkpayrollcsv25--\r\n");
  if(n<0||(size_t)n>=cap-off){free(out);return NULL;}off+=(size_t)n;
 }
 out[off]=0;return out;
}
static int smtp_response_debug(CURL *handle,curl_infotype type,char *data,size_t size,void *ctx){
 (void)handle;
 if(type==CURLINFO_HEADER_IN&&size>=4){
  char *out=(char*)ctx;
  if(data[0]>='4'&&data[0]<='5'&&data[1]>='0'&&data[1]<='9'&&data[2]>='0'&&data[2]<='9'&&(data[3]==' '||data[3]=='-')){
   size_t n=size<239?size:239;
   while(n&&(data[n-1]=='\r'||data[n-1]=='\n'))n--;
   memcpy(out,data,n);out[n]=0;
  }
 }
 return 0;
}
static int smtp_send(const char *url,const char *user,const char *pass,const char *tls_mode,const char *from,const char *from_name,const char *to,const char *subject,const char *body,const unsigned char *attachment,size_t attachment_len,const char *filename,const char *message_token,char *error,size_t error_size){
 if(!safe_address(from)||!safe_address(to))return -1;
 char *msg=build_message(from,from_name,to,subject,body,attachment,attachment_len,filename,message_token);if(!msg)return -1;
 CURL *c=curl_easy_init();if(!c){free(msg);return -1;}
 struct payload p={msg,strlen(msg),0};struct curl_slist *recipients=NULL;char smtp_reply[240]="";
 curl_easy_setopt(c,CURLOPT_VERBOSE,1L);curl_easy_setopt(c,CURLOPT_DEBUGFUNCTION,smtp_response_debug);curl_easy_setopt(c,CURLOPT_DEBUGDATA,(void*)smtp_reply);
 char sender[300],recipient[300];snprintf(sender,sizeof sender,"<%s>",from);snprintf(recipient,sizeof recipient,"<%s>",to);
 recipients=curl_slist_append(NULL,recipient);
 curl_easy_setopt(c,CURLOPT_URL,url);
 /* No SMTP authentication options are set for an IP-allowlisted relay. */
 if(user&&*user){
  curl_easy_setopt(c,CURLOPT_USERNAME,user);
  curl_easy_setopt(c,CURLOPT_PASSWORD,pass);
 }
 curl_easy_setopt(c,CURLOPT_MAIL_FROM,sender);curl_easy_setopt(c,CURLOPT_MAIL_RCPT,recipients);
 curl_easy_setopt(c,CURLOPT_READFUNCTION,read_payload);curl_easy_setopt(c,CURLOPT_READDATA,&p);
 curl_easy_setopt(c,CURLOPT_UPLOAD,1L);curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,10L);curl_easy_setopt(c,CURLOPT_TIMEOUT,30L);
 long ssl_mode=(!strcmp(tls_mode,"none"))?CURLUSESSL_NONE:CURLUSESSL_ALL;
 curl_easy_setopt(c,CURLOPT_USE_SSL,ssl_mode);
 curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
 CURLcode rc=curl_easy_perform(c);long response=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&response);
 if(rc!=CURLE_OK)fprintf(stderr,"SMTP failed: %s (response %ld)\n",curl_easy_strerror(rc),response);
 if(error&&error_size)snprintf(error,error_size,"%s",smtp_reply[0]?smtp_reply:(rc==CURLE_OK?"SMTP server rejected message":curl_easy_strerror(rc)));
 curl_slist_free_all(recipients);curl_easy_cleanup(c);free(msg);
 /* Authentication/setup problems need intervention, not terminal queue failure. */
 if(rc==CURLE_LOGIN_DENIED||response==530||response==534||response==535||response==538)return 3;
 return rc==CURLE_OK&&response/100==2?0:(response>=500&&response<600?2:1);
}
static int smtp_config(sqlite3 *db,char *url,char *user,char *pass,char *tls,char *from,char *from_name){
 sqlite3_stmt*q=NULL;int good=0;
 if(sqlite3_prepare_v2(db,"SELECT url,username,password,tls_mode,sender,sender_name FROM smtp_settings WHERE id=1",-1,&q,NULL)==SQLITE_OK){
  if(sqlite3_step(q)==SQLITE_ROW){
   char *buf[]={url,user,pass,tls,from,from_name};size_t caps[]={260,260,260,20,260,260};
   for(int i=0;i<6;i++)snprintf(buf[i],caps[i],"%s",sqlite3_column_text(q,i)?(const char*)sqlite3_column_text(q,i):"");
   good=1;
  }
 }sqlite3_finalize(q);return good;
}
#include "email_render.h"
/* Alert payroll once after an email reaches terminal failure. Never alert on alert failure. */
static void queue_terminal_failure_notice(sqlite3 *db,int failed_id,const char *recipient,const char *failed_subject){
 sqlite3_stmt*q=NULL;char payroll[260]="",subject[350]="",body[1200]="",event[96];
 if(sqlite3_prepare_v2(db,"SELECT recipient FROM payroll_settings WHERE id=1",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW){
  const unsigned char*t=sqlite3_column_text(q,0);if(t)snprintf(payroll,sizeof payroll,"%s",(const char*)t);
 }sqlite3_finalize(q);q=NULL;
 if(!strchr(payroll,'@')||strpbrk(payroll,"\r\n"))return;
 if(sqlite3_prepare_v2(db,"SELECT subject,body FROM email_templates WHERE id='email_failure_payroll' AND enabled=1",-1,&q,NULL)!=SQLITE_OK)return;
 if(sqlite3_step(q)==SQLITE_ROW){
  const char *ts=(const char*)sqlite3_column_text(q,0),*tb=(const char*)sqlite3_column_text(q,1);
  const struct email_field vars[]={{"{{recipient}}",recipient},{"{{failed_subject}}",failed_subject}};
  char*rs=email_render(ts,vars,2),*rb=email_render(tb,vars,2);
  if(rs&&rb&&strlen(rs)<sizeof subject&&strlen(rb)<sizeof body){snprintf(subject,sizeof subject,"%s",rs);snprintf(body,sizeof body,"%s",rb);}
  free(rs);free(rb);
 }sqlite3_finalize(q);q=NULL;
 if(!subject[0]||!body[0])return;
 snprintf(event,sizeof event,"email_terminal_failure:%d",failed_id);
 if(sqlite3_prepare_v2(db,"INSERT OR IGNORE INTO email_queue(recipient,subject,body,status,next_attempt,event_id) VALUES(?1,?2,?3,'pending',strftime('%Y-%m-%dT%H:%M:%fZ','now'),?4)",-1,&q,NULL)==SQLITE_OK){
  sqlite3_bind_text(q,1,payroll,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,subject,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,body,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,4,event,-1,SQLITE_TRANSIENT);(void)sqlite3_step(q);
 }sqlite3_finalize(q);
}
static int process_one(sqlite3 *db){
 sqlite3_stmt *q=NULL;int id=0;char *recipient=NULL,*subject=NULL,*body=NULL,*csv=NULL;int attempts=0,alert_is_failure=0;char begin[11]="",end[11]="",token[33]="";
 const char *sql="SELECT q.id,q.recipient,q.subject,q.body,q.attempts,q.csv_attachment,coalesce(p.starts,''),coalesce(p.ends,''),coalesce(q.event_id,''),q.message_token FROM email_queue q LEFT JOIN pay_periods p ON (q.event_id='payroll-period:'||p.id OR q.event_id LIKE 'payroll-period:'||p.id||':revision:%') WHERE q.status='pending' AND (q.next_attempt IS NULL OR q.next_attempt<=strftime('%Y-%m-%dT%H:%M:%fZ','now')) ORDER BY q.id LIMIT 1";
 if(sqlite3_prepare_v2(db,sql,-1,&q,NULL)!=SQLITE_OK)return -1;
 if(sqlite3_step(q)==SQLITE_ROW){id=sqlite3_column_int(q,0);attempts=sqlite3_column_int(q,4);recipient=strdup((const char*)sqlite3_column_text(q,1));subject=strdup((const char*)sqlite3_column_text(q,2));body=strdup((const char*)sqlite3_column_text(q,3));const unsigned char *attachment=sqlite3_column_text(q,5);csv=attachment?strdup((const char*)attachment):NULL;alert_is_failure=sqlite3_column_text(q,8)&&!strncmp((const char*)sqlite3_column_text(q,8),"email_terminal_failure:",23);snprintf(begin,sizeof begin,"%s",sqlite3_column_text(q,6)?(const char*)sqlite3_column_text(q,6):"");snprintf(end,sizeof end,"%s",sqlite3_column_text(q,7)?(const char*)sqlite3_column_text(q,7):"");snprintf(token,sizeof token,"%s",sqlite3_column_text(q,9)?(const char*)sqlite3_column_text(q,9):"");}
 sqlite3_finalize(q);if(!id)return 0;
 if(!recipient||!subject||!body){free(recipient);free(subject);free(body);free(csv);return -1;}
 char url[260]="",user[260]="",pass[260]="",tls[20]="",from[260]="",from_name[260]="";
 int configured=smtp_config(db,url,user,pass,tls,from,from_name)&&url[0]&&safe_address(from);
 if(!configured){
  sqlite3_stmt *defer=NULL;
  if(sqlite3_prepare_v2(db,"UPDATE email_queue SET last_error='SMTP is not configured; configure SMTP settings',next_attempt=strftime('%Y-%m-%dT%H:%M:%fZ','now','+5 minutes') WHERE id=?1",-1,&defer,NULL)==SQLITE_OK){sqlite3_bind_int(defer,1,id);sqlite3_step(defer);}
  sqlite3_finalize(defer);
  fprintf(stderr,"email queue %d deferred: SMTP is not configured\n",id);
  free(recipient);free(subject);free(body);free(csv);return 1;
 }
 unsigned char *xlsx=NULL;size_t xlsx_len=0;
 char filename[100]="";int workbook=csv&&*csv;
 if(workbook){
  if(!begin[0]||!end[0]||make_payroll_book(csv,begin,end,&xlsx,&xlsx_len)!=0){
   fprintf(stderr,"Payroll XLSX creation failed; deferring message so other emails can proceed\n");
   sqlite3_stmt*retry=NULL;
   int updated=sqlite3_prepare_v2(db,"UPDATE email_queue SET attempts=attempts+1,status=CASE WHEN attempts>=9 THEN 'failed' ELSE 'pending' END,next_attempt=strftime('%Y-%m-%dT%H:%M:%fZ','now','+15 minutes'),last_error='Payroll workbook generation failed' WHERE id=?1",-1,&retry,NULL)==SQLITE_OK;
   if(updated){sqlite3_bind_int(retry,1,id);updated=sqlite3_step(retry)==SQLITE_DONE;}sqlite3_finalize(retry);
   if(updated&&attempts>=9&&!alert_is_failure)queue_terminal_failure_notice(db,id,recipient,subject);
   free(recipient);free(subject);free(body);free(csv);return updated?1:-1;
  }
  snprintf(filename,sizeof filename,"Payroll_%s_to_%s.xlsx",begin,end);
 }
 char smtp_error[256]="";
 int rc=smtp_send(url,user,pass,tls,from,from_name,recipient,subject,body,xlsx,xlsx_len,filename,token,smtp_error,sizeof smtp_error);
 free(xlsx);
 if(rc!=0&&(rc==2||(rc!=3&&attempts>=9))&&!alert_is_failure)queue_terminal_failure_notice(db,id,recipient,subject);
 free(recipient);free(subject);free(body);free(csv);
 if(sqlite3_prepare_v2(db,"UPDATE email_queue SET status=CASE WHEN ?2=0 THEN 'sent' WHEN ?2=2 OR (?2<>3 AND attempts>=9) THEN 'failed' ELSE 'pending' END,attempts=attempts+CASE WHEN ?2=3 THEN 0 ELSE 1 END,next_attempt=CASE WHEN ?2=0 OR ?2=2 THEN NULL WHEN ?2=3 THEN strftime('%Y-%m-%dT%H:%M:%fZ','now','+5 minutes') ELSE strftime('%Y-%m-%dT%H:%M:%fZ','now', '+' || min(3600,60*(1 << min(attempts,6))) || ' seconds') END,last_error=CASE WHEN ?2=0 THEN '' ELSE ?3 END WHERE id=?1",-1,&q,NULL)!=SQLITE_OK)return -1;
 sqlite3_bind_int(q,1,id);sqlite3_bind_int(q,2,rc);sqlite3_bind_text(q,3,smtp_error,-1,SQLITE_TRANSIENT);int ok=sqlite3_step(q)==SQLITE_DONE;sqlite3_finalize(q);return ok?1:-1;
}
#include "email_render.h"
#include "send_schedule.h"
#include "payroll_mail.inc"
int main(int argc,char **argv){
 if(argc!=2){fprintf(stderr,"usage: timeclock-mailer /path/to/server.db\n");return 2;}
 signal(SIGTERM,on_stop);signal(SIGINT,on_stop);
 sqlite3 *db=NULL;if(sqlite3_open(argv[1],&db)!=SQLITE_OK){fprintf(stderr,"SQLite open failed\n");return 1;}
 sqlite3_busy_timeout(db,5000);
 /* synchronous is connection-local; journal_mode=WAL is persistent. */
 char *pragma_err=NULL;
 if(sqlite3_exec(db,"PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;",NULL,NULL,&pragma_err)!=SQLITE_OK){
  fprintf(stderr,"mailer SQLite WAL/NORMAL setup failed: %s\n",pragma_err?pragma_err:"unknown");
  sqlite3_free(pragma_err);sqlite3_close(db);return 1;
 }
 if(sqlite3_exec(db,"UPDATE email_queue SET message_token=lower(hex(randomblob(16))) WHERE message_token='';",NULL,NULL,NULL)!=SQLITE_OK){sqlite3_close(db);return 1;}
 if(!load_install_id(db)){fprintf(stderr,"Cannot initialize persistent mail identity\n");sqlite3_close(db);return 1;}
 curl_global_init(CURL_GLOBAL_DEFAULT);
 while(!stopping){payroll_enqueue_due(db);int n=process_one(db);if(n<=0){if(n<0)fprintf(stderr,"email queue error\n");for(int i=0;i<20&&!stopping;i++)nanosleep(&(struct timespec){.tv_sec=0,.tv_nsec=100000000},NULL);}}
 curl_global_cleanup();sqlite3_close(db);return 0;
}
