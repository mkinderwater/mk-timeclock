/* Employee late-arrival explanations through a private emailed link.
   No password: the link carries a 256-bit random token that identifies one swipe. Only a
   SHA-256-crypt hash of the token is stored, so a database or backup copy cannot be used to
   rebuild links. The explanation can be added or changed for seven days or until the pay period
   closes, whichever occurs first. Later punches do not cut off the reply window. GET never changes anything, so mail scanners that prefetch links are harmless.
   Referrer-Policy is same-origin, not no-referrer: with no-referrer browsers send "Origin: null"
   on the form POST, which the same-origin check rightly rejects. */
#define NOTE_LINK_SALT "$5$rounds=1000$tcnotelink$"
#define NOTE_MAX 500        /* characters, enforced by the browser */
#define NOTE_MAX_BYTES 2000 /* server cap: 500 characters of any UTF-8 text */

static int note_link_schema(void){
 if(!sql("CREATE TABLE IF NOT EXISTS swipe_note_links(event_id TEXT PRIMARY KEY REFERENCES swipes(event_id),employee_id INTEGER NOT NULL REFERENCES employees(id),token_hash TEXT NOT NULL UNIQUE,created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
         ))return 0;
 return 1;
}
/* Hostname of the installed trusted certificate, read from the nginx site that timeclock-tls-apply.sh
   writes (mode 0644). The self-signed bootstrap site uses "server_name _" and yields nothing, so
   employees are never sent to a page with a certificate warning. Path overridable for tests. */
static void note_link_cert_host(char*out,size_t cap){
 out[0]=0;const char*path=getenv("TIMECLOCK_NGINX_TLS_CONF");if(!path||!*path)path="/etc/nginx/conf.d/timeclock-tls.conf";
 FILE*f=fopen(path,"r");if(!f)return;char line[400];
 while(fgets(line,sizeof line,f)){
  char*p=line;while(*p==' '||*p=='\t')p++;
  if(strncmp(p,"server_name",11)||(p[11]!=' '&&p[11]!='\t'))continue;
  p+=11;while(*p==' '||*p=='\t')p++;
  size_t n=strcspn(p," \t;\r\n");if(!n||n>=cap||n>249||p[n]!=';')break;
  int ok=isalnum((unsigned char)p[0]);for(size_t i=0;ok&&i<n;i++)ok=isalnum((unsigned char)p[i])||p[i]=='.'||p[i]=='-'||p[i]=='_';
  if(ok){memcpy(out,p,n);out[n]=0;}
  break;
 }
 fclose(f);
}
/* Use the HTTPS certificate hostname, falling back to the clock's local DNS name.
   Never infer an employee-facing URL from untrusted HTTP Host headers. */
static const char*note_link_base(char*out,size_t cap){
 char host[256]={0};note_link_cert_host(host,sizeof host);
 if(host[0] && strlen(host)+9<cap){snprintf(out,cap,"https://%s",host);return "certificate";}
 if(gethostname(host,sizeof host))host[0]=0;
 host[sizeof host-1]=0;
 size_t n=strlen(host);int valid=n>0&&n<250&&isalnum((unsigned char)host[0]);
 for(size_t i=0;valid&&i<n;i++)valid=isalnum((unsigned char)host[i])||host[i]=='.'||host[i]=='-';
 if(valid&&strlen(host)+9<cap){snprintf(out,cap,"https://%s",host);return "local_dns";}
 out[0]=0;return "none";
}
/* Called inside the swipe transaction for the employee email only. url is left empty when no
   link address is configured; the email is still sent. Returns 0 only on a database error. */
static int note_link_create(const char*event,int employee,char*url,size_t cap){
 url[0]=0;char base[270],token[65],hash[128];note_link_base(base,sizeof base);
 if(!base[0])return 1;
 if(!random_hex(token,32)||!token_hash(token,NOTE_LINK_SALT,hash,sizeof hash))return 0;
 sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(db,"INSERT INTO swipe_note_links(event_id,employee_id,token_hash) VALUES(?1,?2,?3) ON CONFLICT(event_id) DO UPDATE SET employee_id=excluded.employee_id,token_hash=excluded.token_hash,created_at=excluded.created_at",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,event,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,2,employee);sqlite3_bind_text(q,3,hash,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}
 sqlite3_finalize(q);if(ok&&strlen(base)+68<cap)snprintf(url,cap,"%.270s/n/%.64s",base,token);return ok;
}

struct note_view{char event[100],first[130],swiped_at[40],kind[8],note[NOTE_MAX_BYTES+16];int employee,late,early,open;};
/* Looks up the swipe for a token and decides whether notes are still open. 1 found, 0 unknown, -1 error. */
static int note_load(const char*token,struct note_view*v){
 memset(v,0,sizeof *v);char hash[128];if(!token_hash(token,NOTE_LINK_SALT,hash,sizeof hash))return -1;
 sqlite3_stmt*q=NULL;
 const char*find="SELECT l.event_id,l.employee_id,CASE WHEN e.first_name<>'' THEN e.first_name ELSE e.name END,s.swiped_at,coalesce(a.kind,''),coalesce(a.late_minutes,0),coalesce(n.note,''),e.active,CAST(strftime('%s',l.created_at) AS INTEGER)+604800>CAST(strftime('%s','now') AS INTEGER),coalesce(a.early_minutes,0) "
                 "FROM swipe_note_links l JOIN swipes s ON s.event_id=l.event_id AND s.employee_id=l.employee_id JOIN employees e ON e.id=l.employee_id "
                 "LEFT JOIN attendance_events a ON a.event_id=l.event_id LEFT JOIN swipe_notes n ON n.event_id=l.event_id WHERE l.token_hash=?1";
 if(sqlite3_prepare_v2(db,find,-1,&q,NULL)!=SQLITE_OK)return -1;
 sqlite3_bind_text(q,1,hash,-1,SQLITE_TRANSIENT);int found=0,active=0,not_expired=0;
 if(sqlite3_step(q)==SQLITE_ROW){found=1;
  snprintf(v->event,sizeof v->event,"%s",col_text(q,0));v->employee=sqlite3_column_int(q,1);snprintf(v->first,sizeof v->first,"%s",col_text(q,2));
  snprintf(v->swiped_at,sizeof v->swiped_at,"%s",col_text(q,3));snprintf(v->kind,sizeof v->kind,"%s",col_text(q,4));v->late=sqlite3_column_int(q,5);
  snprintf(v->note,sizeof v->note,"%s",col_text(q,6));active=sqlite3_column_int(q,7);not_expired=sqlite3_column_int(q,8);v->early=sqlite3_column_int(q,9);}
 sqlite3_finalize(q);if(!found)return 0;
 /* Never permit the token to edit a different kind of record. A later OUT is not
    a reason to reject an employee's explanation for their earlier late arrival. */
 v->open=active&&not_expired&&((!strcmp(v->kind,"IN")&&v->late>0)||(!strcmp(v->kind,"OUT")&&v->early>0));
 return 1;
}
static void note_local(const char*stamp,char*out,size_t cap){
 time_t t;struct tm lt;snprintf(out,cap,"%s",stamp);
 if(attendance_stamp(stamp,&t,&lt))strftime(out,cap,"%a %b %e, %Y at %l:%M %p %Z",&lt);
}
static void note_send(int fd,int code,sqlite3_str*o){
 char*body=sqlite3_str_finish(o);if(!body)return;size_t n=strlen(body);char h[600];
 int k=snprintf(h,sizeof h,"HTTP/1.1 %d %s\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %zu\r\nConnection: close\r\nCache-Control: no-store\r\n"
  "X-Content-Type-Options: nosniff\r\nReferrer-Policy: same-origin\r\nX-Robots-Tag: noindex, nofollow\r\nX-Frame-Options: DENY\r\n\r\n",code,reason_phrase(code),n);
 if(k>0&&(size_t)k<sizeof h&&send(fd,h,(size_t)k,0)==k)(void)send(fd,body,n,0);
 sqlite3_free(body);
}
static void note_page(int fd,int code,const char*token,const struct note_view*v,const char*message){
 sqlite3_str*o=sqlite3_str_new(db);
 sqlite3_str_appendall(o,UI_HEAD_LOGIN("Attendance explanation"));
 if(!v){
  sqlite3_str_appendall(o,"<h1>Link not valid</h1><p class='banner banner-problem' role='alert'><span class='flag flag-problem'>Problem</span><span>This link has expired or been replaced.</span></p><p>Contact your manager to add a reason.</p>" UI_FOOT);
  note_send(fd,code,o);return;
 }
 char when[80];note_local(v->swiped_at,when,sizeof when);
 sqlite3_str_appendall(o,v->early>0?"<h1>Early departure explanation</h1><p>Hello ":"<h1>Late arrival explanation</h1><p>Hello ");html_text(o,v->first);
 sqlite3_str_appendall(o,v->early>0?". You left before your scheduled finish.":". You arrived after your scheduled start.");
 sqlite3_str_appendall(o," Add a reason below if you have one. Your punch time does not change.</p><section><table class='swipe-detail'><tbody><tr><th>Recorded</th><td>");html_text(o,when);
 sqlite3_str_appendall(o,"</td></tr><tr><th>Direction</th><td>");html_text(o,v->kind[0]?v->kind:"Recorded");
 if(v->early>0)sqlite3_str_appendf(o,"</td></tr><tr><th>Early</th><td>%d minute%s",v->early,v->early==1?"":"s");
 if(v->late>0)sqlite3_str_appendf(o,"</td></tr><tr><th>Late</th><td>%d minute%s",v->late,v->late==1?"":"s");
 sqlite3_str_appendall(o,"</td></tr></tbody></table></section>");
 if(message&&*message){sqlite3_str_appendall(o,code>=400?"<p role='status' class='is-err'>":"<p role='status' class='is-ok'>");html_text(o,message);sqlite3_str_appendall(o,"</p>");}
 if(v->open){
  sqlite3_str_appendall(o,"<form method='post' action='/n/");sqlite3_str_appendall(o,token);
  sqlite3_str_appendf(o,"'><label>Reason<textarea name='note' required maxlength='%d' rows='5'>",NOTE_MAX);html_text(o,v->note);
  sqlite3_str_appendall(o,"</textarea></label><p class='muted'>Quick reasons:</p><div class='actions'><button type='button' onclick=\"this.form.elements.note.value='Traffic'\">Traffic</button> <button type='button' onclick=\"this.form.elements.note.value='Bus'\">Bus</button> <button type='button' onclick=\"this.form.elements.note.value='Appointment'\">Appointment</button> <button type='button' onclick=\"this.form.elements.note.focus()\">Other</button></div><div class='actions'><button>Save reason</button></div>"
   "<p class='muted'>The reason appears with this punch on your timesheet. You can change it for seven days, or until the pay period closes. Only your manager can correct a punch time.</p></form>");
 }else{
  sqlite3_str_appendall(o,"<section><h2>Reason</h2><p>");html_text(o,v->note[0]?v->note:"No reason was added.");sqlite3_str_appendall(o,"</p><p class='muted'>");
  sqlite3_str_appendall(o,"This link is closed. Contact your manager if a correction is needed.");
  sqlite3_str_appendall(o,"</p></section>");
 }
 sqlite3_str_appendall(o,UI_FOOT);note_send(fd,code,o);
}
/* GET /n/<token>[?saved=1] and POST /n/<token>. Public: no session, the token is the authority. */
static void note_route(int fd,const char*method,const char*path,char*buf){
 char token[65];const char*rest=path+3;size_t n=strcspn(rest,"?");
 if(n!=64){note_page(fd,404,"",NULL,NULL);return;}
 memcpy(token,rest,64);token[64]=0;
 if(!token_valid(token)){note_page(fd,404,"",NULL,NULL);return;}
 int saved=!strcmp(rest+64,"?saved=1");
 struct note_view v;int found=note_load(token,&v);
 if(found<0){send_response(fd,500,"text/plain","Database error");return;}
 if(!found){note_page(fd,404,"",NULL,NULL);return;}
 if(!strcmp(method,"GET")){note_page(fd,200,token,&v,saved?"Reason saved to your timesheet.":NULL);return;}
 if(strcmp(method,"POST")){send_response(fd,405,"text/plain","Method not allowed");return;}
 
 char*body=strstr(buf,"\r\n\r\n");char note[NOTE_MAX_BYTES+8]="";
 if(!body||!form_value_multiline(body+4,"note",note,sizeof note)){note_page(fd,400,token,&v,"The note could not be read. Try again.");return;}
 size_t len=strlen(note);while(len&&(note[len-1]==' '||note[len-1]=='\n'||note[len-1]=='\r'||note[len-1]=='\t'))note[--len]=0;
 if(!len){note_page(fd,400,token,&v,"Enter a reason before saving.");return;}
 if(len>NOTE_MAX_BYTES){note_page(fd,400,token,&v,"The reason is too long. Keep it under 500 characters.");return;}
 if(!sql("BEGIN IMMEDIATE")){send_response(fd,503,"text/plain","Busy, try again");return;}
 /* Re-check inside the write transaction so a swipe arriving at the same moment wins. */
 if(note_load(token,&v)!=1||!v.open){sql("ROLLBACK");note_page(fd,409,token,&v,"This response link is closed.");return;}
 char held[sizeof approval_card_uid];memcpy(held,approval_card_uid,sizeof held);approval_card_uid[0]=0;
 sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(db,"INSERT INTO swipe_notes(event_id,note,updated_by,updated_at) VALUES(?1,?2,?3,strftime('%Y-%m-%dT%H:%M:%fZ','now')) ON CONFLICT(event_id) DO UPDATE SET note=excluded.note,updated_by=excluded.updated_by,updated_at=excluded.updated_at",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,v.event,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,note,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,3,v.employee);ok=sqlite3_step(q)==SQLITE_DONE;}
 sqlite3_finalize(q);memcpy(approval_card_uid,held,sizeof held);
 if(!ok||!sql("COMMIT")){sql("ROLLBACK");send_response(fd,500,"text/plain","Save failed");return;}
 char location[96];snprintf(location,sizeof location,"/n/%s?saved=1",token);send_redirect(fd,location);
}
static void note_link_settings_get(int fd){
 char host[256],effective[270];note_link_cert_host(host,sizeof host);
 const char*source=note_link_base(effective,sizeof effective);sqlite3_str*o=sqlite3_str_new(db);
 sqlite3_str_appendall(o,"{\"certificate_host\":");json_text(o,host);
 sqlite3_str_appendall(o,",\"effective\":");json_text(o,effective);
 sqlite3_str_appendall(o,",\"source\":");json_text(o,source);
 sqlite3_str_appendall(o,"}");send_str(fd,"application/json",o);
}
