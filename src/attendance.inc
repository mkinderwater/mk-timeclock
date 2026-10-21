/* Attendance v0.9.25. Raw swipes remain authoritative; derived status is provisional.
   All writes run inside the containing swipe transaction; email enqueue is idempotent. */
static int attendance_stamp(const char *stamp,time_t *out,struct tm *local){
 int y,m,d,h,mi,se;
 if(sscanf(stamp,"%d-%d-%dT%d:%d:%dZ",&y,&m,&d,&h,&mi,&se)!=6)return 0;
 if(y<2020||y>2100||m<1||m>12||d<1||d>31||h>23||mi>59||se>60)return 0;
sqlite3_stmt *q=NULL;int ok=0;
 if(sqlite3_prepare_v2(db,"SELECT CAST(strftime('%s',?1) AS INTEGER)",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,stamp,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW&&sqlite3_column_type(q,0)!=SQLITE_NULL){*out=(time_t)sqlite3_column_int64(q,0);ok=1;}}sqlite3_finalize(q);
 return ok&&localtime_r(out,local)!=NULL;
}
static int attendance_email(const char *event,int employee,const char *kind,int late,const char *local_time){
 if(late<=0||strcmp(kind,"IN"))return 1;
 sqlite3_stmt *q=NULL;char name[256]="",employee_mail[256]="";int notify=0;
 if(sqlite3_prepare_v2(db,"SELECT name,COALESCE(email,''),late_email_enabled FROM employees WHERE id=?1",-1,&q,NULL)!=SQLITE_OK)return 0;
 sqlite3_bind_int(q,1,employee);
 if(sqlite3_step(q)==SQLITE_ROW){snprintf(name,sizeof name,"%s",sqlite3_column_text(q,0));snprintf(employee_mail,sizeof employee_mail,"%s",sqlite3_column_text(q,1));notify=sqlite3_column_int(q,2);}
 sqlite3_finalize(q);
 if(!notify)return 1; /* Preserve lateness, suppress both recipients. */
 {sqlite3_stmt*h=NULL;int skipped=0;
  if(sqlite3_prepare_v2(db,"SELECT 1 FROM holiday_settings hs JOIN statutory_holidays h ON h.day=substr(?1,1,10) WHERE hs.id=1 AND hs.skip_alerts=1",-1,&h,NULL)==SQLITE_OK){
   sqlite3_bind_text(h,1,local_time,-1,SQLITE_TRANSIENT);skipped=sqlite3_step(h)==SQLITE_ROW;
  }sqlite3_finalize(h);if(skipped)return 1;
 }

 char key[160];
 char latebuf[30],scheduled[64],date[16];snprintf(latebuf,sizeof latebuf,"%d",late);
 snprintf(date,sizeof date,"%.10s",local_time);
 /* Derive scheduled start from this shift's actual local arrival minus recorded late minutes. */
 struct tm arrival={0};int yy,mm,dd,hh,mi,ss;
 if(sscanf(local_time,"%d-%d-%d %d:%d:%d",&yy,&mm,&dd,&hh,&mi,&ss)!=6)return 0;
 arrival.tm_year=yy-1900;arrival.tm_mon=mm-1;arrival.tm_mday=dd;arrival.tm_hour=hh;arrival.tm_min=mi;arrival.tm_sec=ss;arrival.tm_isdst=-1;
 time_t planned=mktime(&arrival)-(time_t)late*60;struct tm st;
 if(!localtime_r(&planned,&st))return 0;
 strftime(scheduled,sizeof scheduled,"%Y-%m-%d %H:%M:%S",&st);
 char note_url[400]="";
 /* {{note_link}} renders only in the employee's own email: the link needs no password, so it is
    never copied into manager mail. */
 struct email_field fields[]={
  {"{{employee_name}}",name},{"{{scheduled_start}}",scheduled},{"{{actual_start}}",local_time},
  {"{{late_minutes}}",latebuf},{"{{attendance_date}}",date},{"{{note_link}}",""}
 };
 const char *query="INSERT OR IGNORE INTO email_queue(recipient,subject,body,status,next_attempt,event_id) VALUES(?1,?2,?3,'pending',strftime('%Y-%m-%dT%H:%M:%fZ','now'),?4)";
 int ok=1;
 for(int recipient_type=0;recipient_type<2;recipient_type++){
  const char *template_id=recipient_type?"late_arrival_manager":"late_arrival_employee";
  sqlite3_stmt *tpl=NULL;char *ts=NULL,*tb=NULL;
  if(sqlite3_prepare_v2(db,"SELECT subject,body FROM email_templates WHERE id=?1 AND enabled=1",-1,&tpl,NULL)!=SQLITE_OK)return 0;
  sqlite3_bind_text(tpl,1,template_id,-1,SQLITE_STATIC);
  if(sqlite3_step(tpl)==SQLITE_ROW){ts=strdup((const char*)sqlite3_column_text(tpl,0));tb=strdup((const char*)sqlite3_column_text(tpl,1));}
  sqlite3_finalize(tpl);
  if(!ts||!tb){free(ts);free(tb);continue;}
  fields[5].value="";
  if(recipient_type==0&&strchr(employee_mail,'@')&&(strstr(ts,"{{note_link}}")||strstr(tb,"{{note_link}}"))){
   if(!note_link_create(event,employee,note_url,sizeof note_url)){free(ts);free(tb);return 0;}
   fields[5].value=note_url[0]?note_url:"(contact your manager to add a reason)";
  }
  char *subject=email_render(ts,fields,sizeof fields/sizeof fields[0]);
  char *message=email_render(tb,fields,sizeof fields/sizeof fields[0]);free(ts);free(tb);
  if(!subject||!message){free(subject);free(message);return 0;}
  if(recipient_type==0){
   if(strchr(employee_mail,'@')){
    sqlite3_stmt *ins=NULL;
    if(sqlite3_prepare_v2(db,query,-1,&ins,NULL)!=SQLITE_OK){free(subject);free(message);return 0;}
    snprintf(key,sizeof key,"late:%s:employee",event);
    sqlite3_bind_text(ins,1,employee_mail,-1,SQLITE_TRANSIENT);sqlite3_bind_text(ins,2,subject,-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(ins,3,message,-1,SQLITE_TRANSIENT);sqlite3_bind_text(ins,4,key,-1,SQLITE_TRANSIENT);
    ok=sqlite3_step(ins)==SQLITE_DONE;sqlite3_finalize(ins);
   }
  }else{
   sqlite3_stmt *mgr=NULL,*ins=NULL;
   if(sqlite3_prepare_v2(db,"SELECT DISTINCT m.id,m.email FROM employees m JOIN employee_roles r ON r.employee_id=m.id AND r.role='manager' WHERE m.active=1 AND m.email LIKE '%@%'",-1,&mgr,NULL)!=SQLITE_OK){free(subject);free(message);return 0;}
   if(sqlite3_prepare_v2(db,query,-1,&ins,NULL)!=SQLITE_OK){sqlite3_finalize(mgr);free(subject);free(message);return 0;}
   while(ok&&sqlite3_step(mgr)==SQLITE_ROW){const char *mail=(const char*)sqlite3_column_text(mgr,1);int mgr_id=sqlite3_column_int(mgr,0);
    snprintf(key,sizeof key,"late:%s:manager:%d",event,mgr_id);sqlite3_reset(ins);sqlite3_clear_bindings(ins);
    sqlite3_bind_text(ins,1,mail,-1,SQLITE_TRANSIENT);sqlite3_bind_text(ins,2,subject,-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(ins,3,message,-1,SQLITE_TRANSIENT);sqlite3_bind_text(ins,4,key,-1,SQLITE_TRANSIENT);
    ok=sqlite3_step(ins)==SQLITE_DONE;
   }
   sqlite3_finalize(mgr);sqlite3_finalize(ins);
  }
  free(subject);free(message);if(!ok)return 0;
 }
 return ok;
}
static int attendance_process(const char *event,int employee,const char *stamp){
 time_t at;struct tm local;if(!attendance_stamp(stamp,&at,&local))return 0;
 sqlite3_stmt*q=NULL;char prior_event[90]="",prior_stamp[64]="",prior_kind[12]="";
 int duplicate_seconds=120;sqlite3_stmt*threshold=NULL;
 if(sqlite3_prepare_v2(db,"SELECT coalesce((SELECT d.duplicate_seconds FROM clock_duplicate_settings d JOIN swipes s ON s.clock_id=d.clock_id WHERE s.event_id=?1),120)",-1,&threshold,NULL)==SQLITE_OK){sqlite3_bind_text(threshold,1,event,-1,SQLITE_TRANSIENT);if(sqlite3_step(threshold)==SQLITE_ROW)duplicate_seconds=sqlite3_column_int(threshold,0);}sqlite3_finalize(threshold);

 /* Event-ordered pairing. An earlier unsynchronized swipe arriving late is an exception. */
 const char *prior="SELECT p.id,p.at,p.kind FROM effective_punches p LEFT JOIN attendance_events a ON a.event_id=p.id WHERE p.employee_id=?1 AND coalesce(a.exception,'') NOT IN ('duplicate_tap','out_of_order') ORDER BY p.at DESC,p.id DESC LIMIT 1";
 if(sqlite3_prepare_v2(db,prior,-1,&q,NULL)!=SQLITE_OK)return 0;
 sqlite3_bind_int(q,1,employee);
 if(sqlite3_step(q)==SQLITE_ROW){snprintf(prior_event,sizeof prior_event,"%s",sqlite3_column_text(q,0));snprintf(prior_stamp,sizeof prior_stamp,"%s",sqlite3_column_text(q,1));snprintf(prior_kind,sizeof prior_kind,"%s",sqlite3_column_text(q,2));}
 sqlite3_finalize(q);
 int exception=0,kind_in=1;const char *reason="";time_t old=0;struct tm ignore;
 if(prior_event[0]&&attendance_stamp(prior_stamp,&old,&ignore)){
  if(at==old){exception=1;reason="duplicate_tap";}
  else if(at<old){exception=1;reason="out_of_order";}
  else if(!strcmp(prior_kind,"IN")&&difftime(at,old)<duplicate_seconds){exception=1;reason="duplicate_tap";}
  else if(!strcmp(prior_kind,"IN")&&difftime(at,old)<=20*3600){kind_in=0;}
  else if(!strcmp(prior_kind,"IN")){
   /* Mark the old unmatched IN, not the new arrival. */
   sqlite3_stmt *fix=NULL;
   if(sqlite3_prepare_v2(db,"UPDATE attendance_events SET exception='missing_out' WHERE event_id=?1 AND exception=''",-1,&fix,NULL)!=SQLITE_OK)return 0;
   sqlite3_bind_text(fix,1,prior_event,-1,SQLITE_TRANSIENT);
   int fixed=sqlite3_step(fix)==SQLITE_DONE;sqlite3_finalize(fix);if(!fixed)return 0;
  }
 }
 /* Per-clock duplicate interval (default 10 seconds), regardless of direction. */
 if(!exception){
  sqlite3_stmt *dup=NULL;
  if(sqlite3_prepare_v2(db,"SELECT s.swiped_at FROM swipes s WHERE s.employee_id=?1 AND s.event_id<>?2 AND s.swiped_at<?3 ORDER BY s.swiped_at DESC LIMIT 1",-1,&dup,NULL)!=SQLITE_OK)return 0;
  sqlite3_bind_int(dup,1,employee);sqlite3_bind_text(dup,2,event,-1,SQLITE_TRANSIENT);sqlite3_bind_text(dup,3,stamp,-1,SQLITE_TRANSIENT);
  if(sqlite3_step(dup)==SQLITE_ROW){time_t previous=0;struct tm t;const char *ts=(const char*)sqlite3_column_text(dup,0);if(ts&&attendance_stamp(ts,&previous,&t)&&difftime(at,previous)<duplicate_seconds){exception=1;reason="duplicate_tap";kind_in=1;}}
  sqlite3_finalize(dup);
 }
 int late=0;char localday[16],clock[16],display[50];strftime(localday,sizeof localday,"%Y-%m-%d",&local);strftime(clock,sizeof clock,"%H:%M",&local);strftime(display,sizeof display,"%Y-%m-%d %H:%M:%S",&local);
 if(kind_in&&!exception){
  /* A genuine swipe is always recorded, but approved leave is not a late arrival. */
  sqlite3_stmt *leave_check=NULL;
  int on_leave=0;
  if(sqlite3_prepare_v2(db,"SELECT 1 FROM timeoff_requests WHERE employee_id=?1 AND status='approved' AND cancelled_at IS NULL AND ?2 BETWEEN starts AND ends LIMIT 1",-1,&leave_check,NULL)==SQLITE_OK){
   sqlite3_bind_int(leave_check,1,employee);sqlite3_bind_text(leave_check,2,localday,-1,SQLITE_TRANSIENT);
   on_leave=sqlite3_step(leave_check)==SQLITE_ROW;
  }
  sqlite3_finalize(leave_check);
  /* Full-day manager-recorded excused absence; actual punches stay intact. */
  if(!on_leave && sqlite3_prepare_v2(db,"SELECT 1 FROM attendance_absences WHERE employee_id=?1 AND day=?2 AND starts='' AND cancelled_at IS NULL AND reason<>'Unexcused absence' LIMIT 1",-1,&leave_check,NULL)==SQLITE_OK){sqlite3_bind_int(leave_check,1,employee);sqlite3_bind_text(leave_check,2,localday,-1,SQLITE_TRANSIENT);on_leave=sqlite3_step(leave_check)==SQLITE_ROW;}
  sqlite3_finalize(leave_check);
  if(!on_leave){
  /* The first IN on the local work date is the only eligible late arrival. */
  int already_in=0;
  if(sqlite3_prepare_v2(db,"SELECT 1 FROM attendance_events a JOIN swipes s ON s.event_id=a.event_id WHERE a.employee_id=?1 AND a.kind='IN' AND date(s.swiped_at,'localtime')=?2 AND s.swiped_at<?3 LIMIT 1",-1,&q,NULL)!=SQLITE_OK)return 0;
  sqlite3_bind_int(q,1,employee);sqlite3_bind_text(q,2,localday,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,stamp,-1,SQLITE_TRANSIENT);
  already_in=sqlite3_step(q)==SQLITE_ROW;sqlite3_finalize(q);q=NULL;
  if(!already_in){
  char scheduled[10]="";
  /* Dated shift takes precedence; weekly profile applies on selected weekdays. */
  if(sqlite3_prepare_v2(db,"SELECT start_time FROM schedule_shifts WHERE employee_id=?1 AND work_date=?2 AND EXISTS(SELECT 1 FROM schedule_profiles sp WHERE sp.employee_id=?1 AND sp.schedule_mode='dated_shifts') ORDER BY ABS((CAST(substr(start_time,1,2) AS INTEGER)*60+CAST(substr(start_time,4,2) AS INTEGER)) - ?3),start_time LIMIT 1",-1,&q,NULL)==SQLITE_OK){
   sqlite3_bind_int(q,1,employee);sqlite3_bind_text(q,2,localday,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,3,local.tm_hour*60+local.tm_min);
   if(sqlite3_step(q)==SQLITE_ROW)snprintf(scheduled,sizeof scheduled,"%s",sqlite3_column_text(q,0));
  }sqlite3_finalize(q);q=NULL;
  if(!scheduled[0]){
   int bit=1<<((local.tm_wday+6)%7);
   if(sqlite3_prepare_v2(db,"SELECT start_time FROM schedule_profiles WHERE employee_id=?1 AND schedule_mode='recurring_weekly' AND (weekday_mask & ?2)!=0",-1,&q,NULL)==SQLITE_OK){
    sqlite3_bind_int(q,1,employee);sqlite3_bind_int(q,2,bit);
    if(sqlite3_step(q)==SQLITE_ROW)snprintf(scheduled,sizeof scheduled,"%s",sqlite3_column_text(q,0));
   }sqlite3_finalize(q);q=NULL;
  }
  if(scheduled[0]){int hh=0,mm=0;if(sscanf(scheduled,"%d:%d",&hh,&mm)==2){
   struct tm start=local;start.tm_hour=hh;start.tm_min=mm;start.tm_sec=0;start.tm_isdst=-1;
   time_t scheduled_at=mktime(&start);double delta=difftime(at,scheduled_at);if(delta>0)late=(int)((delta+59)/60);
  }}
 }
 }
 } /* end late-arrival eligibility during approved leave */
 if(sqlite3_prepare_v2(db,"INSERT INTO attendance_events(event_id,employee_id,kind,late_minutes,exception,paired_in_event) VALUES(?1,?2,?3,?4,?5,?6)",-1,&q,NULL)!=SQLITE_OK)return 0;
 sqlite3_bind_text(q,1,event,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,2,employee);sqlite3_bind_text(q,3,kind_in?"IN":"OUT",-1,SQLITE_STATIC);
 sqlite3_bind_int(q,4,late);sqlite3_bind_text(q,5,reason,-1,SQLITE_STATIC);
 if(!kind_in)sqlite3_bind_text(q,6,prior_event,-1,SQLITE_TRANSIENT);else sqlite3_bind_null(q,6);
 int ok=sqlite3_step(q)==SQLITE_DONE;sqlite3_finalize(q);
 /* Notification failures must not undo a successfully recorded swipe. */
 int grace=0;sqlite3_stmt*g=NULL;
 if(sqlite3_prepare_v2(db,"SELECT coalesce((SELECT l.grace_minutes FROM clock_late_settings l JOIN swipes s ON s.clock_id=l.clock_id WHERE s.event_id=?1),5)",-1,&g,NULL)==SQLITE_OK){sqlite3_bind_text(g,1,event,-1,SQLITE_TRANSIENT);if(sqlite3_step(g)==SQLITE_ROW)grace=sqlite3_column_int(g,0);}sqlite3_finalize(g);
 /* Only notify beyond grace. Compare exact seconds, not rounded late_minutes. */
 int beyond_grace=0;
 if(late>0){sqlite3_stmt*clockq=NULL;char planned_start[12]="";
  if(sqlite3_prepare_v2(db,"SELECT start_time FROM schedule_shifts WHERE employee_id=?1 AND work_date=?2 AND EXISTS(SELECT 1 FROM schedule_profiles sp WHERE sp.employee_id=?1 AND sp.schedule_mode='dated_shifts') ORDER BY ABS((CAST(substr(start_time,1,2) AS INTEGER)*60+CAST(substr(start_time,4,2) AS INTEGER)) - ?3),start_time LIMIT 1",-1,&clockq,NULL)==SQLITE_OK){sqlite3_bind_int(clockq,1,employee);sqlite3_bind_text(clockq,2,localday,-1,SQLITE_TRANSIENT);sqlite3_bind_int(clockq,3,local.tm_hour*60+local.tm_min);if(sqlite3_step(clockq)==SQLITE_ROW)snprintf(planned_start,sizeof planned_start,"%s",sqlite3_column_text(clockq,0));}sqlite3_finalize(clockq);
  if(!planned_start[0]){int bit=1<<((local.tm_wday+6)%7);if(sqlite3_prepare_v2(db,"SELECT start_time FROM schedule_profiles WHERE employee_id=?1 AND schedule_mode='recurring_weekly' AND (weekday_mask & ?2)!=0",-1,&clockq,NULL)==SQLITE_OK){sqlite3_bind_int(clockq,1,employee);sqlite3_bind_int(clockq,2,bit);if(sqlite3_step(clockq)==SQLITE_ROW)snprintf(planned_start,sizeof planned_start,"%s",sqlite3_column_text(clockq,0));}sqlite3_finalize(clockq);}
  int hh=0,mm=0;if(sscanf(planned_start,"%d:%d",&hh,&mm)==2){struct tm start=local;start.tm_hour=hh;start.tm_min=mm;start.tm_sec=0;start.tm_isdst=-1;beyond_grace=difftime(at,mktime(&start))>grace*60;}
 }
 if(ok&&beyond_grace&&!attendance_email(event,employee,"IN",late,display))fprintf(stderr,"Late arrival stored; email queue failed for %s\n",event);
 return ok;
}
