#include "payroll_dates.h"
#include "payroll_timesheet.h"
/* Automatic payroll report: due on payday minus lead_days, exactly once per due date.
   Report contents use an approved, closed pay period, not dispatch-date boundaries.
   No wages or payroll liability are calculated. */
static void lite_payroll_queue_error(sqlite3 *db,int period_id,const char *reason){
 sqlite3_stmt *q=NULL;
 if(sqlite3_prepare_v2(db,"INSERT INTO lite_payroll_queue_failures(period_id,reason,failed_at) VALUES(?1,?2,strftime('%Y-%m-%dT%H:%M:%fZ','now')) ON CONFLICT(period_id) DO UPDATE SET reason=excluded.reason,failed_at=excluded.failed_at",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,period_id);sqlite3_bind_text(q,2,reason,-1,SQLITE_TRANSIENT);sqlite3_step(q);}sqlite3_finalize(q);
 fprintf(stderr,"payroll: period %d report not queued: %s\n",period_id,reason);
}
static void payroll_enqueue_due(sqlite3 *db){
 time_t now=time(NULL);struct tm today;localtime_r(&now,&today);char date[11];strftime(date,sizeof date,"%Y-%m-%d",&today);

 sqlite3_stmt*q=NULL;char cadence[20]="",anchor[20]="",recipient[260]="",send_time[8]="00:00";int lead=0,enabled=0;
 if(sqlite3_prepare_v2(db,"SELECT pc.cycle,coalesce(ps.anchor_payday,''),coalesce(ps.lead_days,3),coalesce(ps.recipient,''),coalesce(ps.enabled,0),coalesce(ps.send_time,'00:00') FROM period_cycle_settings pc LEFT JOIN payroll_settings ps ON ps.id=pc.id WHERE pc.id=1",-1,&q,NULL)!=SQLITE_OK)return;
 if(sqlite3_step(q)==SQLITE_ROW){const char*c=(const char*)sqlite3_column_text(q,0),*a=(const char*)sqlite3_column_text(q,1),*r=(const char*)sqlite3_column_text(q,3);
  snprintf(cadence,sizeof cadence,"%s",c?c:"");snprintf(anchor,sizeof anchor,"%s",a?a:"");snprintf(recipient,sizeof recipient,"%s",r?r:"");lead=sqlite3_column_int(q,2);enabled=sqlite3_column_int(q,4);snprintf(send_time,sizeof send_time,"%s",sqlite3_column_text(q,5));}
 sqlite3_finalize(q);
 if(!safe_address(recipient)||lead<0||lead>14){return;}
 time_t today_t=payroll_day(date);
 int due=enabled&&today_t!=(time_t)-1;

 /* Every eligible approved period is queued, oldest first. Each has its own
    unique event ID, so different pay and reporting cycles never lose periods. */
 char paydate[11];struct tm tmp;
 sqlite3_stmt*periods=NULL;
 if(sqlite3_prepare_v2(db,"SELECT p.id,p.starts,p.ends FROM pay_periods p WHERE p.status='closed' AND (p.ends<?1 OR EXISTS(SELECT 1 FROM payroll_manual_requests m WHERE m.period_id=p.id)) AND (NOT EXISTS(SELECT 1 FROM email_queue e WHERE e.event_id='payroll-period:'||p.id) OR (EXISTS(SELECT 1 FROM payroll_manual_requests m WHERE m.period_id=p.id) AND EXISTS(SELECT 1 FROM lite_period_snapshots s WHERE s.period_id=p.id AND s.revision>1 AND NOT EXISTS(SELECT 1 FROM email_queue e WHERE e.event_id='payroll-period:'||p.id||':revision:'||s.revision)) AND NOT EXISTS(SELECT 1 FROM email_queue e WHERE e.event_id='payroll-period:'||p.id AND e.status='pending'))) AND ((?2=1 AND p.ends>=date(?1,'-45 days') AND p.ends>=coalesce((SELECT nullif(enabled_since,'') FROM payroll_settings WHERE id=1),'9999-12-31')) OR EXISTS(SELECT 1 FROM payroll_manual_requests m WHERE m.period_id=p.id)) ORDER BY p.ends,p.id",-1,&periods,NULL)!=SQLITE_OK)return;
 sqlite3_bind_text(periods,1,date,-1,SQLITE_TRANSIENT);sqlite3_bind_int(periods,2,due);
 int all_ok=1;
 while(sqlite3_step(periods)==SQLITE_ROW){
 int period_id=sqlite3_column_int(periods,0);
 /* Match this closed period with the first payday strictly after it ends.
    Dispatch is due lead_days BEFORE that payday, never merely N days after
    the period ended. Closure remains mandatory. */
 time_t end_day=payroll_day((const char*)sqlite3_column_text(periods,2));
 time_t next_payday=(time_t)-1;
 for(int offset=1;offset<=70&&end_day!=(time_t)-1;offset++){
  time_t candidate=payroll_shift(end_day,offset);
  if(payroll_is_payday(cadence,anchor,candidate)){next_payday=candidate;break;}
 }
 sqlite3_stmt *manualq=NULL;int manual=0;
 if(sqlite3_prepare_v2(db,"SELECT 1 FROM payroll_manual_requests WHERE period_id=?1",-1,&manualq,NULL)==SQLITE_OK){sqlite3_bind_int(manualq,1,period_id);manual=sqlite3_step(manualq)==SQLITE_ROW;}
 sqlite3_finalize(manualq);
 if(!manual){
  if(next_payday==(time_t)-1)continue;
  time_t due_day=payroll_shift(next_payday,-lead);struct tm due_local;char due_date[11];localtime_r(&due_day,&due_local);strftime(due_date,sizeof due_date,"%Y-%m-%d",&due_local);
  if(!send_schedule_due(&today,due_date,send_time))continue;
 }

 /* Avoid hot-loop retries and duplicate log messages; retry once per five minutes. */
 sqlite3_stmt *cooldown=NULL;int cooling=0;
 if(sqlite3_prepare_v2(db,"SELECT failed_at>strftime('%Y-%m-%dT%H:%M:%fZ','now','-5 minutes') FROM lite_payroll_queue_failures WHERE period_id=?1",-1,&cooldown,NULL)==SQLITE_OK){sqlite3_bind_int(cooldown,1,period_id);if(sqlite3_step(cooldown)==SQLITE_ROW)cooling=sqlite3_column_int(cooldown,0);}
 sqlite3_finalize(cooldown);if(cooling)continue;
 char begin[11]="",end[11]="";
 snprintf(begin,sizeof begin,"%s",sqlite3_column_text(periods,1));
 snprintf(end,sizeof end,"%s",sqlite3_column_text(periods,2));
 time_t period_payday=next_payday==(time_t)-1?payroll_shift(today_t,lead):next_payday;
 localtime_r(&period_payday,&tmp);strftime(paydate,sizeof paydate,"%Y-%m-%d",&tmp);
 char *body=NULL,*csv=NULL;int saved_revision=0;
 if(!payroll_build_timesheet(db,begin,end,&body,&csv)){lite_payroll_queue_error(db,period_id,"timesheet generation failed");all_ok=0;continue;}
 /* Existing revisions are authoritative. Never reject delivery because live
    employee data changed after export. */
 sqlite3_stmt *saved=NULL;
 if(sqlite3_prepare_v2(db,"SELECT csv,revision FROM lite_period_snapshots WHERE period_id=?1 ORDER BY revision DESC LIMIT 1",-1,&saved,NULL)==SQLITE_OK){
  sqlite3_bind_int(saved,1,period_id);
  if(sqlite3_step(saved)==SQLITE_ROW){saved_revision=sqlite3_column_int(saved,1);const char *v=(const char*)sqlite3_column_text(saved,0);char *copy=sqlite3_mprintf("%s",v?v:"");if(copy){sqlite3_free(csv);csv=copy;}}
 }
 sqlite3_finalize(saved);
 if(saved_revision){sqlite3_free(body);body=sqlite3_mprintf("Saved timesheet revision %d for %s to %s is attached.\nThis is the preserved report, not a recalculation of current employee records.\n",saved_revision,begin,end);if(!body){sqlite3_free(csv);lite_payroll_queue_error(db,period_id,"report allocation failed");continue;}}
 char subject[600],event[100];snprintf(subject,sizeof subject,"Timeclock attendance: %s to %s",begin,end);snprintf(event,sizeof event,saved_revision>1?"payroll-period:%d:revision:%d":"payroll-period:%d",period_id,saved_revision);
 sqlite3_stmt *tpl=NULL;
 if(sqlite3_prepare_v2(db,"SELECT subject,body,enabled FROM email_templates WHERE id='attendance_report'",-1,&tpl,NULL)==SQLITE_OK){
  if(sqlite3_step(tpl)==SQLITE_ROW){
   if(!sqlite3_column_int(tpl,2)){sqlite3_finalize(tpl);sqlite3_free(body);sqlite3_free(csv);lite_payroll_queue_error(db,period_id,"template disabled");all_ok=0;continue;}
   const char *raw_s=(const char*)sqlite3_column_text(tpl,0),*raw_b=(const char*)sqlite3_column_text(tpl,1);
   const struct email_field fields[]={
    {"{{payday}}",paydate},{"{{period_start}}",begin},{"{{period_end}}",end},{"{{attendance_summary}}",body}
   };
   char *newsubject=email_render(raw_s,fields,4),*newbody=email_render(raw_b,fields,4);
   if(!newsubject||!newbody){free(newsubject);free(newbody);sqlite3_finalize(tpl);sqlite3_free(body);sqlite3_free(csv);lite_payroll_queue_error(db,period_id,"template rendering failed");all_ok=0;continue;}
   /* Period dates are mandatory in the subject, even with a custom template. */
   if(strstr(newsubject,begin)&&strstr(newsubject,end))snprintf(subject,sizeof subject,"%s",newsubject);
   else {snprintf(subject,sizeof subject,"%.490s (%s to %s)",newsubject,begin,end);}
   free(newsubject);sqlite3_free(body);body=sqlite3_mprintf("%s",newbody);free(newbody);if(!body){sqlite3_finalize(tpl);sqlite3_free(csv);lite_payroll_queue_error(db,period_id,"email body generation failed");all_ok=0;continue;}
  }
 }sqlite3_finalize(tpl);
 if(sqlite3_exec(db,"SAVEPOINT lite_payroll_pair",NULL,NULL,NULL)!=SQLITE_OK){all_ok=0;sqlite3_free(body);sqlite3_free(csv);lite_payroll_queue_error(db,period_id,"database transaction failed");continue;}
 int pair_ok=1;
 if(sqlite3_prepare_v2(db,"INSERT OR IGNORE INTO email_queue(recipient,subject,body,status,next_attempt,event_id,csv_attachment) VALUES(?1,?2,?3,'pending',strftime('%Y-%m-%dT%H:%M:%fZ','now'),?4,?5)",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,recipient,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,subject,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,body,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,4,event,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,5,csv,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)!=SQLITE_DONE)pair_ok=0;}else pair_ok=0;sqlite3_finalize(q);
 if(pair_ok){
 /* Version 1 is an immutable copy of the exact source used for the first
    payroll workbook. Never overwrite it when attendance is corrected later. */
 sqlite3_stmt *snap=NULL;
 if(sqlite3_prepare_v2(db,"INSERT OR IGNORE INTO lite_period_snapshots(period_id,revision,starts,ends,csv) VALUES(?1,1,?2,?3,?4)",-1,&snap,NULL)==SQLITE_OK){
  sqlite3_bind_int(snap,1,period_id);
  sqlite3_bind_text(snap,2,begin,-1,SQLITE_TRANSIENT);
  sqlite3_bind_text(snap,3,end,-1,SQLITE_TRANSIENT);
  sqlite3_bind_text(snap,4,csv,-1,SQLITE_TRANSIENT);
  if(sqlite3_step(snap)!=SQLITE_DONE)pair_ok=0;
 }else pair_ok=0;
 sqlite3_finalize(snap);
 }
 if(pair_ok){if(sqlite3_exec(db,"RELEASE SAVEPOINT lite_payroll_pair",NULL,NULL,NULL)!=SQLITE_OK)all_ok=0;else {sqlite3_stmt *clear=NULL;if(sqlite3_prepare_v2(db,"DELETE FROM lite_payroll_queue_failures WHERE period_id=?1",-1,&clear,NULL)==SQLITE_OK){sqlite3_bind_int(clear,1,period_id);sqlite3_step(clear);}sqlite3_finalize(clear);}}
 else{sqlite3_exec(db,"ROLLBACK TO SAVEPOINT lite_payroll_pair",NULL,NULL,NULL);sqlite3_exec(db,"RELEASE SAVEPOINT lite_payroll_pair",NULL,NULL,NULL);all_ok=0;}
 sqlite3_free(body);sqlite3_free(csv);
 if(!pair_ok)lite_payroll_queue_error(db,period_id,"queue and snapshot transaction failed");
 }
 sqlite3_finalize(periods);
 sqlite3_exec(db,"DELETE FROM payroll_manual_requests WHERE EXISTS (SELECT 1 FROM email_queue e WHERE e.event_id=(SELECT CASE WHEN max(revision)>1 THEN 'payroll-period:'||payroll_manual_requests.period_id||':revision:'||max(revision) ELSE 'payroll-period:'||payroll_manual_requests.period_id END FROM lite_period_snapshots WHERE period_id=payroll_manual_requests.period_id))",NULL,NULL,NULL);
 /* Keep checking throughout the due day: a manager may close a period later today. */
 (void)all_ok;
}
