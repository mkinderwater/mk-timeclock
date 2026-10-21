/* Basic schedule module. Deliberately no payroll or alert side effects. */
static int schedule_date_ok(const char *v){
 if(strlen(v)!=10||v[4]!='-'||v[7]!='-')return 0;
 for(int i=0;i<10;i++)if(i!=4&&i!=7&&!isdigit((unsigned char)v[i]))return 0;
 int y=atoi(v),m=atoi(v+5),d=atoi(v+8);
 if(y<2020||y>2100||m<1||m>12||d<1||d>31)return 0;
 struct tm t={0};t.tm_year=y-1900;t.tm_mon=m-1;t.tm_mday=d;t.tm_isdst=-1;
 time_t e=mktime(&t);struct tm out;if(e==(time_t)-1||!localtime_r(&e,&out))return 0;
 return out.tm_year==y-1900&&out.tm_mon==m-1&&out.tm_mday==d;
}
static int schedule_time_ok(const char *v){return strlen(v)==5&&isdigit((unsigned char)v[0])&&isdigit((unsigned char)v[1])&&v[2]==':'&&isdigit((unsigned char)v[3])&&isdigit((unsigned char)v[4])&&atoi(v)<24&&atoi(v+3)<60;}
static int schedule_employee_ok(int id){sqlite3_stmt*q=NULL;int ok=0;if(sqlite3_prepare_v2(db,"SELECT 1 FROM employees WHERE id=?1 AND active=1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,id);ok=sqlite3_step(q)==SQLITE_ROW;}sqlite3_finalize(q);return ok;}
static void schedule_json_text(sqlite3_str *out,const char *v){
 sqlite3_str_appendchar(out,1,'"');if(v)for(const unsigned char *p=(const unsigned char*)v;*p;p++){
  if(*p=='"'||*p=='\\'){sqlite3_str_appendchar(out,1,'\\');sqlite3_str_appendchar(out,1,(char)*p);}
  else if(*p<32)sqlite3_str_appendf(out,"\\u%04x",(unsigned)*p);
  else sqlite3_str_appendchar(out,1,(char)*p);
 }sqlite3_str_appendchar(out,1,'"');
}
static void schedules_json(int fd){
 sqlite3_str*o=sqlite3_str_new(db);sqlite3_str_appendall(o,"{\"employees\":[");sqlite3_stmt*q=NULL;int first=1;
 if(sqlite3_prepare_v2(db,"SELECT e.id,e.name,COALESCE(p.schedule_mode,'unconfigured'),COALESCE(p.start_time,''),COALESCE(p.end_time,''),COALESCE(p.weekday_mask,31),EXISTS(SELECT 1 FROM cards c WHERE c.employee_id=e.id AND c.active=1),CASE WHEN p.schedule_mode='none' THEN 1 ELSE 0 END,e.attendance_mode FROM employees e LEFT JOIN schedule_profiles p ON p.employee_id=e.id WHERE e.active=1 ORDER BY e.name",-1,&q,NULL)==SQLITE_OK){
 while(sqlite3_step(q)==SQLITE_ROW){const char*name=(const char*)sqlite3_column_text(q,1);const char*pay=(const char*)sqlite3_column_text(q,2);const char*st=(const char*)sqlite3_column_text(q,3);const char*en=(const char*)sqlite3_column_text(q,4);sqlite3_str_appendf(o,"%s{\"id\":%d,\"name\":",first?"":",",sqlite3_column_int(q,0));schedule_json_text(o,name);
 sqlite3_str_appendall(o,",\"schedule_mode\":");schedule_json_text(o,pay);sqlite3_str_appendall(o,",\"start\":");schedule_json_text(o,st);sqlite3_str_appendall(o,",\"end\":");schedule_json_text(o,en);sqlite3_str_appendf(o,",\"mask\":%d,\"has_card\":%s",sqlite3_column_int(q,5),sqlite3_column_int(q,6)?"true":"false");sqlite3_str_appendf(o,",\"schedule_exempt\":%s",sqlite3_column_int(q,7)?"true":"false");sqlite3_str_appendall(o,",\"attendance_mode\":");schedule_json_text(o,(const char*)sqlite3_column_text(q,8));sqlite3_str_appendchar(o,1,'}');first=0;}}
 sqlite3_finalize(q);sqlite3_str_appendall(o,"],\"shifts\":[");first=1;
 if(sqlite3_prepare_v2(db,"SELECT s.id,s.employee_id,s.work_date,s.start_time,s.end_time FROM schedule_shifts s JOIN employees e ON e.id=s.employee_id WHERE e.active=1 ORDER BY s.work_date,s.start_time,s.id LIMIT 500",-1,&q,NULL)==SQLITE_OK){while(sqlite3_step(q)==SQLITE_ROW){sqlite3_str_appendf(o,"%s{\"id\":%d,\"employee_id\":%d,\"date\":",first?"":",",sqlite3_column_int(q,0),sqlite3_column_int(q,1));schedule_json_text(o,(const char*)sqlite3_column_text(q,2));sqlite3_str_appendall(o,",\"start\":");schedule_json_text(o,(const char*)sqlite3_column_text(q,3));sqlite3_str_appendall(o,",\"end\":");schedule_json_text(o,(const char*)sqlite3_column_text(q,4));sqlite3_str_appendall(o,"}");first=0;}}sqlite3_finalize(q);
 sqlite3_str_appendall(o,"]}");send_str(fd,"application/json",o);
}
static int schedule_apply(const char *body){
 char op[24]="",idbuf[24]="",start[16]="",end[16]="",date[24]="",maskbuf[16]="",type[32]="",shiftid[24]="";
 if(!form_value(body,"op",op,sizeof op)||!form_value(body,"employee_id",idbuf,sizeof idbuf))return 0;
 int id=atoi(idbuf);if(id<1||!schedule_employee_ok(id))return 0;
 sqlite3_stmt*q=NULL;int ok=0;
 if(!strcmp(op,"profile")){
  if(!form_value(body,"type",type,sizeof type)||!form_value(body,"start",start,sizeof start)||!form_value(body,"end",end,sizeof end)||!form_value(body,"mask",maskbuf,sizeof maskbuf))return 0;
  int mask=atoi(maskbuf);if((strcmp(type,"recurring_weekly")&&strcmp(type,"dated_shifts")&&strcmp(type,"none"))||!schedule_time_ok(start)||!schedule_time_ok(end)||!strcmp(start,end)||mask<0||mask>127)return 0;
  if(!strcmp(type,"recurring_weekly")&&mask==0)return 0;
  if(sqlite3_prepare_v2(db,"UPDATE employees SET schedule_exempt=?1 WHERE id=?2",-1,&q,NULL)!=SQLITE_OK)return 0;
  sqlite3_bind_int(q,1,!strcmp(type,"none"));sqlite3_bind_int(q,2,id);
  int updated=sqlite3_step(q)==SQLITE_DONE;sqlite3_finalize(q);q=NULL;if(!updated)return 0;
  if(sqlite3_prepare_v2(db,"INSERT INTO schedule_profiles(employee_id,pay_type,start_time,end_time,weekday_mask,schedule_mode) VALUES(?1,CASE WHEN ?2='recurring_weekly' THEN 'salary' ELSE 'hourly' END,?3,?4,?5,?2) ON CONFLICT(employee_id) DO UPDATE SET pay_type=excluded.pay_type,start_time=excluded.start_time,end_time=excluded.end_time,weekday_mask=excluded.weekday_mask,schedule_mode=excluded.schedule_mode",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,id);sqlite3_bind_text(q,2,type,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,start,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,4,end,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,5,mask);ok=sqlite3_step(q)==SQLITE_DONE;}
 }else if(!strcmp(op,"shift")){
  if(!form_value(body,"date",date,sizeof date)||!form_value(body,"start",start,sizeof start)||!form_value(body,"end",end,sizeof end)||!schedule_date_ok(date)||!schedule_time_ok(start)||!schedule_time_ok(end)||!strcmp(start,end))return 0;
  int hourly=0;if(sqlite3_prepare_v2(db,"SELECT 1 FROM schedule_profiles WHERE employee_id=?1 AND schedule_mode='dated_shifts'",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,id);hourly=sqlite3_step(q)==SQLITE_ROW;}sqlite3_finalize(q);q=NULL;if(!hourly)return 0;
  if(sqlite3_prepare_v2(db,"INSERT INTO schedule_shifts(employee_id,work_date,start_time,end_time) VALUES(?1,?2,?3,?4) ON CONFLICT(employee_id,work_date,start_time) DO UPDATE SET end_time=excluded.end_time",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,id);sqlite3_bind_text(q,2,date,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,start,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,4,end,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}
 }else if(!strcmp(op,"delete")){
  if(!form_value(body,"shift_id",shiftid,sizeof shiftid))return 0;
  int sid=atoi(shiftid);if(sid<1)return 0;
  if(sqlite3_prepare_v2(db,"DELETE FROM schedule_shifts WHERE id=?1 AND employee_id=?2",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,sid);sqlite3_bind_int(q,2,id);ok=sqlite3_step(q)==SQLITE_DONE&&sqlite3_changes(db)==1;}
 }
 sqlite3_finalize(q);return ok;
}
/* Validate before opening a transaction or requesting manager-card approval. */
static const char *schedule_input_error(const char *body){
 char op[24]="",type[32]="",start[16]="",end[16]="",maskbuf[16]="";
 if(!body||!form_value(body,"op",op,sizeof op))return "Schedule request is incomplete.";
 if(strcmp(op,"profile")&&strcmp(op,"shift"))return NULL;
 if(!strcmp(op,"profile")){
  if(!form_value(body,"type",type,sizeof type)||!type[0])return "Choose a schedule type.";
  if(strcmp(type,"recurring_weekly")&&strcmp(type,"dated_shifts")&&strcmp(type,"none"))return "Choose a schedule type.";
  if(!strcmp(type,"recurring_weekly")){
   if(!form_value(body,"mask",maskbuf,sizeof maskbuf))return "Choose at least one working day.";
   char *tail=NULL;long m=strtol(maskbuf,&tail,10);
   if(!maskbuf[0]||*tail||m<1||m>127)return "Choose at least one working day.";
  }
 }
 if(!form_value(body,"start",start,sizeof start)||!form_value(body,"end",end,sizeof end)||!start[0]||!end[0])return "Enter a start and end time.";
 if(!schedule_time_ok(start)||!schedule_time_ok(end))return "Enter a valid time.";
 if(!strcmp(start,end))return "Start and end times must be different.";
 return NULL;
}
static void schedule_send_validation_error(int fd,const char *error){
 char msg[160];snprintf(msg,sizeof msg,"{\"error\":\"%s\"}",error);send_response(fd,400,"application/json",msg);
}
/* Routine schedule edits are approved by the authenticated manager session.
   Every committed change has an immutable audit row in the same transaction. */
static void schedule_save_direct(int fd,int who,const char *body){
 if(who<=0){send_response(fd,401,"application/json","{\"error\":\"Sign in again.\"}");return;}
 if(!body||strlen(body)>700||!strstr(body,"employee_id=")){
  send_response(fd,400,"application/json","{\"error\":\"Schedule not saved. Check the days and times.\"}");return;
 }
 const char *validation=schedule_input_error(body);if(validation){schedule_send_validation_error(fd,validation);return;}
 if(!sql("BEGIN IMMEDIATE")){send_response(fd,503,"application/json","{\"error\":\"The clock is busy. Try again.\"}");return;}
 int ok=schedule_apply(body);
 if(ok){sqlite3_stmt*q=NULL;
  ok=sqlite3_prepare_v2(db,"INSERT INTO schedule_change_audit(manager_id,change_body) VALUES(?1,?2)",-1,&q,NULL)==SQLITE_OK;
  if(ok){sqlite3_bind_int(q,1,who);sqlite3_bind_text(q,2,body,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}
  sqlite3_finalize(q);
 }
 if(ok)ok=sql("COMMIT");
 if(!ok)sql("ROLLBACK");
 send_response(fd,ok?200:400,"application/json",ok?"{\"status\":\"saved\"}":"{\"error\":\"Schedule not saved. Check the days and times.\"}");
}
static void schedule_arm(int fd,int who,const char *body){
 if(!body||strlen(body)>700||!strstr(body,"employee_id=")){send_response(fd,400,"application/json","{\"error\":\"Schedule not saved. Check the days and times.\"}");return;}
 const char *validation=schedule_input_error(body);if(validation){schedule_send_validation_error(fd,validation);return;}
 if(!sql("BEGIN IMMEDIATE")){send_response(fd,503,"application/json","{\"error\":\"The clock is busy. Try again.\"}");return;}
 if(authorization_pending()){sql("ROLLBACK");send_response(fd,409,"application/json","{\"error\":\"The card reader is waiting for another approval. Try again in 30 seconds.\"}");return;}
 /* Validate and roll back the proposed change before consuming an RFID scan. */
 if(!sql("SAVEPOINT validate_schedule")){sql("ROLLBACK");send_response(fd,500,"application/json","{\"error\":\"The clock could not complete this. Try again.\"}");return;}
 int valid_schedule=schedule_apply(body);
 int validation_reset=sql("ROLLBACK TO validate_schedule")&&sql("RELEASE validate_schedule");
 if(!valid_schedule||!validation_reset){sql("ROLLBACK");send_response(fd,400,"application/json","{\"error\":\"Schedule not saved. Check the days and times.\"}");return;}
 sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(db,"INSERT INTO schedule_authorization(id,requested_by,change_body,armed_at) VALUES(1,?1,?2,CAST(strftime('%s','now') AS INTEGER)) ON CONFLICT(id) DO UPDATE SET requested_by=excluded.requested_by,change_body=excluded.change_body,armed_at=excluded.armed_at",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,who);sqlite3_bind_text(q,2,body,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 char token[33]="";
 if(ok)ok=sql("UPDATE manager_scan SET armed_at=CAST(strftime('%s','now') AS INTEGER),uid='',scanned_at=0,scan_mode='approval' WHERE id=1");
 if(ok)ok=approval_token_new(token);
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");char reply[160];snprintf(reply,sizeof reply,"{\"status\":\"awaiting manager card\",\"token\":\"%s\"}",token);send_response(fd,ok?200:500,"application/json",ok?reply:"{\"error\":\"The clock could not complete this. Try again.\"}");
}
static void schedule_status(int fd,int who){
 sqlite3_stmt*q=NULL;int owner=0;long long armed=0,scanned=0;char body[800]="",uid[40]="";
 if(sqlite3_prepare_v2(db,"SELECT a.requested_by,a.change_body,a.armed_at,s.uid,s.scanned_at FROM schedule_authorization a JOIN manager_scan s ON s.id=1 WHERE a.id=1",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW){owner=sqlite3_column_int(q,0);const char*b=(const char*)sqlite3_column_text(q,1),*u=(const char*)sqlite3_column_text(q,3);if(b)snprintf(body,sizeof body,"%s",b);if(u)snprintf(uid,sizeof uid,"%s",u);armed=sqlite3_column_int64(q,2);scanned=sqlite3_column_int64(q,4);}sqlite3_finalize(q);
 if(owner!=who||!armed){send_response(fd,409,"application/json","{\"error\":\"This approval is no longer valid. Start again.\"}");return;}
 if((long long)time(NULL)-armed>30){sql("DELETE FROM schedule_authorization WHERE id=1");send_response(fd,200,"application/json","{\"status\":\"expired\"}");return;}
 if(!uid[0]||scanned<armed){send_response(fd,200,"application/json","{\"status\":\"waiting\"}");return;}
 int authorized=0;const char*check="SELECT 1 FROM manager_cards mc JOIN employees e ON e.id=mc.manager_id JOIN employee_roles r ON r.employee_id=e.id AND r.role='manager' JOIN user_accounts ua ON ua.employee_id=e.id WHERE mc.uid=?1 COLLATE NOCASE AND mc.active=1 AND e.active=1 AND ua.disabled=0";
 if(sqlite3_prepare_v2(db,check,-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,uid,-1,SQLITE_TRANSIENT);authorized=sqlite3_step(q)==SQLITE_ROW;}sqlite3_finalize(q);
 if(!authorized){sql("UPDATE manager_scan SET uid='',scanned_at=0 WHERE id=1");send_response(fd,200,"application/json","{\"status\":\"waiting\",\"message\":\"registered manager card required\"}");return;}
 if(!sql("BEGIN IMMEDIATE")){send_response(fd,503,"application/json","{\"error\":\"The clock is busy. Try again.\"}");return;}
 int ok=schedule_apply(body);if(ok)ok=sql("DELETE FROM schedule_authorization WHERE id=1")&&sql("UPDATE manager_scan SET armed_at=0,uid='',scanned_at=0 WHERE id=1");
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");send_response(fd,ok?200:400,"application/json",ok?"{\"status\":\"saved\"}":"{\"error\":\"Schedule not saved. Check the days and times.\"}");
}
static void schedule_page(int fd){
 static const char page[]=
 UI_HEAD("Schedules","schedules")"<h1>Schedules</h1><p>Schedules decide late and missing-punch notices. Choose a recurring weekly schedule, dated shifts, or no fixed schedule.</p><p id='schedbanner' role='alert' hidden></p><section><label>Employee<select id='emp'></select></label><p id='empflag' role='status'></p><label>Schedule type<select id='type'><option value=''>Choose a schedule</option><option value='recurring_weekly'>Recurring weekly</option><option value='dated_shifts'>Dated shifts</option><option value='none'>No fixed schedule</option></select></label>"
 "<div id='weekly'><p class='muted'>Working days and times. If the end is earlier, the shift ends the following day.</p><div id='days'></div><label>Start <input id='start' type='time' value='09:00'></label><label>End <input id='end' type='time' value='17:00'></label></div><div class='actions'><button id='profile'>Save schedule</button></div></section>"
 "<section id='hourly'><h2>Dated shifts</h2><p class='muted'>Saved dated shifts are kept when you change schedule type. They become active again if you return to Dated shifts. Check upcoming dates before saving.</p><label>Date <input id='date' type='date'></label><label>Start <input id='shiftstart' type='time' value='09:00'></label><label>End <input id='shiftend' type='time' value='17:00'></label><div class='actions'><button id='add'>Add dated shift</button></div><table><thead><tr><th>Date</th><th>Time</th><th></th></tr></thead><tbody id='shifts'></tbody></table></section>"
 "<section><p id='status' role='status'></p><p class='muted'>Changes are logged. Punches already recorded are not changed.</p></section>"
 "<script>let all={employees:[],shifts:[]};const el=id=>document.getElementById(id);let busy=false;const dn=['Mon','Tue','Wed','Thu','Fri','Sat','Sun'];el('days').replaceChildren(...dn.map((n,i)=>{let l=document.createElement('label'),c=document.createElement('input');c.type='checkbox';c.dataset.bit=String(1<<i);l.append(c,document.createTextNode(n));return l}));"
 "const api=tc.api;async function load(){all=await api('/api/v1/admin/schedules');let selected=el('emp').value;el('emp').replaceChildren();let today=new Date().toLocaleDateString('en-CA'),none=[],noshift=[];for(let e of all.employees){let o=document.createElement('option'),f=issue(e,today);o.value=e.id;o.textContent=e.name+(f?' ('+f.toLowerCase()+')':'');el('emp').append(o);if(f==='No schedule')none.push(e.name);else if(f)noshift.push(e.name)}if(selected)el('emp').value=selected;let msg=[];if(none.length)msg.push('No schedule: '+none.join(', ')+'.');if(noshift.length)msg.push('No upcoming dated shifts: '+noshift.join(', ')+'.');tc.banner(el('schedbanner'),'check','Check',msg.length?msg.join(' ')+' Late and missing-punch notices are not sent for them.':'');render()}function issue(e,today){if(e.attendance_mode==='vacation_only'||e.schedule_mode==='none'||e.schedule_exempt)return '';if(!e.has_card)return '';if(e.schedule_mode==='unconfigured')return 'No schedule';if(e.schedule_mode==='dated_shifts'&&!all.shifts.some(x=>x.employee_id===e.id&&x.date>=today))return 'No upcoming shifts';return ''}"
 "function render(){let e=all.employees.find(x=>String(x.id)===el('emp').value);if(!e)return;let f=issue(e,new Date().toLocaleDateString('en-CA'));el('empflag').replaceChildren();if(e.schedule_mode==='none')el('empflag').append(tc.flag('No fixed schedule','muted'));if(f)el('empflag').append(tc.flag(f,'check'),f==='No schedule'?' Save a schedule for this employee.':' Add shifts below.');el('type').value=e.schedule_mode==='unconfigured'?'':e.schedule_mode;el('start').value=e.start||'09:00';el('end').value=e.end||'17:00';for(let x of el('days').querySelectorAll('input'))x.checked=!!(e.mask&Number(x.dataset.bit));el('hourly').hidden=el('type').value!=='dated_shifts';el('weekly').hidden=el('type').value!=='recurring_weekly';el('shifts').replaceChildren();for(let s of all.shifts.filter(x=>x.employee_id===e.id)){let row=document.createElement('tr'),d=document.createElement('td'),t=document.createElement('td'),a=document.createElement('td'),b=document.createElement('button');d.textContent=s.date;t.textContent=s.start+' - '+s.end;b.textContent='Delete';b.onclick=()=>save({op:'delete',employee_id:e.id,shift_id:s.id});a.append(b);row.append(d,t,a);el('shifts').append(row)}}"
 "el('emp').onchange=render;el('type').onchange=()=>{el('hourly').hidden=el('type').value!=='dated_shifts';el('weekly').hidden=el('type').value!=='recurring_weekly'};async function save(obj){if(busy)return;busy=true;el('status').textContent='Saving...';try{const j=await api('/api/v1/admin/schedules/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(obj)});if(j.status!=='saved')throw Error('Schedule not saved.');await load();tc.ok(el('status'),'Schedule saved.');}catch(e){tc.err(el('status'),e)}finally{busy=false}}"
 "el('profile').onclick=()=>{let mask=0;for(let c of el('days').querySelectorAll('input'))if(c.checked)mask|=Number(c.dataset.bit);(()=>{const t=el('type').value,s=el('start').value,e=el('end').value;if(!t)return tc.err(el('status'),Error('Choose a schedule type.'));if(t==='recurring_weekly'&&!mask)return tc.err(el('status'),Error('Choose at least one working day.'));if(!s||!e)return tc.err(el('status'),Error('Enter a start and end time.'));if(s===e)return tc.err(el('status'),Error('Start and end times must be different.'));save({op:'profile',employee_id:el('emp').value,type:t,start:s,end:e,mask})})()};el('add').onclick=()=>save({op:'shift',employee_id:el('emp').value,date:el('date').value,start:el('shiftstart').value,end:el('shiftend').value});load().catch(e=>tc.err(el('status'),e))</script>"UI_FOOT;
 send_response(fd,200,"text/html; charset=utf-8",page);
}
