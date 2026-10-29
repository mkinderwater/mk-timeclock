#ifndef PAYROLL_TIMESHEET_H
#define PAYROLL_TIMESHEET_H
#include "vacation_usage.h"
#include "employee_number.h"
#include "period_review.h"
/* Shared source rows for email and CSV: corrected punches, never raw UID records. */
static void payroll_csv_value(sqlite3_str *csv,const char *value){
 sqlite3_str_appendchar(csv,1,'"');
 for(const char *p=value?value:"";*p;p++){if(*p=='"')sqlite3_str_appendchar(csv,1,'"');if(*p=='\r'||*p=='\n')sqlite3_str_appendchar(csv,1,' ');else sqlite3_str_appendchar(csv,1,*p);}
 sqlite3_str_appendchar(csv,1,'"');
}
static void payroll_csv_employee(sqlite3_str *csv,sqlite3 *db,int id){
 char number[65],internal[32];employee_number(db,id,number,sizeof number);snprintf(internal,sizeof internal,"%d",id);
 sqlite3_str_appendchar(csv,1,',');
 if(!strcmp(number,internal))sqlite3_str_appendall(csv,number);else payroll_csv_value(csv,number);
 sqlite3_str_appendall(csv,"\r\n");
}
/* Only the period list opts into request-local materialized punches. */
static int payroll_report_prepare(sqlite3 *db,const char *query,sqlite3_stmt **stmt,int cached){
 if(!cached)return sqlite3_prepare_v2(db,query,-1,stmt,NULL);
 const char *rest=query;int recursive=0;
 if(!strncmp(rest,"WITH RECURSIVE ",15)){recursive=1;rest+=15;}
 else if(!strncmp(rest,"WITH ",5))rest+=5;
 char *sqltext=sqlite3_mprintf("WITH %seffective_punches AS (SELECT * FROM temp.lite_period_punches)%s%s",recursive?"RECURSIVE ":"",rest==query?" ":", ",rest);
 if(!sqltext)return SQLITE_NOMEM;
 int rc=sqlite3_prepare_v2(db,sqltext,-1,stmt,NULL);sqlite3_free(sqltext);return rc;
}
static int payroll_build_timesheet_source(sqlite3 *db,const char *begin,const char *end,char **body,char **csv_out,int cached,int preview){
 *body=NULL;*csv_out=NULL;
 sqlite3_str *txt=sqlite3_str_new(db),*csv=sqlite3_str_new(db);
 sqlite3_str_appendf(txt,"Payroll attendance | %s to %s\nAttendance period\n\nDaily timesheets (local time)\n",begin,end);
 sqlite3_str_appendall(csv,"Employee,RFID ID,Date,IN,OUT,Worked minutes,Reason / correction,Status,Worked hours,Employee ID\r\n");
 /* An OUT must immediately follow its IN. Never pair across another IN. */
 const char *query_prefix=cached?
 "WITH intervals AS (SELECT * FROM temp.lite_period_report_intervals WHERE date(at,'localtime') BETWEEN ?1 AND ?2) ":
 "WITH punches AS (SELECT p.employee_id,p.id,p.at,p.kind,p.manual,"
 " lead(p.at) OVER (PARTITION BY p.employee_id ORDER BY p.at,p.id) AS next_at,"
 " lead(p.kind) OVER (PARTITION BY p.employee_id ORDER BY p.at,p.id) AS next_kind,"
 " lead(p.id) OVER (PARTITION BY p.employee_id ORDER BY p.at,p.id) AS next_id "
 " FROM effective_punches p), intervals AS ("
 " SELECT employee_id,id,at,next_at,next_id,manual,"
 " CAST(strftime('%s',next_at) AS INTEGER)-CAST(strftime('%s',at) AS INTEGER) AS seconds "
 " FROM punches WHERE kind='IN' AND next_kind='OUT' AND next_at IS NOT NULL) ";
 const char *query_tail=
 "SELECT e.name,date(i.at,'localtime'),strftime('%H:%M',i.at,'localtime'),"
 "strftime('%H:%M',i.next_at,'localtime'),i.seconds,"
 "trim(CASE WHEN coalesce(a.exception,'') IN ('missing_out','missing_in') THEN '' ELSE coalesce(a.exception,'') END||CASE WHEN coalesce(n.note,'')<>'' THEN ' '||n.note ELSE '' END||"
 "CASE WHEN coalesce(c.reason,'')<>'' THEN ' Manager correction: '||c.reason ELSE '' END||"
 "CASE WHEN coalesce(co.reason,'')<>'' THEN ' Manager corrected OUT: '||co.reason ELSE '' END||"
 "CASE WHEN coalesce(coadd.reason,'')<>'' THEN ' Manager added OUT: '||coadd.reason ELSE '' END||"
 "CASE WHEN coalesce(co.early_minutes,coadd.early_minutes,ae.early_minutes,0)>0 THEN ' Left early: '||coalesce(co.early_minutes,coadd.early_minutes,ae.early_minutes,0)||' min' ELSE '' END||CASE WHEN coalesce(ne.note,'')<>'' THEN CASE WHEN ne.updated_by=i.employee_id THEN ' Departure reason: ' ELSE ' Manager note: ' END||ne.note ELSE '' END||CASE WHEN coalesce(c.late_minutes,ci.late_minutes,a.late_minutes,0)>0 THEN ' Late arrival: '||coalesce(c.late_minutes,ci.late_minutes,a.late_minutes,0)||' min' ELSE '' END),"
 "i.manual, i.at, i.next_at, i.employee_id, "
 "coalesce((SELECT s.card_uid FROM swipes s WHERE s.event_id=i.id),''), "
 "coalesce((SELECT group_concat(uid,', ') FROM cards WHERE employee_id=i.employee_id AND active=1),''), "
 "coalesce(c.late_minutes,ci.late_minutes,a.late_minutes,0), CASE WHEN c.id IS NOT NULL OR co.id IS NOT NULL OR coadd.id IS NOT NULL OR i.manual=1 THEN 1 ELSE 0 END,coalesce(co.early_minutes,coadd.early_minutes,ae.early_minutes,0) "
 "FROM intervals i JOIN employees e ON e.id=i.employee_id "
 "LEFT JOIN attendance_events a ON a.event_id=i.id "
 "LEFT JOIN swipe_notes n ON n.event_id=i.id "
 "LEFT JOIN attendance_events ae ON ae.event_id=i.next_id "
 "LEFT JOIN swipe_notes ne ON ne.event_id=i.next_id "
 "LEFT JOIN punch_corrections c ON c.event_id=i.id AND c.action='change' "
 "LEFT JOIN punch_corrections ci ON ci.manual_id=i.id "
 "LEFT JOIN punch_corrections co ON co.event_id=i.next_id AND co.action='change' "
 "LEFT JOIN punch_corrections coadd ON coadd.manual_id=i.next_id AND coadd.action='add' "
 "WHERE date(i.at,'localtime') BETWEEN ?1 AND ?2 ORDER BY e.name,e.id,i.at,i.id";
 sqlite3_stmt *q=NULL;
 char *query=sqlite3_mprintf("%s%s",query_prefix,query_tail);
 int prepared=query?payroll_report_prepare(db,query,&q,cached):SQLITE_NOMEM;sqlite3_free(query);
 if(prepared!=SQLITE_OK){sqlite3_str_finish(txt);sqlite3_str_finish(csv);return 0;}
 sqlite3_bind_text(q,1,begin,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,end,-1,SQLITE_TRANSIENT);
 char prev_day[16]="";long long day=0,period=0;int count=0,invalid_intervals=0,long_shifts=0,current_employee=0;
 while(sqlite3_step(q)==SQLITE_ROW){
  const char *name=(const char*)sqlite3_column_text(q,0),*date=(const char*)sqlite3_column_text(q,1);
  const char *in=(const char*)sqlite3_column_text(q,2),*out=(const char*)sqlite3_column_text(q,3),*reason=(const char*)sqlite3_column_text(q,5);
  if(!name)name="Unknown";
  if(!date)date="";
  if(!in)in="";
  if(!out)out="";
  if(!reason)reason="";
  /* Separate a recorded explanation from the automatically generated lateness note. */
  char *formatted_reason=NULL;
  const char *late_marker=strstr(reason," Late arrival:");
  if(late_marker && late_marker!=reason){
   formatted_reason=sqlite3_mprintf("%.*s; %s",(int)(late_marker-reason),reason,late_marker+1);
   if(formatted_reason)reason=formatted_reason;
  }
  long long seconds=sqlite3_column_int64(q,4);int valid=seconds>=0;int long_shift=seconds>72000;
  int employee_id=sqlite3_column_int(q,9);
  if(current_employee!=employee_id||strcmp(prev_day,date)){
   if(prev_day[0])sqlite3_str_appendf(txt,"  Daily total: %lldh %02lldm\n",day/3600,(day%3600)/60);
   day=0;
   if(current_employee!=employee_id){
    if(current_employee){sqlite3_str_appendf(txt,"Period total: %lldh %02lldm\n",period/3600,(period%3600)/60);sqlite3_str_appendchar(txt,1,'\n');}
    period=0;current_employee=employee_id;const char *active_cards=(const char*)sqlite3_column_text(q,11);
    char display_id[65];employee_number(db,employee_id,display_id,sizeof display_id);sqlite3_str_appendf(txt,"\n%s (Employee #%s)\n",name,display_id);
    if(active_cards&&active_cards[0])sqlite3_str_appendf(txt,"  RFID ID: %s\n",active_cards);
    else sqlite3_str_appendall(txt,"  RFID ID: No active card\n");
   }
   sqlite3_str_appendf(txt,"%s\n",date);
   snprintf(prev_day,sizeof prev_day,"%s",date);
  }
  if(valid){day+=seconds;period+=seconds;}
  if(!valid)invalid_intervals++;
  else if(long_shift)long_shifts++;
  const char *uid=(const char*)sqlite3_column_text(q,10);
  if(!uid||!uid[0])uid=sqlite3_column_int(q,6)?"Manager entry":"Not recorded";
  const char *status=!valid?"CHECK: invalid interval":long_shift?"Complete - long shift (over 20 h)":(sqlite3_column_int(q,13)?"Complete - manager correction":sqlite3_column_int(q,12)?(sqlite3_column_int(q,14)?"Complete - late arrival and early departure":"Complete - late arrival"):sqlite3_column_int(q,14)?"Complete - early departure":"Complete");
  sqlite3_str_appendf(txt,"  IN %s  OUT %s  %s",in,out,!valid?"CHECK: invalid interval":long_shift?"Long shift (over 20 h)  ":"");
  if(valid)sqlite3_str_appendf(txt,"%lldh %02lldm",seconds/3600,(seconds%3600)/60);
  sqlite3_str_appendf(txt,"  [RFID: %s]\n",uid);
  if(reason[0])sqlite3_str_appendf(txt,"    Reason: %s\n",reason);
  if(sqlite3_column_int(q,6))sqlite3_str_appendall(txt,"    Manager-added punch\n");
  payroll_csv_value(csv,name);sqlite3_str_appendchar(csv,1,',');payroll_csv_value(csv,uid);sqlite3_str_appendchar(csv,1,',');payroll_csv_value(csv,date);sqlite3_str_appendchar(csv,1,',');
  payroll_csv_value(csv,in);sqlite3_str_appendchar(csv,1,',');payroll_csv_value(csv,out);
  sqlite3_str_appendf(csv,",%lld,",valid?seconds/60:0);payroll_csv_value(csv,reason);
  sqlite3_str_appendchar(csv,1,',');payroll_csv_value(csv,status);sqlite3_str_appendf(csv,",%.2f",valid?seconds/3600.0:0.0);payroll_csv_employee(csv,db,employee_id);count++;sqlite3_free(formatted_reason);
 }
 sqlite3_finalize(q);
 if(prev_day[0]){sqlite3_str_appendf(txt,"  Daily total: %lldh %02lldm\nPeriod total: %lldh %02lldm\n",day/3600,(day%3600)/60,period/3600,(period%3600)/60);}
 if(!count)sqlite3_str_appendall(txt,"No completed IN/OUT intervals in this period.\n");
 /* Unpaired effective punches must remain visible, not silently omitted. */
 sqlite3_stmt *unpaired=NULL;
 const char *unpaired_sql=
 "WITH ordered AS (SELECT employee_id,id,at,kind,manual,"
 "lag(kind) OVER (PARTITION BY employee_id ORDER BY at,id) AS prev_kind,"
 "lag(at) OVER (PARTITION BY employee_id ORDER BY at,id) AS prev_at,"
 "lead(kind) OVER (PARTITION BY employee_id ORDER BY at,id) AS next_kind,"
 "lead(at) OVER (PARTITION BY employee_id ORDER BY at,id) AS next_at FROM effective_punches) "
 "SELECT e.name,o.employee_id,date(o.at,'localtime'),strftime('%H:%M',o.at,'localtime'),o.kind,o.manual,"
 "coalesce((SELECT group_concat(uid,', ') FROM cards WHERE employee_id=o.employee_id AND active=1),''),"
 "coalesce((SELECT s.card_uid FROM swipes s WHERE s.event_id=o.id),''),o.at,o.next_kind "
 "FROM ordered o JOIN employees e ON e.id=o.employee_id WHERE date(o.at,'localtime') BETWEEN ?1 AND ?2 AND "
 "((o.kind='IN' AND coalesce(o.next_kind,'')<>'OUT') OR "
 "(o.kind='OUT' AND coalesce(o.prev_kind,'')<>'IN')) "
 "ORDER BY e.name,o.employee_id,o.at,o.id";
 if(cached)unpaired_sql=
 "SELECT e.name,o.employee_id,date(o.at,'localtime'),strftime('%H:%M',o.at,'localtime'),o.kind,o.manual,"
 "coalesce((SELECT group_concat(uid,', ') FROM cards WHERE employee_id=o.employee_id AND active=1),''),"
 "coalesce((SELECT s.card_uid FROM swipes s WHERE s.event_id=o.id),''),o.at,o.next_kind "
 "FROM temp.lite_period_unpaired o JOIN employees e ON e.id=o.employee_id WHERE date(o.at,'localtime') BETWEEN ?1 AND ?2 "
 "ORDER BY e.name,o.employee_id,o.at,o.id";
 if(sqlite3_prepare_v2(db,unpaired_sql,-1,&unpaired,NULL)!=SQLITE_OK){sqlite3_free(sqlite3_str_finish(txt));sqlite3_free(sqlite3_str_finish(csv));return 0;}
 sqlite3_bind_text(unpaired,1,begin,-1,SQLITE_TRANSIENT);
 sqlite3_bind_text(unpaired,2,end,-1,SQLITE_TRANSIENT);
 int unpaired_count=0,unpaired_rc;
 while((unpaired_rc=sqlite3_step(unpaired))==SQLITE_ROW){
  const char *name=(const char*)sqlite3_column_text(unpaired,0),*date=(const char*)sqlite3_column_text(unpaired,2);
  const char *time=(const char*)sqlite3_column_text(unpaired,3),*kind=(const char*)sqlite3_column_text(unpaired,4);
  const char *cards=(const char*)sqlite3_column_text(unpaired,6),*uid=(const char*)sqlite3_column_text(unpaired,7);
  int eid=sqlite3_column_int(unpaired,1),is_in=kind&&!strcmp(kind,"IN");
  int active=0;
  
  /* Only the latest IN within 20 hours on an open period is still in progress. */
  if(preview&&is_in&&sqlite3_column_type(unpaired,9)==SQLITE_NULL){
   sqlite3_stmt *state=NULL;
   if(sqlite3_prepare_v2(db,"SELECT status FROM pay_periods WHERE starts=?1 AND ends=?2",-1,&state,NULL)==SQLITE_OK){
    sqlite3_bind_text(state,1,begin,-1,SQLITE_TRANSIENT);sqlite3_bind_text(state,2,end,-1,SQLITE_TRANSIENT);
    if(sqlite3_step(state)==SQLITE_ROW&&!strcmp((const char*)sqlite3_column_text(state,0),"open")){
     const char *stamp=(const char*)sqlite3_column_text(unpaired,8);
     if(stamp&&*stamp){sqlite3_stmt *age=NULL;
      if(sqlite3_prepare_v2(db,"SELECT CAST(strftime('%s','now') AS INTEGER)-CAST(strftime('%s',?1) AS INTEGER) BETWEEN 0 AND 71999",-1,&age,NULL)==SQLITE_OK){sqlite3_bind_text(age,1,stamp,-1,SQLITE_TRANSIENT);if(sqlite3_step(age)==SQLITE_ROW)active=sqlite3_column_int(age,0);}sqlite3_finalize(age);
     }
    }
   }sqlite3_finalize(state);
  }
  const char *flag=is_in?(active?"Clocked in, no OUT yet":"CHECK: Missing OUT"):"CHECK: Missing IN";
  if(!unpaired_count++)sqlite3_str_appendall(txt,"\nUnpaired punches (no hours inferred)\n");
  sqlite3_str_appendf(txt,"  %s: %s %s %s\n",name?name:"Unknown",date?date:"",kind?kind:"",time?time:"");
  payroll_csv_value(csv,name?name:"Unknown");sqlite3_str_appendchar(csv,1,',');
  payroll_csv_value(csv,uid&&*uid?uid:cards?cards:"");sqlite3_str_appendchar(csv,1,',');
  payroll_csv_value(csv,date?date:"");sqlite3_str_appendchar(csv,1,',');
  payroll_csv_value(csv,is_in?(time?time:"") : "");sqlite3_str_appendchar(csv,1,',');
  payroll_csv_value(csv,is_in?"":(time?time:""));sqlite3_str_appendall(csv,",0,,");
  payroll_csv_value(csv,flag);sqlite3_str_appendall(csv,",0.00");payroll_csv_employee(csv,db,eid);
 }
 sqlite3_finalize(unpaired);
 if(unpaired_rc!=SQLITE_DONE){sqlite3_free(sqlite3_str_finish(txt));sqlite3_free(sqlite3_str_finish(csv));return 0;}
 if(unpaired_count)sqlite3_str_appendf(txt,"%d unpaired punches listed; CHECK rows require review.\n",unpaired_count);

 /* Approved manager-recorded absences are attendance context, never paid hours. */
 sqlite3_stmt *absence=NULL;
 const char *absence_sql="SELECT e.name,e.id,a.day,a.reason,a.note,a.starts,a.ends,coalesce((SELECT group_concat(uid,', ') FROM cards WHERE employee_id=e.id AND active=1),'') FROM attendance_absences a JOIN employees e ON e.id=a.employee_id WHERE a.cancelled_at IS NULL AND a.day BETWEEN ?1 AND ?2 ORDER BY e.name,e.id,a.day,a.id";
 if(sqlite3_prepare_v2(db,absence_sql,-1,&absence,NULL)!=SQLITE_OK){sqlite3_str_finish(txt);sqlite3_str_finish(csv);return 0;}
 sqlite3_bind_text(absence,1,begin,-1,SQLITE_TRANSIENT);sqlite3_bind_text(absence,2,end,-1,SQLITE_TRANSIENT);int absence_rows=0;
 while(sqlite3_step(absence)==SQLITE_ROW){const char *n=(const char*)sqlite3_column_text(absence,0),*d=(const char*)sqlite3_column_text(absence,2),*r=(const char*)sqlite3_column_text(absence,3),*note=(const char*)sqlite3_column_text(absence,4),*st=(const char*)sqlite3_column_text(absence,5),*en=(const char*)sqlite3_column_text(absence,6),*uid=(const char*)sqlite3_column_text(absence,7);
 if(!absence_rows++)sqlite3_str_appendall(txt,"\nManager-recorded absences (no attendance hours added)\n");
 char display_id[65];employee_number(db,sqlite3_column_int(absence,1),display_id,sizeof display_id);sqlite3_str_appendf(txt,"  %s (Employee #%s): %s, %s%s%s%s%s%s\n",n?n:"Unknown",display_id,d?d:"",r?r:"",st&&*st?" (":"",st&&*st?st:"",st&&*st?" to ":"",st&&*st?en:"",st&&*st?")":"");if(note&&*note)sqlite3_str_appendf(txt,"    Manager note: %s\n",note);
 payroll_csv_value(csv,n?n:"Unknown");sqlite3_str_appendchar(csv,1,',');payroll_csv_value(csv,uid?uid:"");sqlite3_str_appendchar(csv,1,',');payroll_csv_value(csv,d?d:"");sqlite3_str_appendall(csv,",,,0,");payroll_csv_value(csv,note?note:"");sqlite3_str_appendall(csv,",");char status[200];snprintf(status,sizeof status,"Recorded absence: %s%s%s%s",r?r:"Other",st&&*st?" (":"",st&&*st?st:"",st&&*st?" to ":"");if(st&&*st){size_t used=strlen(status);snprintf(status+used,sizeof(status)-used,"%s)",en?en:"");}payroll_csv_value(csv,status);sqlite3_str_appendall(csv,",0.00");payroll_csv_employee(csv,db,sqlite3_column_int(absence,1));
 }sqlite3_finalize(absence);
 /* Holidays are attendance context even when no vacation request exists.
    They never create punches or paid hours. */
 sqlite3_stmt *holidays=NULL;int holiday_count=0;
 const char *holiday_sql="SELECT e.name,e.id,h.day,h.name,coalesce((SELECT group_concat(uid,', ') FROM cards WHERE employee_id=e.id AND active=1),'') FROM statutory_holidays h CROSS JOIN employees e WHERE h.day BETWEEN ?1 AND ?2 AND (SELECT ah.active FROM employee_active_history ah WHERE ah.employee_id=e.id AND julianday(ah.effective_at)<julianday(h.day,'+1 day') ORDER BY julianday(ah.effective_at) DESC,ah.id DESC LIMIT 1)=1 AND NOT EXISTS(SELECT 1 FROM timeoff_requests t WHERE t.employee_id=e.id AND t.status='approved' AND t.cancelled_at IS NULL AND h.day BETWEEN t.starts AND t.ends) ORDER BY e.name,e.id,h.day";
 if(sqlite3_prepare_v2(db,holiday_sql,-1,&holidays,NULL)==SQLITE_OK){
  sqlite3_bind_text(holidays,1,begin,-1,SQLITE_TRANSIENT);sqlite3_bind_text(holidays,2,end,-1,SQLITE_TRANSIENT);
  while(sqlite3_step(holidays)==SQLITE_ROW){
   const char *person=(const char*)sqlite3_column_text(holidays,0),*day=(const char*)sqlite3_column_text(holidays,2),*holiday=(const char*)sqlite3_column_text(holidays,3),*card=(const char*)sqlite3_column_text(holidays,4);int eid=sqlite3_column_int(holidays,1);
   if(!holiday_count++)sqlite3_str_appendall(txt,"\nStatutory holidays (attendance context only)\n");
   char display_id[65];employee_number(db,eid,display_id,sizeof display_id);sqlite3_str_appendf(txt,"  %s (Employee #%s): %s, %s, 0 recorded hours\n",person?person:"Unknown",display_id,day?day:"",holiday?holiday:"Stat holiday");
   payroll_csv_value(csv,person?person:"Unknown");sqlite3_str_appendchar(csv,1,',');payroll_csv_value(csv,card?card:"");sqlite3_str_appendchar(csv,1,',');payroll_csv_value(csv,day?day:"");sqlite3_str_appendall(csv,",,,0,,");
   char label[256];snprintf(label,sizeof label,"Stat holiday: %s",holiday?holiday:"Holiday");payroll_csv_value(csv,label);sqlite3_str_appendall(csv,",0.00");payroll_csv_employee(csv,db,eid);
  }
 }sqlite3_finalize(holidays);
 /* Historical context only: the most recent approved vacation weekday before this
    period, independent of punches. Never count it as current leave or paid hours. */
 sqlite3_stmt *history=NULL;
 const char *history_sql=
 "WITH relevant(id) AS ("
 " SELECT id FROM employees WHERE active=1"
 " UNION SELECT employee_id FROM effective_punches WHERE date(at,'localtime') BETWEEN ?1 AND ?2"
 " UNION SELECT employee_id FROM timeoff_requests WHERE starts<=?2 AND ends>=?1) "
 "SELECT e.name,e.id,coalesce((SELECT group_concat(uid,', ') FROM cards WHERE employee_id=e.id AND active=1),''),"
 " (SELECT max(CASE CAST(strftime('%w',MIN(t.ends,date(?1,'-1 day'))) AS INTEGER)"
 " WHEN 0 THEN date(MIN(t.ends,date(?1,'-1 day')),'-2 days')"
 " WHEN 6 THEN date(MIN(t.ends,date(?1,'-1 day')),'-1 day')"
 " ELSE MIN(t.ends,date(?1,'-1 day')) END)"
 " FROM timeoff_requests t WHERE t.employee_id=e.id AND t.kind='vacation'"
 " AND t.status='approved' AND t.cancelled_at IS NULL AND t.starts<?1"
 " AND (CASE CAST(strftime('%w',MIN(t.ends,date(?1,'-1 day'))) AS INTEGER)"
 " WHEN 0 THEN date(MIN(t.ends,date(?1,'-1 day')),'-2 days')"
 " WHEN 6 THEN date(MIN(t.ends,date(?1,'-1 day')),'-1 day')"
 " ELSE MIN(t.ends,date(?1,'-1 day')) END)>=t.starts)"
 " FROM relevant r JOIN employees e ON e.id=r.id ORDER BY e.name,e.id";
 if(payroll_report_prepare(db,history_sql,&history,cached)!=SQLITE_OK){sqlite3_str_finish(txt);sqlite3_str_finish(csv);return 0;}
 sqlite3_bind_text(history,1,begin,-1,SQLITE_TRANSIENT);
 sqlite3_bind_text(history,2,end,-1,SQLITE_TRANSIENT);
 int history_count=0;
 while(sqlite3_step(history)==SQLITE_ROW){
  const char *last=(const char*)sqlite3_column_text(history,3);
  if(!last||!*last)continue;
  const char *name=(const char*)sqlite3_column_text(history,0);
  const char *card=(const char*)sqlite3_column_text(history,2);
  if(!history_count++)sqlite3_str_appendall(txt,"\nPrevious approved vacation (reference only)\n");
  char display_id[65];employee_number(db,sqlite3_column_int(history,1),display_id,sizeof display_id);sqlite3_str_appendf(txt,"  %s (Employee #%s, RFID: %s): last approved vacation day before this period: %s\n",name?name:"Unknown",display_id,card&&*card?card:"No active card",last);
  payroll_csv_value(csv,name?name:"Unknown");sqlite3_str_appendchar(csv,1,',');payroll_csv_value(csv,card?card:"");sqlite3_str_appendall(csv,",,,,0,,");
  char historical_status[128];snprintf(historical_status,sizeof historical_status,"Previous approved vacation day: %s (reference only)",last);
  payroll_csv_value(csv,historical_status);sqlite3_str_appendall(csv,",0.00");payroll_csv_employee(csv,db,sqlite3_column_int(history,1));
 }
 sqlite3_finalize(history);
 /* Use defined salary weekdays or dated hourly shifts; each hourly weekday
    without a dated shift falls back to a clearly labelled unscheduled day.
    A statutory holiday is shown separately and must not be counted as vacation. */
 sqlite3_stmt *leave=NULL;
 const char *leave_sql=
 "WITH RECURSIVE days(d) AS (SELECT date(?1) UNION ALL SELECT date(d,'+1 day') FROM days WHERE d<date(?2)), "
 "eligible AS (SELECT t.id,t.employee_id,t.kind,d.d,e.name, "
 "coalesce((SELECT group_concat(uid,', ') FROM cards WHERE employee_id=e.id AND active=1),'') card, "
 "e.attendance_mode attendance_mode, (SELECT schedule_mode FROM schedule_profiles WHERE employee_id=e.id) pay_type, "
 "coalesce((SELECT weekday_mask FROM schedule_profiles WHERE employee_id=e.id),0) mask, "
 "EXISTS(SELECT 1 FROM schedule_shifts ss JOIN schedule_profiles sp ON sp.employee_id=ss.employee_id AND sp.schedule_mode='dated_shifts' WHERE ss.employee_id=e.id AND ss.work_date=d.d) has_shift, "
  "(SELECT name FROM statutory_holidays h WHERE h.day=d.d) holiday "
 "FROM timeoff_requests t JOIN employees e ON e.id=t.employee_id JOIN days d ON d.d BETWEEN t.starts AND t.ends "
 "WHERE t.status='approved' AND t.cancelled_at IS NULL AND t.starts<=?2 AND t.ends>=?1) "
 "SELECT name,employee_id,d,kind,card,id, "
 "EXISTS(SELECT 1 FROM effective_punches p WHERE p.employee_id=eligible.employee_id AND date(p.at,'localtime')=eligible.d), "
 "CASE WHEN pay_type='recurring_weekly' THEN 'scheduled' WHEN has_shift THEN 'scheduled' ELSE 'no shift scheduled' END, holiday,attendance_mode "
 "FROM eligible WHERE holiday IS NOT NULL OR "
 "(pay_type='recurring_weekly' AND (mask & (1 << ((CAST(strftime('%w',d) AS INTEGER)+6)%7)))<>0) "
 "OR (pay_type='dated_shifts' AND has_shift) "
 "OR ((pay_type IS NULL OR pay_type='dated_shifts' OR pay_type='none') AND CAST(strftime('%w',d) AS INTEGER) BETWEEN 1 AND 5) "
 "ORDER BY name,employee_id,d,id";
 if(payroll_report_prepare(db,leave_sql,&leave,cached)!=SQLITE_OK){sqlite3_str_finish(txt);sqlite3_str_finish(csv);return 0;}
 sqlite3_bind_text(leave,1,begin,-1,SQLITE_TRANSIENT);sqlite3_bind_text(leave,2,end,-1,SQLITE_TRANSIENT);
 int leave_rows=0;
 while(sqlite3_step(leave)==SQLITE_ROW){
  const char *n=(const char*)sqlite3_column_text(leave,0),*d=(const char*)sqlite3_column_text(leave,2);
  const char *kind=(const char*)sqlite3_column_text(leave,3),*card=(const char*)sqlite3_column_text(leave,4);
  int eid=sqlite3_column_int(leave,1);
  if(!n)n="Unknown";
  if(!d)d="";
  if(!kind)kind="time off";
  if(!card)card="";
  int had_punches=sqlite3_column_int(leave,6);
  const char *schedule=(const char*)sqlite3_column_text(leave,7);
  const char *holiday=(const char*)sqlite3_column_text(leave,8);
  int is_holiday=holiday&&*holiday;
  const char *mode=(const char*)sqlite3_column_text(leave,9);
  const char *qualifier=(mode&&!strcmp(mode,"vacation_only"))?"":((schedule&&!strcmp(schedule,"scheduled"))?"":" - no shift scheduled");
  if(!leave_rows++)sqlite3_str_appendall(txt,"\nApproved time off (not attendance hours)\n");
  char display_id[65];employee_number(db,eid,display_id,sizeof display_id);sqlite3_str_appendf(txt,"  %s (Employee #%s, RFID: %s): %s, %s %s%s%s\n",n,display_id,*card?card:"No active card",d,is_holiday?"stat holiday, not vacation:":"approved",is_holiday?(holiday?holiday:""):kind,qualifier,had_punches?" - CHECK: punches also recorded":"");
  payroll_csv_value(csv,n);sqlite3_str_appendchar(csv,1,',');payroll_csv_value(csv,card);
  sqlite3_str_appendchar(csv,1,',');payroll_csv_value(csv,d);
  sqlite3_str_appendall(csv,",,,0,,");
  char label[220];snprintf(label,sizeof label,"%s%s%s%s",is_holiday?"Stat holiday (not vacation): ":"Approved ",is_holiday?holiday:kind,qualifier,had_punches?" - CHECK: punches recorded":"");payroll_csv_value(csv,label);sqlite3_str_appendall(csv,",0.00");payroll_csv_employee(csv,db,eid);
 }
 sqlite3_finalize(leave);
 if(long_shifts)sqlite3_str_appendf(txt,"\nCHECK: %d long shift%s (over 20 h) for manager review.\n",long_shifts,long_shifts==1?"":"s");
 if(invalid_intervals)sqlite3_str_appendf(txt,"\nCHECK: %d interval%s with invalid duration.\n",invalid_intervals,invalid_intervals==1?"":"s");
 sqlite3_str_appendall(txt,"\nHours are the sum of recorded IN to OUT intervals. Time between an OUT and the next IN is not counted as attendance. No break classification or automatic deduction is applied. Review missing punches and corrections in Timeclock before processing payroll.\n");
 /* Annual usage is saved in the same immutable CSV as shifts and leave. */
 sqlite3_stmt *annual=NULL;
 const char *annual_sql="WITH relevant(id) AS (SELECT id FROM employees WHERE active=1 AND date(created_at,'localtime')<=?2 UNION SELECT employee_id FROM effective_punches WHERE date(at,'localtime') BETWEEN ?1 AND ?2 UNION SELECT employee_id FROM timeoff_requests WHERE starts<=?2 AND ends>=?1) SELECT e.id,e.name FROM relevant r JOIN employees e ON e.id=r.id ORDER BY e.name,e.id";
 if(payroll_report_prepare(db,annual_sql,&annual,cached)!=SQLITE_OK){sqlite3_str_finish(txt);sqlite3_str_finish(csv);return 0;}
 sqlite3_bind_text(annual,1,begin,-1,SQLITE_TRANSIENT);sqlite3_bind_text(annual,2,end,-1,SQLITE_TRANSIENT);
 int annual_rc;while((annual_rc=sqlite3_step(annual))==SQLITE_ROW){
  int employee=sqlite3_column_int(annual,0);const char *name=(const char*)sqlite3_column_text(annual,1);
  for(int year=atoi(begin);year<=atoi(end);year++){
   int used=0,specified=0;double allowed=0;
   if(!vacation_year_usage(db,employee,year,&used,&allowed,&specified)){sqlite3_finalize(annual);sqlite3_str_finish(txt);sqlite3_str_finish(csv);return 0;}
   if(!specified&&!used)continue;
   char label[160],limit[32];if(specified)snprintf(limit,sizeof limit,"%g",allowed);else snprintf(limit,sizeof limit,"unspecified");
   snprintf(label,sizeof label,"Annual vacation %d: %d of %s days used",year,used,limit);
   char display_id[65];employee_number(db,employee,display_id,sizeof display_id);sqlite3_str_appendf(txt,"\n%s (Employee #%s): %s (approved days, including future bookings; informational only)\n",name?name:"Unknown",display_id,label);
   payroll_csv_value(csv,name);sqlite3_str_appendall(csv,",,,,,0,,");payroll_csv_value(csv,label);sqlite3_str_appendall(csv,",0.00");payroll_csv_employee(csv,db,employee);
  }
 }
 sqlite3_finalize(annual);
 if(annual_rc!=SQLITE_DONE){sqlite3_str_finish(txt);sqlite3_str_finish(csv);return 0;}
 *body=sqlite3_str_finish(txt);*csv_out=sqlite3_str_finish(csv);
 if(!*body||!*csv_out){sqlite3_free(*body);sqlite3_free(*csv_out);*body=NULL;*csv_out=NULL;return 0;}
 return 1;
}

static int payroll_build_timesheet(sqlite3 *db,const char *begin,const char *end,char **body,char **csv_out){
 return payroll_build_timesheet_source(db,begin,end,body,csv_out,0,0);
}

static int __attribute__((unused)) payroll_build_timesheet_preview(sqlite3 *db,const char *begin,const char *end,char **body,char **csv_out){return payroll_build_timesheet_source(db,begin,end,body,csv_out,0,1);}
#endif
