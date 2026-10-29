/* Attendance v0.9.25. Raw swipes remain authoritative; derived status is provisional.
   All writes run inside the containing swipe transaction; email enqueue is idempotent. */
static int attendance_stamp(const char *stamp,time_t *out,struct tm *local){
 int y,m,d,h,mi,se;
 if(sscanf(stamp,"%d-%d-%dT%d:%d:%dZ",&y,&m,&d,&h,&mi,&se)!=6)return 0;
 if(y<2000||y>2100||m<1||m>12||d<1||d>31||h>23||mi>59||se>60)return 0;
sqlite3_stmt *q=NULL;int ok=0;
 if(sqlite3_prepare_v2(db,"SELECT CAST(strftime('%s',?1) AS INTEGER)",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,stamp,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW&&sqlite3_column_type(q,0)!=SQLITE_NULL){*out=(time_t)sqlite3_column_int64(q,0);ok=1;}}sqlite3_finalize(q);
 return ok&&localtime_r(out,local)!=NULL;
}
/* One schedule lookup for both arrival and departure, anchored to the shift's IN date. */
static int attendance_schedule_day(int employee,const struct tm *workday,time_t *starts,time_t *ends){
 char day[16],start[10]="",finish[10]="";strftime(day,sizeof day,"%Y-%m-%d",workday);
 sqlite3_stmt *q=NULL;
 const char *query="SELECT start_time,end_time FROM schedule_shifts WHERE employee_id=?1 AND work_date=?2 AND EXISTS(SELECT 1 FROM schedule_profiles WHERE employee_id=?1 AND schedule_mode='dated_shifts') ORDER BY ABS((CAST(substr(start_time,1,2) AS INTEGER)*60+CAST(substr(start_time,4,2) AS INTEGER))-?3),start_time LIMIT 1";
 if(sqlite3_prepare_v2(db,query,-1,&q,NULL)!=SQLITE_OK)return 0;
 sqlite3_bind_int(q,1,employee);sqlite3_bind_text(q,2,day,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,3,workday->tm_hour*60+workday->tm_min);
 if(sqlite3_step(q)==SQLITE_ROW){snprintf(start,sizeof start,"%s",sqlite3_column_text(q,0));snprintf(finish,sizeof finish,"%s",sqlite3_column_text(q,1));}sqlite3_finalize(q);q=NULL;
 if(!start[0]){
  if(sqlite3_prepare_v2(db,"SELECT start_time,end_time FROM schedule_profiles WHERE employee_id=?1 AND schedule_mode='recurring_weekly' AND (weekday_mask & ?2)!=0",-1,&q,NULL)!=SQLITE_OK)return 0;
  sqlite3_bind_int(q,1,employee);sqlite3_bind_int(q,2,1<<((workday->tm_wday+6)%7));
  if(sqlite3_step(q)==SQLITE_ROW){snprintf(start,sizeof start,"%s",sqlite3_column_text(q,0));snprintf(finish,sizeof finish,"%s",sqlite3_column_text(q,1));}sqlite3_finalize(q);
 }
 int sh,sm,eh,em;
 if(sscanf(start,"%d:%d",&sh,&sm)!=2||sscanf(finish,"%d:%d",&eh,&em)!=2)return 0;
 struct tm begin=*workday,end=*workday;
 begin.tm_hour=sh;begin.tm_min=sm;begin.tm_sec=0;begin.tm_isdst=-1;
 end.tm_hour=eh;end.tm_min=em;end.tm_sec=0;end.tm_isdst=-1;
 if(strcmp(finish,start)<=0)end.tm_mday++;
 *starts=mktime(&begin);*ends=mktime(&end);return 1;
}
/* After-midnight arrivals can belong to yesterday's overnight shift. */
static int attendance_schedule(int employee,const struct tm *arrival,int grace,time_t *starts,time_t *ends){
 struct tm yesterday=*arrival; yesterday.tm_mday--;yesterday.tm_isdst=-1;
 time_t actual=mktime(&(struct tm){.tm_year=arrival->tm_year,.tm_mon=arrival->tm_mon,.tm_mday=arrival->tm_mday,.tm_hour=arrival->tm_hour,.tm_min=arrival->tm_min,.tm_sec=arrival->tm_sec,.tm_isdst=-1});
 time_t noon=mktime(&yesterday);if(!localtime_r(&noon,&yesterday))return 0;
 time_t ps,pe;
 if(attendance_schedule_day(employee,&yesterday,&ps,&pe)){
  struct tm finish;if(localtime_r(&pe,&finish)&&(finish.tm_year!=yesterday.tm_year||finish.tm_yday!=yesterday.tm_yday)&&actual>=ps&&difftime(actual,pe)<=grace*60){*starts=ps;*ends=pe;return 1;}
 }
 return attendance_schedule_day(employee,arrival,starts,ends);
}
static int attendance_partial_excused(int employee,time_t begin,time_t end){
 sqlite3_stmt *q=NULL;int covered=0;
 const char *query="SELECT 1 FROM attendance_absences WHERE employee_id=?1 AND cancelled_at IS NULL AND reason<>'Unexcused absence' AND starts<>'' AND CAST(strftime('%s',day||' '||starts,'utc') AS INTEGER)<=?2 AND CAST(strftime('%s',day||' '||ends,'utc') AS INTEGER)>=?3 LIMIT 1";
 if(sqlite3_prepare_v2(db,query,-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,employee);sqlite3_bind_int64(q,2,begin);sqlite3_bind_int64(q,3,end);covered=sqlite3_step(q)==SQLITE_ROW;}sqlite3_finalize(q);return covered;
}
static int attendance_excused(int employee,const struct tm *workday){
 char day[16];strftime(day,sizeof day,"%Y-%m-%d",workday);sqlite3_stmt *q=NULL;int excused=0;
 const char *query="SELECT 1 WHERE EXISTS(SELECT 1 FROM timeoff_requests WHERE employee_id=?1 AND status='approved' AND cancelled_at IS NULL AND ?2 BETWEEN starts AND ends) OR EXISTS(SELECT 1 FROM attendance_absences WHERE employee_id=?1 AND day=?2 AND starts='' AND cancelled_at IS NULL AND reason<>'Unexcused absence')";
 if(sqlite3_prepare_v2(db,query,-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,employee);sqlite3_bind_text(q,2,day,-1,SQLITE_TRANSIENT);excused=sqlite3_step(q)==SQLITE_ROW;}sqlite3_finalize(q);return excused;
}
static int attendance_deviation(double seconds,int grace){
 return seconds>grace*60?(int)((seconds+59)/60):0;
}
/* Compare resolved shift starts, so yesterday's overnight arrival cannot hide tonight's. */
static int attendance_late(int employee,const char *event,const char *stamp,int grace,time_t *scheduled){
 time_t at,end;struct tm local,shiftday;
 if(!attendance_stamp(stamp,&at,&local)||!attendance_schedule(employee,&local,grace,scheduled,&end)||at>=end||!localtime_r(scheduled,&shiftday)||attendance_excused(employee,&shiftday)||attendance_partial_excused(employee,*scheduled,at))return 0;
 sqlite3_stmt *q=NULL;int prior=0;
 if(sqlite3_prepare_v2(db,"SELECT p.at FROM effective_punches p LEFT JOIN attendance_events a ON a.event_id=p.id WHERE p.employee_id=?1 AND p.id<>?2 AND p.kind='IN' AND coalesce(a.exception,'') NOT IN ('duplicate_tap','out_of_order') AND p.at<?3 AND julianday(p.at)>=julianday(?3,'-2 days') AND NOT EXISTS(SELECT 1 FROM effective_punches o WHERE o.employee_id=p.employee_id AND o.kind='OUT' AND o.at>p.at AND CAST(strftime('%s',o.at) AS INTEGER)<?4) ORDER BY p.at DESC",-1,&q,NULL)!=SQLITE_OK)return 0;
 sqlite3_bind_int(q,1,employee);sqlite3_bind_text(q,2,event,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,stamp,-1,SQLITE_TRANSIENT);sqlite3_bind_int64(q,4,*scheduled);
 while(sqlite3_step(q)==SQLITE_ROW){time_t previous,ps,pe;struct tm day;if(attendance_stamp((const char*)sqlite3_column_text(q,0),&previous,&day)&&attendance_schedule(employee,&day,grace,&ps,&pe)&&ps==*scheduled){prior=1;break;}}
 sqlite3_finalize(q);return prior?0:attendance_deviation(difftime(at,*scheduled),grace);
}
/* Manager corrections classify the effective completed shift without issuing notices. */
static int attendance_corrected_early(int employee,const char *out,int grace,time_t *finish,time_t frozen_finish){
 *finish=0;
 sqlite3_stmt *q=NULL;char instamp[40]="",outstamp[40]="";
 if(sqlite3_prepare_v2(db,"SELECT in_at,out_at FROM effective_shift_pairs WHERE employee_id=?1 AND out_id=?2 AND state='complete'",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,employee);sqlite3_bind_text(q,2,out,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW){snprintf(instamp,sizeof instamp,"%s",sqlite3_column_text(q,0));snprintf(outstamp,sizeof outstamp,"%s",sqlite3_column_text(q,1));}}sqlite3_finalize(q);q=NULL;
 time_t in_at,out_at,start,end;struct tm in_day,out_day,shiftday;
 if(!attendance_stamp(instamp,&in_at,&in_day)||!attendance_stamp(outstamp,&out_at,&out_day)||!attendance_schedule(employee,&in_day,grace,&start,&end)||!localtime_r(&start,&shiftday))return 0;
 if(frozen_finish)end=frozen_finish;
 if(in_at>=end)return 0;
 *finish=end;
 if(attendance_excused(employee,&shiftday)||attendance_partial_excused(employee,out_at,end))return 0;
 int returned=0;
 if(sqlite3_prepare_v2(db,"SELECT 1 FROM effective_punches WHERE employee_id=?1 AND kind='IN' AND at>?2 AND CAST(strftime('%s',at) AS INTEGER)<=?3 LIMIT 1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,employee);sqlite3_bind_text(q,2,outstamp,-1,SQLITE_TRANSIENT);sqlite3_bind_int64(q,3,end+grace*60);returned=sqlite3_step(q)==SQLITE_ROW;}sqlite3_finalize(q);
 return returned?0:attendance_deviation(difftime(end,out_at),grace);
}
static int attendance_email(const char *event,int employee,const char *kind,int minutes,const char *local_time,time_t scheduled_at){
 int early=!strcmp(kind,"OUT");
 if(minutes<=0||(!early&&strcmp(kind,"IN")))return 1;
 const char *prefix=early?"early":"late";
 sqlite3_stmt *q=NULL;char name[256]="",employee_mail[256]="";int notify=0;
 if(sqlite3_prepare_v2(db,"SELECT name,COALESCE(email,''),late_email_enabled FROM employees WHERE id=?1",-1,&q,NULL)!=SQLITE_OK)return 0;
 sqlite3_bind_int(q,1,employee);
 if(sqlite3_step(q)==SQLITE_ROW){snprintf(name,sizeof name,"%s",sqlite3_column_text(q,0));snprintf(employee_mail,sizeof employee_mail,"%s",sqlite3_column_text(q,1));notify=sqlite3_column_int(q,2);}
 sqlite3_finalize(q);
 if(!notify)return 1; /* Preserve attendance flags, suppress both recipients. */
 {sqlite3_stmt*h=NULL;int skipped=0;
  if(sqlite3_prepare_v2(db,"SELECT 1 FROM holiday_settings hs JOIN statutory_holidays h ON h.day=substr(?1,1,10) WHERE hs.id=1 AND hs.skip_alerts=1",-1,&h,NULL)==SQLITE_OK){
   sqlite3_bind_text(h,1,local_time,-1,SQLITE_TRANSIENT);skipped=sqlite3_step(h)==SQLITE_ROW;
  }sqlite3_finalize(h);if(skipped)return 1;
 }

 char key[160];
 char minutesbuf[30],scheduled[64],date[16];snprintf(minutesbuf,sizeof minutesbuf,"%d",minutes);
 snprintf(date,sizeof date,"%.10s",local_time);
 /* Use the actual selected schedule boundary, never reconstruct it from rounded minutes. */
 struct tm st;
 if(!localtime_r(&scheduled_at,&st))return 0;
 strftime(scheduled,sizeof scheduled,"%Y-%m-%d %H:%M:%S",&st);
 char note_url[400]="";
 /* {{note_link}} renders only in the employee's own email: the link needs no password, so it is
    never copied into manager mail. */
 struct email_field fields[]={
  {"{{employee_name}}",name},{early?"{{scheduled_end}}":"{{scheduled_start}}",scheduled},{early?"{{actual_end}}":"{{actual_start}}",local_time},
  {early?"{{early_minutes}}":"{{late_minutes}}",minutesbuf},{"{{attendance_date}}",date},{"{{note_link}}",""}
 };
 int ok=1;
 for(int recipient_type=0;recipient_type<2;recipient_type++){
  const char *template_id=early?(recipient_type?"early_departure_manager":"early_departure_employee"):(recipient_type?"late_arrival_manager":"late_arrival_employee");
  char *ts=NULL,*tb=NULL;
  int loaded=template_load(db,template_id,&ts,&tb);
  if(loaded<0)return 0;
  if(!loaded)continue;
  fields[5].value="";
  if(recipient_type==0&&email_address_ok(employee_mail)&&(strstr(ts,"{{note_link}}")||strstr(tb,"{{note_link}}"))){
   if(!note_link_create(event,employee,note_url,sizeof note_url)){free(ts);free(tb);return 0;}
   fields[5].value=note_url[0]?note_url:"(contact your manager to add a reason)";
  }
  char *subject=email_render(ts,fields,sizeof fields/sizeof fields[0]);
  char *message=email_render(tb,fields,sizeof fields/sizeof fields[0]);free(ts);free(tb);
  if(!subject||!message){free(subject);free(message);return 0;}
  if(recipient_type==0){
   if(email_address_ok(employee_mail)){
    snprintf(key,sizeof key,"%s:%s:employee",prefix,event);
    ok=email_enqueue(db,employee_mail,subject,message,key,NULL)>=0;
   }
  }else{
   sqlite3_stmt *mgr=NULL;
   if(sqlite3_prepare_v2(db,"SELECT DISTINCT m.id,m.email FROM employees m JOIN employee_roles r ON r.employee_id=m.id AND r.role='manager' WHERE m.active=1 AND m.email LIKE '%@%'",-1,&mgr,NULL)!=SQLITE_OK){free(subject);free(message);return 0;}
   while(ok&&sqlite3_step(mgr)==SQLITE_ROW){
    const char *mail=col_text(mgr,1);if(!email_address_ok(mail))continue;
    snprintf(key,sizeof key,"%s:%s:manager:%d",prefix,event,sqlite3_column_int(mgr,0));
    ok=email_enqueue(db,mail,subject,message,key,NULL)>=0;
   }
   sqlite3_finalize(mgr);
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
 int grace=5;sqlite3_stmt*g=NULL;
 if(sqlite3_prepare_v2(db,"SELECT coalesce((SELECT l.grace_minutes FROM clock_late_settings l JOIN swipes s ON s.clock_id=l.clock_id WHERE s.event_id=?1),5)",-1,&g,NULL)==SQLITE_OK){sqlite3_bind_text(g,1,event,-1,SQLITE_TRANSIENT);if(sqlite3_step(g)==SQLITE_ROW)grace=sqlite3_column_int(g,0);}sqlite3_finalize(g);
 time_t scheduled_at=0,scheduled_end=0;int late=0,early=0,out_of_shift=0;char display[50];strftime(display,sizeof display,"%Y-%m-%d %H:%M:%S",&local);
 if(!exception){
  struct tm workday=local;time_t in_at=at;
  int anchored=kind_in||attendance_stamp(prior_stamp,&in_at,&workday);
  int scheduled=anchored&&attendance_schedule(employee,&workday,grace,&scheduled_at,&scheduled_end);
  out_of_shift=!scheduled||in_at>=scheduled_end;
  if(!kind_in){
   /* Keep the visit's original classification, unless a manager corrected its IN. */
   sqlite3_stmt*saved=NULL;
   if(sqlite3_prepare_v2(db,"SELECT out_of_shift FROM attendance_events WHERE event_id=?1 AND NOT EXISTS(SELECT 1 FROM punch_corrections WHERE event_id=?1)",-1,&saved,NULL)!=SQLITE_OK)return 0;
   sqlite3_bind_text(saved,1,prior_event,-1,SQLITE_TRANSIENT);if(sqlite3_step(saved)==SQLITE_ROW)out_of_shift=sqlite3_column_int(saved,0);sqlite3_finalize(saved);
  }
  if(out_of_shift){scheduled_at=0;scheduled_end=0;}
  else if(kind_in&&scheduled)late=attendance_late(employee,event,stamp,grace,&scheduled_at);
 }
 if(sqlite3_prepare_v2(db,"INSERT INTO attendance_events(event_id,employee_id,kind,late_minutes,exception,paired_in_event,early_minutes,departure_finish,departure_due,departure_ready,out_of_shift) VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11)",-1,&q,NULL)!=SQLITE_OK)return 0;
 sqlite3_bind_text(q,1,event,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,2,employee);sqlite3_bind_text(q,3,kind_in?"IN":"OUT",-1,SQLITE_STATIC);
 sqlite3_bind_int(q,4,late);sqlite3_bind_text(q,5,reason,-1,SQLITE_STATIC);
 if(!kind_in)sqlite3_bind_text(q,6,prior_event,-1,SQLITE_TRANSIENT);else sqlite3_bind_null(q,6);
 sqlite3_bind_int(q,7,early);
 sqlite3_bind_int64(q,8,!kind_in&&!exception?scheduled_end:0);sqlite3_bind_int64(q,9,!kind_in&&!exception&&scheduled_end?scheduled_end+grace*60:0);
 sqlite3_bind_int64(q,10,!kind_in&&!exception&&scheduled_end?time(NULL)+60:0);sqlite3_bind_int(q,11,out_of_shift);
 int ok=sqlite3_step(q)==SQLITE_DONE;sqlite3_finalize(q);
 /* Notification failures must not undo a successfully recorded swipe. */
 if(ok&&late>0&&!attendance_email(event,employee,"IN",late,display,scheduled_at))fprintf(stderr,"Late arrival stored; email queue failed for %s\n",event);

 return ok;
}

/* Corrected/manual OUTs share final-return timing, but never generate another alert. */
static void attendance_corrected_departures_due(time_t now){
 sqlite3_stmt *q=NULL;int count=0;sqlite3_int64 ids[100];
 if(sqlite3_prepare_v2(db,"SELECT id FROM punch_corrections WHERE departure_due>0 AND departure_due<?1 ORDER BY departure_due LIMIT 100",-1,&q,NULL)!=SQLITE_OK)return;
 sqlite3_bind_int64(q,1,now);while(count<100&&sqlite3_step(q)==SQLITE_ROW)ids[count++]=sqlite3_column_int64(q,0);sqlite3_finalize(q);q=NULL;
 for(int i=0;i<count;i++){
  if(!sql("SAVEPOINT corrected_departure"))return;
  int ok=1,employee=0,minutes=0;time_t finish=0,due=0,unused=0;char event[90]="";
  if(sqlite3_prepare_v2(db,"SELECT employee_id,coalesce(event_id,manual_id),departure_finish,departure_due FROM punch_corrections WHERE id=?1 AND action<>'void' AND kind='OUT'",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int64(q,1,ids[i]);if(sqlite3_step(q)==SQLITE_ROW){employee=sqlite3_column_int(q,0);snprintf(event,sizeof event,"%s",sqlite3_column_text(q,1));finish=(time_t)sqlite3_column_int64(q,2);due=(time_t)sqlite3_column_int64(q,3);}}else ok=0;sqlite3_finalize(q);q=NULL;
  if(employee)minutes=attendance_corrected_early(employee,event,(int)((due-finish)/60),&unused,finish);
  if(ok)ok=sqlite3_prepare_v2(db,"UPDATE punch_corrections SET early_minutes=?2,departure_due=0 WHERE id=?1",-1,&q,NULL)==SQLITE_OK;
  if(ok){sqlite3_bind_int64(q,1,ids[i]);sqlite3_bind_int(q,2,minutes);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);q=NULL;
  if(ok&&event[0]){ok=sqlite3_prepare_v2(db,"UPDATE attendance_events SET early_minutes=?2 WHERE event_id=?1",-1,&q,NULL)==SQLITE_OK;if(ok){sqlite3_bind_text(q,1,event,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,2,minutes);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);q=NULL;}
  if(ok)sql("RELEASE corrected_departure");else{sql("ROLLBACK TO corrected_departure");sql("RELEASE corrected_departure");}
 }
}
/* Called by the server's minute tick. Source times, finish and grace are frozen at OUT.
   A return before finish+grace makes the earlier OUT an intermediate departure. */
static void attendance_departures_due(time_t now){
 attendance_corrected_departures_due(now);
 sqlite3_stmt *q=NULL;
 if(sqlite3_prepare_v2(db,"SELECT event_id FROM attendance_events WHERE departure_due>0 AND departure_due<?1 AND departure_ready<?1 ORDER BY departure_due LIMIT 100",-1,&q,NULL)!=SQLITE_OK)return;
 sqlite3_bind_int64(q,1,now);char events[100][90];int count=0;
 while(count<100&&sqlite3_step(q)==SQLITE_ROW){snprintf(events[count++],90,"%s",sqlite3_column_text(q,0));}sqlite3_finalize(q);
 for(int i=0;i<count;i++){
  if(!sql("SAVEPOINT departure_review"))return;
  int ok=1,employee=0,minutes=0;time_t finish=0,due=0,at=0;struct tm local;char stamp[40]="";
  const char *query="SELECT a.employee_id,a.departure_finish,a.departure_due,p.at FROM attendance_events a JOIN effective_punches p ON p.id=a.event_id WHERE a.event_id=?1 AND p.kind='OUT' AND a.exception='' AND NOT EXISTS(SELECT 1 FROM effective_punches r WHERE r.employee_id=a.employee_id AND r.kind='IN' AND r.at>p.at AND CAST(strftime('%s',r.at) AS INTEGER)<=a.departure_due) AND NOT EXISTS(SELECT 1 FROM punch_corrections c WHERE c.event_id=a.event_id)";
  if(sqlite3_prepare_v2(db,query,-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,events[i],-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW){employee=sqlite3_column_int(q,0);finish=(time_t)sqlite3_column_int64(q,1);due=(time_t)sqlite3_column_int64(q,2);snprintf(stamp,sizeof stamp,"%s",sqlite3_column_text(q,3));}}else ok=0;sqlite3_finalize(q);q=NULL;
  if(employee&&attendance_stamp(stamp,&at,&local)){
   time_t begin=0,end=0;struct tm shiftday=local;
   /* Full-day leave is anchored to the shift selected at IN. */
   sqlite3_stmt *in=NULL;char instamp[40]="";
   if(sqlite3_prepare_v2(db,"SELECT p.at FROM attendance_events a JOIN effective_punches p ON p.id=a.paired_in_event WHERE a.event_id=?1",-1,&in,NULL)==SQLITE_OK){sqlite3_bind_text(in,1,events[i],-1,SQLITE_TRANSIENT);if(sqlite3_step(in)==SQLITE_ROW)snprintf(instamp,sizeof instamp,"%s",sqlite3_column_text(in,0));}sqlite3_finalize(in);
   struct tm inday;time_t intime;
   if(attendance_stamp(instamp,&intime,&inday)&&attendance_schedule(employee,&inday,(int)((due-finish)/60),&begin,&end))localtime_r(&begin,&shiftday);
   if(!attendance_excused(employee,&shiftday)&&!attendance_partial_excused(employee,at,finish))minutes=attendance_deviation(difftime(finish,at),(int)((due-finish)/60));
  }
  if(ok&&sqlite3_prepare_v2(db,"UPDATE attendance_events SET early_minutes=?2,departure_due=0 WHERE event_id=?1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,events[i],-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,2,minutes);ok=sqlite3_step(q)==SQLITE_DONE;}else ok=0;sqlite3_finalize(q);q=NULL;
  if(ok&&minutes){char display[50];strftime(display,sizeof display,"%Y-%m-%d %H:%M:%S",&local);ok=attendance_email(events[i],employee,"OUT",minutes,display,finish);}
  if(ok)sql("RELEASE departure_review");else{sql("ROLLBACK TO departure_review");sql("RELEASE departure_review");}
 }
}
