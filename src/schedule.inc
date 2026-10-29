/* Basic schedule module. Deliberately no payroll or alert side effects. */
static int schedule_employee_ok(int id){sqlite3_stmt*q=NULL;int ok=0;if(sqlite3_prepare_v2(db,"SELECT 1 FROM employees WHERE id=?1 AND active=1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,id);ok=sqlite3_step(q)==SQLITE_ROW;}sqlite3_finalize(q);return ok;}
static void schedules_json(int fd){
 sqlite3_str*o=sqlite3_str_new(db);sqlite3_str_appendall(o,"{\"employees\":[");sqlite3_stmt*q=NULL;int first=1;
 if(sqlite3_prepare_v2(db,"SELECT e.id,e.name,COALESCE(p.schedule_mode,'unconfigured'),COALESCE(p.start_time,''),COALESCE(p.end_time,''),COALESCE(p.weekday_mask,31),EXISTS(SELECT 1 FROM cards c WHERE c.employee_id=e.id AND c.active=1),CASE WHEN p.schedule_mode='none' THEN 1 ELSE 0 END,e.attendance_mode FROM employees e LEFT JOIN schedule_profiles p ON p.employee_id=e.id WHERE e.active=1 ORDER BY e.name",-1,&q,NULL)==SQLITE_OK){
 while(sqlite3_step(q)==SQLITE_ROW){const char*name=(const char*)sqlite3_column_text(q,1);const char*pay=(const char*)sqlite3_column_text(q,2);const char*st=(const char*)sqlite3_column_text(q,3);const char*en=(const char*)sqlite3_column_text(q,4);sqlite3_str_appendf(o,"%s{\"id\":%d,\"name\":",first?"":",",sqlite3_column_int(q,0));json_text(o,name);
 sqlite3_str_appendall(o,",\"schedule_mode\":");json_text(o,pay);sqlite3_str_appendall(o,",\"start\":");json_text(o,st);sqlite3_str_appendall(o,",\"end\":");json_text(o,en);sqlite3_str_appendf(o,",\"mask\":%d,\"has_card\":%s",sqlite3_column_int(q,5),sqlite3_column_int(q,6)?"true":"false");sqlite3_str_appendf(o,",\"schedule_exempt\":%s",sqlite3_column_int(q,7)?"true":"false");sqlite3_str_appendall(o,",\"attendance_mode\":");json_text(o,(const char*)sqlite3_column_text(q,8));sqlite3_str_appendchar(o,1,'}');first=0;}}
 sqlite3_finalize(q);sqlite3_str_appendall(o,"],\"shifts\":[");first=1;
 if(sqlite3_prepare_v2(db,"SELECT s.id,s.employee_id,s.work_date,s.start_time,s.end_time FROM schedule_shifts s JOIN employees e ON e.id=s.employee_id WHERE e.active=1 ORDER BY s.work_date,s.start_time,s.id LIMIT 500",-1,&q,NULL)==SQLITE_OK){while(sqlite3_step(q)==SQLITE_ROW){sqlite3_str_appendf(o,"%s{\"id\":%d,\"employee_id\":%d,\"date\":",first?"":",",sqlite3_column_int(q,0),sqlite3_column_int(q,1));json_text(o,(const char*)sqlite3_column_text(q,2));sqlite3_str_appendall(o,",\"start\":");json_text(o,(const char*)sqlite3_column_text(q,3));sqlite3_str_appendall(o,",\"end\":");json_text(o,(const char*)sqlite3_column_text(q,4));sqlite3_str_appendall(o,"}");first=0;}}sqlite3_finalize(q);
 sqlite3_str_appendall(o,"]}");send_str(fd,"application/json",o);
}
static int schedule_apply(const char *body){
 char op[24]="",idbuf[24]="",start[16]="",end[16]="",date[24]="",maskbuf[16]="",type[32]="",shiftid[24]="";
 if(!form_value(body,"op",op,sizeof op)||!form_value(body,"employee_id",idbuf,sizeof idbuf))return 0;
 int id=atoi(idbuf);if(id<1||!schedule_employee_ok(id))return 0;
 sqlite3_stmt*q=NULL;int ok=0;
 if(!strcmp(op,"profile")){
  if(!form_value(body,"type",type,sizeof type)||!form_value(body,"start",start,sizeof start)||!form_value(body,"end",end,sizeof end)||!form_value(body,"mask",maskbuf,sizeof maskbuf))return 0;
  int mask=atoi(maskbuf);if((strcmp(type,"recurring_weekly")&&strcmp(type,"dated_shifts")&&strcmp(type,"none"))||!time_valid(start)||!time_valid(end)||!strcmp(start,end)||mask<0||mask>127)return 0;
  if(!strcmp(type,"recurring_weekly")&&mask==0)return 0;
  if(sqlite3_prepare_v2(db,"UPDATE employees SET schedule_exempt=?1 WHERE id=?2",-1,&q,NULL)!=SQLITE_OK)return 0;
  sqlite3_bind_int(q,1,!strcmp(type,"none"));sqlite3_bind_int(q,2,id);
  int updated=sqlite3_step(q)==SQLITE_DONE;sqlite3_finalize(q);q=NULL;if(!updated)return 0;
  if(sqlite3_prepare_v2(db,"INSERT INTO schedule_profiles(employee_id,pay_type,start_time,end_time,weekday_mask,schedule_mode) VALUES(?1,CASE WHEN ?2='recurring_weekly' THEN 'salary' ELSE 'hourly' END,?3,?4,?5,?2) ON CONFLICT(employee_id) DO UPDATE SET pay_type=excluded.pay_type,start_time=excluded.start_time,end_time=excluded.end_time,weekday_mask=excluded.weekday_mask,schedule_mode=excluded.schedule_mode",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,id);sqlite3_bind_text(q,2,type,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,start,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,4,end,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,5,mask);ok=sqlite3_step(q)==SQLITE_DONE;}
 }else if(!strcmp(op,"shift")){
  if(!form_value(body,"date",date,sizeof date)||!form_value(body,"start",start,sizeof start)||!form_value(body,"end",end,sizeof end)||!date_valid(date)||!time_valid(start)||!time_valid(end)||!strcmp(start,end))return 0;
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
 if(!time_valid(start)||!time_valid(end))return "Enter a valid time.";
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
 if(!begin_change(fd))return;
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
 if(!approval_begin(fd))return;
 /* Validate and roll back the proposed change before consuming an RFID scan. */
 if(!sql("SAVEPOINT validate_schedule")){sql("ROLLBACK");reply_failed(fd);return;}
 int valid_schedule=schedule_apply(body);
 int validation_reset=sql("ROLLBACK TO validate_schedule")&&sql("RELEASE validate_schedule");
 if(!valid_schedule||!validation_reset){sql("ROLLBACK");send_response(fd,400,"application/json","{\"error\":\"Schedule not saved. Check the days and times.\"}");return;}
 approval_finish(fd,approval_save("schedule_authorization",who,body));
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
 if(!begin_change(fd))return;
 int ok=schedule_apply(body);if(ok)ok=sql("DELETE FROM schedule_authorization WHERE id=1")&&sql("UPDATE manager_scan SET armed_at=0,uid='',scanned_at=0 WHERE id=1");
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");send_response(fd,ok?200:400,"application/json",ok?"{\"status\":\"saved\"}":"{\"error\":\"Schedule not saved. Check the days and times.\"}");
}
static void schedule_page(int fd){send_response(fd,200,"text/html; charset=utf-8",UI_HEAD("Bulk schedules","schedules")
 "<h1>Bulk schedules</h1><p>Switch between employees to maintain schedules. The same editor is available in each employee record.</p><section><label>Employee<select id='bulkEmployee'></select></label><p id='bulkStatus' role='status'></p></section>"
 EMPLOYEE_SCHEDULE_HTML EMPLOYEE_SCHEDULE_JS
 "<script>(async()=>{const select=document.getElementById('bulkEmployee'),status=document.getElementById('bulkStatus');try{const j=await tc.api('/api/v1/admin/schedules');for(const e of j.employees){const o=document.createElement('option');o.value=e.id;o.textContent=e.name;select.append(o)}const id=new URLSearchParams(location.search).get('employee_id');if([...select.options].some(o=>o.value===id))select.value=id;const show=()=>{const e=j.employees.find(e=>String(e.id)===select.value);status.textContent=e&&e.attendance_mode==='vacation_only'?'Vacation-only employees do not need an attendance schedule.':'';window.employeeSchedule.open(Number(select.value),e&&e.attendance_mode==='vacation_only')};select.onchange=show;show()}catch(e){tc.err(status,e)}})();</script>"UI_FOOT);}
