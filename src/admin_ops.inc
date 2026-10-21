/* Manager-only operational visibility and local-date exports. No payroll calculations. */
static int op_date_ok(const char *v){return schedule_date_ok(v);}
static int op_query_param(const char *url,const char *key,char *out,size_t cap){
 const char *q=strchr(url,'?');if(!q)return 0;return form_value(q+1,key,out,cap);
}
static const char *op_text(sqlite3_stmt*q,int col){const unsigned char*v=sqlite3_column_text(q,col);return v?(const char*)v:"";}
static void op_csv(sqlite3_str*s,const char*v){
 sqlite3_str_appendchar(s,1,'"');if(v&&v[0]&&strchr("=+@-\t\r",v[0]))sqlite3_str_appendchar(s,1,'\'');for(const char*p=v?v:"";*p;p++){
  if(*p=='"')sqlite3_str_appendall(s,"\"\"");
  else if(*p=='\r'||*p=='\n')sqlite3_str_appendchar(s,1,' ');
  else sqlite3_str_appendchar(s,1,*p);
 }sqlite3_str_appendchar(s,1,'"');
}
static void attendance_export(int fd,int who,const char *path){
 char from[20]="",to[20]="",fmt[8]="",emp[22]="",scope[20]="";
 if(!op_query_param(path,"from",from,sizeof from)||!op_query_param(path,"to",to,sizeof to)||!op_query_param(path,"format",fmt,sizeof fmt)||!op_query_param(path,"employee",emp,sizeof emp)||!op_query_param(path,"scope",scope,sizeof scope)||!op_date_ok(from)||!op_date_ok(to)||strcmp(from,to)>0||(strcmp(fmt,"csv")&&strcmp(fmt,"json"))||(strcmp(scope,"all")&&strcmp(scope,"employee"))){send_response(fd,400,"application/json","{\"error\":\"Choose valid dates. The start date must not be after the end date.\"}");return;}
 char *end=NULL;long employee=0;
 if(!strcmp(scope,"employee")){employee=strtol(emp,&end,10);if(!*emp||*end||employee<=0||employee>2147483647){send_response(fd,400,"application/json","{\"error\":\"Select an employee.\"}");return;}}
 const char *query="SELECT e.id,e.name,s.event_id,s.swiped_at,datetime(s.swiped_at,'localtime'),date(s.swiped_at,'localtime'),CASE WHEN c.action='void' THEN 'VOID' ELSE coalesce(c.kind,a.kind,'') END,coalesce(a.late_minutes,0),coalesce(a.exception,''),coalesce(a.paired_in_event,''),coalesce(n.note,''),s.clock_id,s.card_uid,e.first_name,e.last_name,e.position FROM swipes s JOIN employees e ON e.id=s.employee_id LEFT JOIN attendance_events a ON a.event_id=s.event_id LEFT JOIN punch_corrections c ON c.event_id=s.event_id LEFT JOIN swipe_notes n ON n.event_id=s.event_id WHERE date(s.swiped_at,'localtime') BETWEEN ?1 AND ?2 AND (?3=0 OR e.id=?3) ORDER BY e.name,s.swiped_at,s.event_id LIMIT 100001";
 sqlite3_stmt*q=NULL;if(sqlite3_prepare_v2(db,query,-1,&q,NULL)!=SQLITE_OK){send_response(fd,500,"application/json","{\"error\":\"The clock could not complete this. Try again.\"}");return;}
 sqlite3_bind_text(q,1,from,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,to,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,3,(int)employee);
 sqlite3_str *o=sqlite3_str_new(db);int json=!strcmp(fmt,"json");
 if(json){sqlite3_str_appendall(o,"{\"timezone\":\"device local timezone\",\"from\":");str_json(o,from);sqlite3_str_appendall(o,",\"to\":");str_json(o,to);sqlite3_str_appendall(o,",\"records\":[");}
 else sqlite3_str_appendall(o,"employee_id,employee,event_id,utc_timestamp,local_timestamp,local_date,direction,late_minutes,exception,paired_in_event,note,clock_id,card_uid,first_name,last_name,position\r\n");
 int count=0,rc;while((rc=sqlite3_step(q))==SQLITE_ROW){
  if(count>=100000){rc=SQLITE_TOOBIG;break;}
  if(json){if(count)sqlite3_str_appendchar(o,1,',');sqlite3_str_appendchar(o,1,'{');}
  const char *keys[]={"employee_id","employee","event_id","utc_timestamp","local_timestamp","local_date","direction","late_minutes","exception","paired_in_event","note","clock_id","card_uid","first_name","last_name","position"};
  for(int i=0;i<16;i++){
   char num[32];const char*v=op_text(q,i);
   if(i==0||i==7){snprintf(num,sizeof num,"%d",sqlite3_column_int(q,i));v=num;}
   if(json){if(i)sqlite3_str_appendchar(o,1,',');str_json(o,keys[i]);sqlite3_str_appendchar(o,1,':');if(i==0||i==7)sqlite3_str_appendall(o,v);else str_json(o,v);}
   else{if(i)sqlite3_str_appendchar(o,1,',');op_csv(o,v);}
  }
  if(json)sqlite3_str_appendchar(o,1,'}');else sqlite3_str_appendall(o,"\r\n");count++;
  if(sqlite3_str_length(o)>6*1024*1024){rc=SQLITE_TOOBIG;break;}
 }
 sqlite3_finalize(q);
 if(rc!=SQLITE_DONE){sqlite3_str_finish(o);send_response(fd,500,"application/json","{\"error\":\"Too many records. Choose a shorter date range.\"}");return;}
 if(json){sqlite3_str_appendall(o,"],\"record_count\":");sqlite3_str_appendf(o,"%d}",count);}
 sqlite3_stmt*audit=NULL;if(sqlite3_prepare_v2(db,"INSERT INTO admin_export_audit(manager_id,scope,format,from_date,to_date) VALUES(?1,?2,?3,?4,?5)",-1,&audit,NULL)==SQLITE_OK){sqlite3_bind_int(audit,1,who);sqlite3_bind_text(audit,2,scope,-1,SQLITE_TRANSIENT);sqlite3_bind_text(audit,3,fmt,-1,SQLITE_TRANSIENT);sqlite3_bind_text(audit,4,from,-1,SQLITE_TRANSIENT);sqlite3_bind_text(audit,5,to,-1,SQLITE_TRANSIENT);sqlite3_step(audit);}sqlite3_finalize(audit);
 op_export_send(fd,json?"application/json; charset=utf-8":"text/csv; charset=utf-8",json?"attendance.json":"attendance.csv",o);
}
static void email_queue_json(int fd){
 sqlite3_stmt*q=NULL;const char*query="SELECT id,recipient,subject,status,attempts,coalesce(next_attempt,''),coalesce(event_id,''),coalesce(last_error,'') FROM email_queue ORDER BY id DESC LIMIT 200";
 if(sqlite3_prepare_v2(db,query,-1,&q,NULL)!=SQLITE_OK){send_response(fd,500,"application/json","{\"error\":\"The clock could not complete this. Try again.\"}");return;}
 sqlite3_str*o=sqlite3_str_new(db);sqlite3_str_appendall(o,"{\"messages\":[");int rows=0;
 while(sqlite3_step(q)==SQLITE_ROW){if(rows++)sqlite3_str_appendchar(o,1,',');sqlite3_str_appendall(o,"{\"id\":");sqlite3_str_appendf(o,"%d",sqlite3_column_int(q,0));
  const char*keys[]={"recipient","subject","status","next_attempt","event_id","last_error"};int cols[]={1,2,3,5,6,7};for(int i=0;i<6;i++){sqlite3_str_appendall(o,",");str_json(o,keys[i]);sqlite3_str_appendchar(o,1,':');str_json(o,op_text(q,cols[i]));}
  sqlite3_str_appendf(o,",\"attempts\":%d}",sqlite3_column_int(q,4));}
 sqlite3_finalize(q);sqlite3_str_appendall(o,"]}");send_str(fd,"application/json",o);
}
static void rejected_cards_json(int fd){
 sqlite3_stmt*q=NULL;const char*query="SELECT u.uid,u.clock_id,u.first_seen,u.last_seen,u.seen_count,coalesce(n.name,u.clock_id) FROM unknown_cards u LEFT JOIN clock_names n ON n.clock_id=u.clock_id ORDER BY last_seen DESC LIMIT 200";
 if(sqlite3_prepare_v2(db,query,-1,&q,NULL)!=SQLITE_OK){send_response(fd,500,"application/json","{\"error\":\"The clock could not complete this. Try again.\"}");return;}
 sqlite3_str*o=sqlite3_str_new(db);sqlite3_str_appendall(o,"{\"unknown_cards\":[");int rows=0;
 while(sqlite3_step(q)==SQLITE_ROW){if(rows++)sqlite3_str_appendchar(o,1,',');sqlite3_str_appendchar(o,1,'{');const char*keys[]={"uid","clock_id","first_seen","last_seen"};for(int i=0;i<4;i++){if(i)sqlite3_str_appendchar(o,1,',');str_json(o,keys[i]);sqlite3_str_appendchar(o,1,':');str_json(o,op_text(q,i));}sqlite3_str_appendf(o,",\"seen_count\":%d,\"clock_name\":",sqlite3_column_int(q,4));str_json(o,op_text(q,5));sqlite3_str_appendchar(o,1,'}');}
 sqlite3_finalize(q);sqlite3_str_appendall(o,"]}");send_str(fd,"application/json",o);
}
static void attendance_export_page(int fd){send_response(fd,200,"text/html; charset=utf-8",
 UI_HEAD("Attendance export","export")"<h1>Attendance export</h1><p>Card swipes with their correction status. Punches added by a manager are not included. Dates are local; each row also has the UTC time.</p><section><form action='/api/v1/admin/export' method='get'><label>Employees <select name='scope' id='scope'><option value='all'>All employees</option><option value='employee'>One employee</option></select></label><label id='emplabel' hidden>Employee <select name='employee' id='employee'></select></label><label>From <input type='date' name='from' required></label><label>To <input type='date' name='to' required></label><label>Format <select name='format'><option value='csv'>CSV</option><option value='json'>JSON</option></select></label><div class='actions'><button>Export</button></div></form></section>"
 "<script>const sc=document.getElementById('scope'),el=document.getElementById('employee'),lab=document.getElementById('emplabel');sc.onchange=()=>{lab.hidden=sc.value!=='employee'};tc.request('/api/v1/admin/employees').then(r=>r.json()).then(j=>{for(let e of j.employees){let o=document.createElement('option');o.value=e.id;o.textContent=[e.first_name,e.last_name].filter(Boolean).join(' ')||e.name;if(!e.active)o.textContent+=' (inactive)';el.append(o)}})</script>"UI_FOOT);}
static void email_queue_page(int fd){send_response(fd,200,"text/html; charset=utf-8",
 UI_HEAD("Email queue","queue")"<h1>Email queue</h1><p>Sent means the mail server accepted the message.</p><p id='qbanner' role='alert' hidden></p><section><p id='qstatus' role='status'></p><table class='mobile-queue'><thead><tr><th>ID</th><th>Recipient</th><th>Subject</th><th>Status</th><th>Attempts</th><th>Next attempt</th><th>Last error</th><th>Action</th></tr></thead><tbody id='rows'></tbody></table></section><script>async function refresh(){try{let r=await tc.request('/api/v1/admin/email-queue');let d=await tc.json(r);if(!r.ok)throw Error(d.error||'Queue unavailable');rows.replaceChildren();let failed=0;for(let m of d.messages){let tr=document.createElement('tr');let when=m.next_attempt&&m.status==='pending'?tc.datetime(m.next_attempt):'';let vals=[m.id,m.recipient,m.subject,'',m.attempts,when,m.last_error];for(let i=0;i<vals.length;i++){let td=document.createElement('td');td.dataset.label=['ID','Recipient','Subject','Status','Attempts','Next attempt','Last error'][i];td.textContent=vals[i]==null?'':vals[i];tr.append(td)}let lv={failed:['Failed','problem'],pending:['Waiting','check'],sent:['Sent','ok']}[m.status]||[m.status,'muted'];tr.children[3].append(tc.flag(lv[0],lv[1]));if(m.status==='failed'){tr.className='row-problem';failed++}if(m.status==='failed'){let td=document.createElement('td'),b=document.createElement('button');td.dataset.label='Action';b.textContent='Retry';b.onclick=async()=>{if(!confirm('Send this email to '+m.recipient+' again?'))return;try{let r=await tc.request('/api/v1/admin/email-queue/retry',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({id:m.id})});let j=await tc.json(r);if(!r.ok)throw Error(j.error);await refresh();tc.ok(document.getElementById('qstatus'),'Email queued again.')}catch(e){tc.err(document.getElementById('qstatus'),e)}};td.append(b);tr.append(td)}else{let td=document.createElement('td');td.dataset.label='Action';tr.append(td)}rows.append(tr)}tc.banner(document.getElementById('qbanner'),'problem','Problem',failed?failed+(failed===1?' email failed.':' emails failed.')+' Check the error, fix the cause, then select Retry.':'')}catch(e){tc.err(document.getElementById('qstatus'),e)}}refresh();setInterval(refresh,10000)</script>"UI_FOOT);}
static void rejected_cards_clear(int fd,int who){
 if(who<=0){send_response(fd,403,"application/json","{\"error\":\"Manager sign-in required\"}");return;}
 if(!sql("BEGIN IMMEDIATE")){send_response(fd,503,"application/json","{\"error\":\"The clock is busy. Try again.\"}");return;}
 int ok=sql("DELETE FROM unknown_cards");int removed=ok?sqlite3_changes(db):0;
 sqlite3_stmt*q=NULL;
 if(ok){ok=sqlite3_prepare_v2(db,"INSERT INTO employee_change_audit(manager_id,employee_id,change_body) VALUES(?1,NULL,?2)",-1,&q,NULL)==SQLITE_OK;
  if(ok){char detail[120];snprintf(detail,sizeof detail,"Cleared all unknown-card sightings: %d",removed);sqlite3_bind_int(q,1,who);sqlite3_bind_text(q,2,detail,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}}
 sqlite3_finalize(q);
 if(ok)ok=sql("COMMIT");
 if(!ok)sql("ROLLBACK");
 if(!ok){send_response(fd,500,"application/json","{\"error\":\"Nothing was cleared; please retry\"}");return;}
 char out[96];snprintf(out,sizeof out,"{\"cleared\":%d}",removed);send_response(fd,200,"application/json",out);
}
static void rejected_cards_page(int fd){send_response(fd,200,"text/html; charset=utf-8",
 UI_HEAD("Unknown cards","unknown")"<h1>Unknown cards</h1><p>Cards tapped that belong to no one. Assign them under <a href='/employees'>Employees</a>.</p><section><p id='ustatus' role='status'></p><div class='actions'><button type='button' id='clearUnknown' class='secondary'>Clear all unknown cards</button></div><table><thead><tr><th>Card</th><th>Clock</th><th>First tapped</th><th>Last tapped</th><th>Taps</th></tr></thead><tbody id='rows'></tbody></table></section><script>async function refresh(){try{let r=await tc.request('/api/v1/admin/rejected-cards');let d=await tc.json(r);rows.replaceChildren();let week=Date.now()-7*86400000,t=v=>{return tc.datetime(v)};for(let m of d.unknown_cards){let tr=document.createElement('tr');for(let v of [m.uid,(m.clock_name||m.clock_id),t(m.first_seen),t(m.last_seen),m.seen_count]){let td=document.createElement('td');td.textContent=v;tr.append(td)}if(new Date(m.last_seen).getTime()>=week){tr.className='row-check';tr.children[3].append(' ',tc.flag('This week','check'))}rows.append(tr)}document.getElementById('clearUnknown').disabled=!d.unknown_cards.length;if(!d.unknown_cards.length){let tr=document.createElement('tr'),td=document.createElement('td');td.colSpan=5;td.textContent='None.';tr.append(td);rows.append(tr)}}catch(e){tc.err(document.getElementById('ustatus'),e)}}document.getElementById('clearUnknown').onclick=async()=>{if(!confirm('Clear all unknown-card sightings? Registered cards and punches will not be changed.'))return;let b=document.getElementById('clearUnknown');b.disabled=true;try{let r=await tc.request('/api/v1/admin/rejected-cards/clear',{method:'POST'}),j=await tc.json(r);if(!r.ok)throw Error(j.error||'Could not clear unknown cards');await refresh();tc.ok(document.getElementById('ustatus'),j.cleared+' unknown card(s) cleared.')}catch(e){tc.err(document.getElementById('ustatus'),e);b.disabled=false}};refresh();setInterval(refresh,10000)</script>"UI_FOOT);}
static void rejected_uploads_page(int fd){send_response(fd,200,"text/html; charset=utf-8",
 UI_HEAD("Rejected uploads","uploads")"<h1>Rejected uploads</h1><p>Swipes a clock sent that the server refused. They are not counted as punches. Add a missed punch under <a href='/punch-corrections'>Punches and corrections</a> if one is needed.</p><section><p id='rstatus' role='status'></p><table class='mobile-uploads'><thead><tr><th>Swipe time</th><th>Clock</th><th>Card</th><th>Reason</th><th>Refused</th><th>Event</th></tr></thead><tbody id='rows'></tbody></table></section>"
 "<script>async function refresh(){try{let r=await tc.request('/api/v1/admin/rejected-uploads');let d=await tc.json(r);rows.replaceChildren();let week=Date.now()-7*86400000,t=v=>{return tc.datetime(v)};for(let m of d.rejected_uploads){let tr=document.createElement('tr');let vals=[t(m.swiped_at),m.clock_id,m.card_uid,m.reason,t(m.rejected_at),m.event_id];for(let i=0;i<vals.length;i++){let td=document.createElement('td');td.dataset.label=['Swipe time','Clock','Card','Reason','Refused','Event'][i];td.textContent=vals[i]==null?'':vals[i];tr.append(td)}if(new Date(m.rejected_at).getTime()>=week){tr.className='row-problem';tr.children[0].append(' ',tc.flag('This week','problem'))}rows.append(tr)}if(!d.rejected_uploads.length){let tr=document.createElement('tr'),td=document.createElement('td');td.colSpan=6;td.textContent='None.';tr.append(td);rows.append(tr)}}catch(e){tc.err(document.getElementById('rstatus'),e)}}refresh();setInterval(refresh,10000)</script>"UI_FOOT);}
/* Card status changes use the same manager-card approval transaction as employee saves. */
static int op_admin_change(int fd,const char *body,int who){
 (void)who;char op[32]="";if(!form_value(body,"op",op,sizeof op))return 0;
 if(strcmp(op,"card_status"))return 0;
 {
  char uid[48]="",kind[24]="",active[8]="";
  if(!form_value(body,"uid",uid,sizeof uid)||!form_value(body,"kind",kind,sizeof kind)||!form_value(body,"active",active,sizeof active)||(strcmp(kind,"employee")&&strcmp(kind,"manager"))||(strcmp(active,"0")&&strcmp(active,"1"))){send_response(fd,400,"application/json","{\"error\":\"invalid card state\"}");return 1;}
  int enabled=active[0]=='1';
  if(!enabled&&!strcmp(kind,"manager")){
   sqlite3_stmt*q=NULL;int count=0;
   if(sqlite3_prepare_v2(db,"SELECT count(*) FROM manager_cards mc JOIN employees e ON e.id=mc.manager_id JOIN user_accounts ua ON ua.employee_id=e.id JOIN employee_roles er ON er.employee_id=e.id AND er.role='manager' WHERE mc.active=1 AND e.active=1 AND ua.disabled=0",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW)count=sqlite3_column_int(q,0);
   sqlite3_finalize(q);if(count<=1){send_response(fd,409,"application/json","{\"error\":\"This is the last active manager card. Register another manager card first.\"}");return 1;}
  }
  sqlite3_stmt*q=NULL;const char *stmt=!strcmp(kind,"manager")?"UPDATE manager_cards SET active=?1 WHERE uid=?2 COLLATE NOCASE AND active<>?1":"UPDATE cards SET active=?1 WHERE uid=?2 COLLATE NOCASE AND active<>?1";
  int ok=sqlite3_prepare_v2(db,stmt,-1,&q,NULL)==SQLITE_OK;
  if(ok){sqlite3_bind_int(q,1,enabled);sqlite3_bind_text(q,2,uid,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE&&sqlite3_changes(db)==1;}sqlite3_finalize(q);
  if(ok&&enabled&&!strcmp(kind,"employee")){sqlite3_stmt *owner_q=NULL;long owner=0;
   if(sqlite3_prepare_v2(db,"SELECT employee_id FROM cards WHERE uid=?1 COLLATE NOCASE",-1,&owner_q,NULL)==SQLITE_OK){sqlite3_bind_text(owner_q,1,uid,-1,SQLITE_TRANSIENT);if(sqlite3_step(owner_q)==SQLITE_ROW)owner=sqlite3_column_int64(owner_q,0);}sqlite3_finalize(owner_q);
   if(owner)ok=welcome_enqueue(owner);
  }
  send_response(fd,ok?200:409,"application/json",ok?"{\"status\":\"saved\"}":"{\"error\":\"Card not found, or already in that state.\"}");return 1;
 }
}
/* Lost-card replacement. Keep original UID assigned but inactive for audit/history. */
static void card_replace(int fd,int who,const char *body){
 char old[32]="",fresh[32]="";
 if(who<=0||!form_value(body,"old_uid",old,sizeof old)||!form_value(body,"new_uid",fresh,sizeof fresh)){
  send_response(fd,400,"application/json","{\"error\":\"Enter a card UID of 8 to 20 hexadecimal characters.\"}");return;
 }
 size_t n=strlen(fresh);if(!strcmp(old,fresh)||strlen(old)<8||strlen(old)>20||n<8||n>20||n%2){send_response(fd,400,"application/json","{\"error\":\"Enter a card UID of 8 to 20 hexadecimal characters.\"}");return;}
 for(char *p=old;*p;p++){if(!isxdigit((unsigned char)*p)){send_response(fd,400,"application/json","{\"error\":\"Enter a card UID of 8 to 20 hexadecimal characters.\"}");return;}*p=(char)toupper((unsigned char)*p);}
 for(char *p=fresh;*p;p++){if(!isxdigit((unsigned char)*p)){send_response(fd,400,"application/json","{\"error\":\"Enter a card UID of 8 to 20 hexadecimal characters.\"}");return;}*p=(char)toupper((unsigned char)*p);}
 if(!strcmp(old,fresh)){send_response(fd,400,"application/json","{\"error\":\"The new card must be different from the old one.\"}");return;}
 if(!sql("BEGIN IMMEDIATE")){send_response(fd,503,"application/json","{\"error\":\"The clock is busy. Try again.\"}");return;}
 sqlite3_stmt *q=NULL;int owner=0,rc=SQLITE_ERROR,occupied=0,ok=1;
 if(sqlite3_prepare_v2(db,"SELECT employee_id FROM cards WHERE uid=?1 COLLATE NOCASE AND active=1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,old,-1,SQLITE_TRANSIENT);rc=sqlite3_step(q);if(rc==SQLITE_ROW)owner=sqlite3_column_int(q,0);}sqlite3_finalize(q);q=NULL;
 if(rc!=SQLITE_ROW||owner<=0)ok=0;
 if(ok){rc=SQLITE_ERROR;if(sqlite3_prepare_v2(db,"SELECT 1 FROM cards WHERE uid=?1 COLLATE NOCASE UNION ALL SELECT 1 FROM manager_cards WHERE uid=?1 COLLATE NOCASE LIMIT 1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,fresh,-1,SQLITE_TRANSIENT);rc=sqlite3_step(q);}sqlite3_finalize(q);q=NULL;occupied=rc==SQLITE_ROW;if(occupied||rc!=SQLITE_DONE)ok=0;}
 if(ok){ok=sqlite3_prepare_v2(db,"UPDATE cards SET active=0 WHERE uid=?1 COLLATE NOCASE AND employee_id=?2 AND active=1",-1,&q,NULL)==SQLITE_OK;if(ok){sqlite3_bind_text(q,1,old,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,2,owner);ok=sqlite3_step(q)==SQLITE_DONE&&sqlite3_changes(db)==1;}sqlite3_finalize(q);q=NULL;}
 if(ok){ok=sqlite3_prepare_v2(db,"INSERT INTO cards(uid,employee_id,active) VALUES(?1,?2,1)",-1,&q,NULL)==SQLITE_OK;if(ok){sqlite3_bind_text(q,1,fresh,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,2,owner);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);q=NULL;}
 if(ok){ok=sqlite3_prepare_v2(db,"INSERT INTO card_replacement_audit(manager_id,employee_id,old_uid,new_uid) VALUES(?1,?2,?3,?4)",-1,&q,NULL)==SQLITE_OK;if(ok){sqlite3_bind_int(q,1,who);sqlite3_bind_int(q,2,owner);sqlite3_bind_text(q,3,old,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,4,fresh,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);q=NULL;}
 if(ok){ok=sqlite3_prepare_v2(db,"DELETE FROM unknown_cards WHERE uid=?1 COLLATE NOCASE",-1,&q,NULL)==SQLITE_OK;if(ok){sqlite3_bind_text(q,1,fresh,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);q=NULL;}
 if(ok)ok=welcome_enqueue(owner);
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");
 send_response(fd,ok?200:409,"application/json",ok?"{\"status\":\"reassigned\"}":"{\"error\":\"That card is already assigned. Nothing was changed.\"}");
}
static void card_list_json(int fd){
 sqlite3_stmt*q=NULL;const char *sqltext="SELECT c.uid,CASE WHEN trim(e.last_name)<>'' THEN e.last_name||', '||e.first_name ELSE e.name END,c.active,'employee' FROM cards c JOIN employees e ON e.id=c.employee_id UNION ALL SELECT m.uid,CASE WHEN trim(e.last_name)<>'' THEN e.last_name||', '||e.first_name ELSE e.name END,m.active,'manager' FROM manager_cards m JOIN employees e ON e.id=m.manager_id ORDER BY 2 COLLATE NOCASE,1";
 if(sqlite3_prepare_v2(db,sqltext,-1,&q,NULL)!=SQLITE_OK){send_response(fd,500,"application/json","{\"error\":\"The clock could not complete this. Try again.\"}");return;}
 sqlite3_str*o=sqlite3_str_new(db);sqlite3_str_appendall(o,"{\"cards\":[");int count=0;
 while(sqlite3_step(q)==SQLITE_ROW){if(count++)sqlite3_str_appendchar(o,1,',');sqlite3_str_appendall(o,"{\"uid\":");str_json(o,op_text(q,0));sqlite3_str_appendall(o,",\"name\":");str_json(o,op_text(q,1));sqlite3_str_appendf(o,",\"active\":%s,\"kind\":",sqlite3_column_int(q,2)?"true":"false");str_json(o,op_text(q,3));sqlite3_str_appendchar(o,1,'}');}
 sqlite3_finalize(q);sqlite3_str_appendall(o,"]}");send_str(fd,"application/json",o);
}
static void card_manage_page(int fd){send_response(fd,200,"text/html; charset=utf-8",
 UI_HEAD("Card status","cards")"<h1>Card status</h1><p>Deactivate a card or replace a lost one. Past punches are kept. Deactivating or reactivating requires a manager card.</p><p id='status' role='status'></p><section><table><thead><tr><th>Person</th><th>Card</th><th>Type</th><th>Status</th><th>Action</th></tr></thead><tbody id='rows'></tbody></table></section><section id='replacement' hidden><h2>Replace lost card</h2><p id='target'></p><label>New card<input id='newUid' maxlength='20' autocomplete='off' placeholder='Scan or enter the card UID'></label><div class='actions'><button type='button' id='scanReplace'>Scan new card (30 seconds)</button><button type='button' id='saveReplace'>Replace card</button><button type='button' class='secondary' id='cancelReplace'>Cancel</button></div><p id='replaceStatus' role='status'></p></section>"
 "<script>let selected=null,scanTimer=null;const api=tc.api;async function refresh(){let j=await api('/api/v1/admin/card-status');rows.replaceChildren();for(let c of j.cards){let tr=document.createElement('tr');for(let v of [c.name,c.uid,c.kind==='manager'?'Manager':'Attendance',c.active?'Active':'']){let td=document.createElement('td');td.textContent=v;tr.append(td)}if(!c.active)tr.children[3].append(tc.flag('Inactive','muted'));let td=document.createElement('td'),b=document.createElement('button');b.className='secondary';b.textContent=c.active?'Deactivate':'Reactivate';b.onclick=()=>change(c);td.append(b);if(c.kind==='employee'&&c.active){let r=document.createElement('button');r.type='button';r.className='secondary';r.textContent='Replace';r.onclick=()=>{selected=c;replacement.hidden=false;target.textContent='Replacing '+c.uid+' for '+c.name+'. The old card stops working when you save.';newUid.value='';scanReplace.textContent='Scan new card (30 seconds)';replaceStatus.textContent='';newUid.focus()};td.append(' ',r)}tr.append(td);rows.append(tr)}}async function change(c){if(!confirm((c.active?'Deactivate ':'Reactivate ')+c.uid+'?'))return;document.getElementById('status').textContent=tc.CARD;try{let approval=await api('/api/v1/admin/employee-change/arm',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({op:'card_status',uid:c.uid,kind:c.kind,active:c.active?'0':'1'})});for(let n=0;n<43;n++){await new Promise(r=>setTimeout(r,700));let j=await api('/api/v1/admin/employee-change/status',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({token:approval.token})});if(j.status==='saved'){await refresh();tc.ok(document.getElementById('status'),'Card '+(c.active?'deactivated.':'reactivated.'));return}if(j.status==='expired')throw Error(tc.TIMEOUT)}throw Error(tc.TIMEOUT)}catch(e){tc.err(document.getElementById('status'),e)}}"
 "cancelReplace.onclick=()=>{replacement.hidden=true;selected=null;if(scanTimer)clearInterval(scanTimer)};scanReplace.onclick=async()=>{if(!selected)return;scanReplace.disabled=true;scanReplace.textContent='Scan new card (30 seconds)';replaceStatus.textContent='Tap the new card on the clock.';try{await api('/api/v1/admin/manager-scan/arm',{method:'POST'});let attempts=0;scanTimer=setInterval(async()=>{try{let j=await api('/api/v1/admin/manager-scan/status');if(j.status==='scanned'&&j.uid){newUid.value=j.uid;scanReplace.textContent=j.uid;replaceStatus.textContent='Select Replace card to save.';clearInterval(scanTimer);scanTimer=null;scanReplace.disabled=false}else if(j.status==='expired'||++attempts>48){clearInterval(scanTimer);scanTimer=null;scanReplace.disabled=false;tc.err(replaceStatus,tc.TIMEOUT)}}catch(e){clearInterval(scanTimer);scanTimer=null;scanReplace.disabled=false;tc.err(replaceStatus,e)}},700)}catch(e){scanReplace.disabled=false;tc.err(replaceStatus,e)}};saveReplace.onclick=async()=>{if(!selected)return;let newCard=newUid.value.trim().toUpperCase();if(!/^(?:[A-F0-9]{2}){4,10}$/.test(newCard)){tc.err(replaceStatus,'Enter a card UID of 8 to 20 hexadecimal characters.');return}if(!confirm('Replace '+selected.uid+' with '+newCard+' for '+selected.name+'?'))return;saveReplace.disabled=true;try{await api('/api/v1/admin/card-replace',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({old_uid:selected.uid,new_uid:newCard})});tc.ok(replaceStatus,'Card replaced. The old card no longer works.');selected=null;await refresh()}catch(e){tc.err(replaceStatus,e)}finally{saveReplace.disabled=false}};refresh().catch(e=>tc.err(document.getElementById('status'),e))</script>"UI_FOOT);}
