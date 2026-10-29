/* Daily attendance snapshots use effective punches and the existing durable email queue.
   No hours are inferred. A queued employee/day key is never sent a second time. */
static void daily_attendance_settings(int fd,const char*body){
 sqlite3_stmt*q=NULL;
 if(!body){
  if(sqlite3_prepare_v2(db,"SELECT enabled,send_time FROM daily_attendance_settings WHERE id=1",-1,&q,NULL)!=SQLITE_OK){reply_failed(fd);return;}
  sqlite3_str*out=sqlite3_str_new(db);
  if(sqlite3_step(q)!=SQLITE_ROW){sqlite3_finalize(q);sqlite3_free(sqlite3_str_finish(out));reply_failed(fd);return;}
  sqlite3_str_appendall(out,"{\"enabled\":");sqlite3_str_appendf(out,"%s",sqlite3_column_int(q,0)?"true":"false");sqlite3_str_appendall(out,",\"send_time\":");json_text(out,col_text(q,1));sqlite3_str_appendchar(out,1,'}');sqlite3_finalize(q);send_str(fd,"application/json",out);return;
 }
 char enabled[4]="",send_time[8]="";
 if(!form_value(body,"enabled",enabled,sizeof enabled)||(strcmp(enabled,"0")&&strcmp(enabled,"1"))||!form_value(body,"send_time",send_time,sizeof send_time)||!time_valid(send_time)){
  send_response(fd,400,"application/json","{\"error\":\"Choose a daily email option and a valid send time.\"}");return;
 }
 int ok=sqlite3_prepare_v2(db,"UPDATE daily_attendance_settings SET enabled=?1,send_time=?2 WHERE id=1",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,enabled[0]=='1');sqlite3_bind_text(q,2,send_time,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 if(ok)send_response(fd,200,"application/json","{\"status\":\"saved\"}");else reply_failed(fd);
}
static int daily_local_time(const char*stamp,const char*day,char out[64],time_t*at){
 struct tm local;if(!attendance_stamp(stamp,at,&local))return 0;
 char date[16];strftime(date,sizeof date,"%Y-%m-%d",&local);
 strftime(out,64,strcmp(date,day)?"%b %d %I:%M:%S %p":"%I:%M:%S %p",&local);return 1;
}
/* A NULL summary means no punches recorded on this local date. */
static int daily_attendance_summary(int employee,const char*day,time_t now,int cutoff_min,char**summary){
 *summary=NULL;
 sqlite3_str*out=sqlite3_str_new(db);sqlite3_stmt*q=NULL;
 const char*query="SELECT x.state,x.in_at,coalesce(x.out_at,''),x.seconds,coalesce(ci.late_minutes,ai.late_minutes,0),coalesce(co.early_minutes,ao.early_minutes,0),"
 REVIEW_ACTIVE_AT_SQL("x","'open'","?3") " "
 "FROM effective_shift_pairs x LEFT JOIN attendance_events ai ON ai.event_id=x.in_id LEFT JOIN attendance_events ao ON ao.event_id=x.out_id "
 "LEFT JOIN punch_corrections ci ON ci.event_id=x.in_id OR ci.manual_id=x.in_id LEFT JOIN punch_corrections co ON co.event_id=x.out_id OR co.manual_id=x.out_id "
 "WHERE x.employee_id=?1 AND date(x.in_at,'localtime')=?2 AND CAST(strftime('%s',x.in_at) AS INTEGER)<=?3 ORDER BY x.in_at,x.in_id";
 if(sqlite3_prepare_v2(db,query,-1,&q,NULL)!=SQLITE_OK){sqlite3_free(sqlite3_str_finish(out));return 0;}
 sqlite3_bind_int(q,1,employee);sqlite3_bind_text(q,2,day,-1,SQLITE_TRANSIENT);sqlite3_bind_int64(q,3,now);
 long long seconds=0;int rows=0,incomplete=0,eligible=0,rc;
 while((rc=sqlite3_step(q))==SQLITE_ROW){
  char in[64],end[64];time_t in_at,out_at=0;
  if(!daily_local_time(col_text(q,1),day,in,&in_at)){rc=SQLITE_ERROR;break;}
  const char*state=col_text(q,0);rows++;
  struct tm started;localtime_r(&in_at,&started);
  /* Suppress a snapshot consisting only of running shifts begun after send time.
     Completion or an earlier punch makes this day eligible on a later tick. */
  if(!sqlite3_column_int(q,6)||started.tm_hour*60+started.tm_min<=cutoff_min)eligible=1;
  if(!strcmp(state,"missing_in")){sqlite3_str_appendf(out,"IN missing - OUT %s\n",in);incomplete=1;}
  else if(strcmp(state,"complete")){sqlite3_str_appendf(out,sqlite3_column_int(q,6)?"IN %s - Still clocked in\n":"IN %s - OUT missing\n",in);incomplete=1;}
  else{
   if(!daily_local_time(col_text(q,2),day,end,&out_at)){rc=SQLITE_ERROR;break;}
   sqlite3_str_appendf(out,"IN %s - OUT %s\n",in,end);
   long long interval=sqlite3_column_int64(q,3);
   if(interval<0||out_at>now){sqlite3_str_appendall(out,"Check punch timestamps; interval not included.\n");incomplete=1;}
   else{seconds+=interval;if(interval>72000){sqlite3_str_appendall(out,"Long shift - check the recorded punches.\n");}}
  }
  int late=sqlite3_column_int(q,4),early=sqlite3_column_int(q,5);
  if(late){sqlite3_str_appendf(out,"Late arrival: %d min\n",late);}
  if(early){sqlite3_str_appendf(out,"Early departure: %d min\n",early);}
 }
 sqlite3_finalize(q);
 if(rc!=SQLITE_DONE){sqlite3_free(sqlite3_str_finish(out));return 0;}
 if(!rows||!eligible){sqlite3_free(sqlite3_str_finish(out));return 1;}
 if(incomplete)sqlite3_str_appendall(out,"Recorded hours: incomplete.\nCompleted intervals: ");else sqlite3_str_appendall(out,"Recorded hours: ");
 sqlite3_str_appendf(out,"%lldh %02lldm",seconds/3600,(seconds/60)%60);if(seconds%60)sqlite3_str_appendf(out," %02llds",seconds%60);
 char asof[64];struct tm local;localtime_r(&now,&local);strftime(asof,sizeof asof,"%Y-%m-%d %I:%M %p %Z",&local);
 sqlite3_str_appendf(out,"\nAs of %s.\n",asof);*summary=sqlite3_str_finish(out);return *summary!=NULL;
}
static int daily_attendance_queue(int employee,const char*name,const char*email,const char*day,const char*summary,const char*event,const char**error){
 *error="Database unavailable. Check storage and database access.";
 sqlite3_stmt*q=NULL;int period=0,manager=0;char starts[16]="",ends[16]="",token[65]="",base[270]="";
 q=db_prepare(db,"SELECT id,starts,ends FROM pay_periods WHERE ?1 BETWEEN starts AND ends ORDER BY starts DESC,id DESC LIMIT 1","t",day);
 if(!q)return 0;
 if(sqlite3_step(q)==SQLITE_ROW){period=sqlite3_column_int(q,0);snprintf(starts,sizeof starts,"%s",col_text(q,1));snprintf(ends,sizeof ends,"%s",col_text(q,2));}sqlite3_finalize(q);
 if(!period){*error="No pay period covers this date. Check the pay-period schedule.";return 0;}
 note_link_base(base,sizeof base);if(!base[0]){*error="No hostname is set for links. Configure HTTPS certificate.";return 0;}
 if(!sql("BEGIN IMMEDIATE"))return 0;
 int found=review_link_read(employee,period,token,NULL),ok=found>=0;
 if(ok&&found&&!token[0]){*error="Existing link cannot be emailed. Revoke it under Employee emails and links to issue a replacement.";ok=0;} /* Do not replace a valid old hash-only link. */
 if(ok&&!found){
  q=db_prepare(db,"SELECT e.id FROM employees e JOIN employee_roles r ON r.employee_id=e.id AND r.role='manager' WHERE e.active=1 ORDER BY e.id LIMIT 1","");
  if(q&&sqlite3_step(q)==SQLITE_ROW){manager=sqlite3_column_int(q,0);}
  sqlite3_finalize(q);
  if(!manager)*error="No active manager is available to issue links.";
  ok=manager&&review_link_store(employee,period,manager,token);
  if(ok)ok=db_run(db,"INSERT INTO review_link_audit(manager_id,employee_id,period_id,action) VALUES(?1,?2,?3,'daily email link issued')","iii",manager,employee,period)>=0;
 }
 if(ok){
  char link[390],subject[32768],message[32768];snprintf(link,sizeof link,"%s/review/%s",base,token);
  const struct email_field fields[]={{"{{employee_name}}",name},{"{{attendance_date}}",day},{"{{daily_summary}}",summary},{"{{period_start}}",starts},{"{{period_end}}",ends},{"{{review_link}}",link}};
  ok=template_render(db,"daily_attendance_employee",fields,6,subject,sizeof subject,message,sizeof message)==1;
  if(!ok)*error="Daily attendance email could not be rendered. Check Email templates.";
  if(ok){*error="Daily attendance email could not enter the queue. Check storage and database access.";ok=email_enqueue(db,email,subject,message,event,NULL)>=0;}
  if(ok)ok=email_preparation_clear(event);
 }
 if(ok){ok=sql("COMMIT");}
 if(!ok){sql("ROLLBACK");}return ok;
}
/* One snapshot of today's recorded punches, at or after the company time.
   Existing queue keys make repeated ticks and restarts idempotent. */
static void daily_attendance_due(time_t now){
 sqlite3_stmt*q=db_prepare(db,"SELECT enabled,send_time FROM daily_attendance_settings WHERE id=1","");
 if(!q)return;
 int enabled=0;char send_time[8]="";
 if(sqlite3_step(q)==SQLITE_ROW){enabled=sqlite3_column_int(q,0);snprintf(send_time,sizeof send_time,"%s",col_text(q,1));}sqlite3_finalize(q);
 struct tm local;if(!enabled||!time_valid(send_time)||!localtime_r(&now,&local)||local.tm_hour*60+local.tm_min<hhmm_minutes(send_time))return;
 int template=template_state(db,"daily_attendance_employee");
 if(template==-2){email_preparation_failure("daily-preparation",0,"Daily emails","Could not read the daily email template. Check the database.");return;}
 email_preparation_clear("daily-preparation");if(template!=1)return;
 char day[16];strftime(day,sizeof day,"%Y-%m-%d",&local);
 q=db_prepare(db,"SELECT e.id,e.name,e.email FROM employees e WHERE e.active=1 AND e.attendance_mode='punch_required' AND instr(coalesce(e.email,''),'@')>1 ORDER BY e.id","");
 if(!q){email_preparation_failure("daily-preparation",0,day,"Could not read employees for daily emails. Check the database.");return;}
 while(sqlite3_step(q)==SQLITE_ROW){
  int employee=sqlite3_column_int(q,0);const char*name=col_text(q,1),*email=col_text(q,2);if(!email_address_ok(email))continue;
  char event[80];snprintf(event,sizeof event,"daily-attendance:%s:%d",day,employee);
  long long exists=0;if(db_int_checked(db,&exists,"SELECT count(*) FROM email_queue WHERE event_id=?1","t",event)!=1){email_preparation_failure(event,employee,day,"Could not check the email queue. Check the database.");continue;}if(exists){email_preparation_clear(event);continue;}
  char*summary=NULL;
  if(!daily_attendance_summary(employee,day,now,hhmm_minutes(send_time),&summary)){email_preparation_failure(event,employee,day,"Daily attendance could not be calculated. Check the recorded punches and database.");continue;}
  const char*error=NULL;
  if(summary&&!daily_attendance_queue(employee,name,email,day,summary,event,&error))email_preparation_failure(event,employee,day,error);
  sqlite3_free(summary);
 }
 sqlite3_finalize(q);
}
