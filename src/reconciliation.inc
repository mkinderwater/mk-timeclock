#include "payroll_timesheet.h"
/* v0.9.43: audited attendance corrections and closed periods. Original swipes never modified. */
static int rec_schema(void){return sql(
 "CREATE TABLE IF NOT EXISTS punch_corrections(id INTEGER PRIMARY KEY,employee_id INTEGER NOT NULL REFERENCES employees(id),event_id TEXT UNIQUE,manual_id TEXT UNIQUE,action TEXT NOT NULL CHECK(action IN ('add','change','void')),effective_at TEXT NOT NULL,kind TEXT NOT NULL CHECK(kind IN ('IN','OUT')),reason TEXT NOT NULL CHECK(length(trim(reason))>=5),manager_id INTEGER NOT NULL REFERENCES employees(id),created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
 "CREATE TABLE IF NOT EXISTS punch_correction_history(id INTEGER PRIMARY KEY,correction_id INTEGER,employee_id INTEGER,event_id TEXT,manual_id TEXT,action TEXT,effective_at TEXT,kind TEXT,reason TEXT,manager_id INTEGER,recorded_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
 "CREATE TRIGGER IF NOT EXISTS correction_history_insert AFTER INSERT ON punch_corrections BEGIN INSERT INTO punch_correction_history(correction_id,employee_id,event_id,manual_id,action,effective_at,kind,reason,manager_id) VALUES(new.id,new.employee_id,new.event_id,new.manual_id,new.action,new.effective_at,new.kind,new.reason,new.manager_id); END;"
 "CREATE TRIGGER IF NOT EXISTS correction_history_update AFTER UPDATE ON punch_corrections BEGIN INSERT INTO punch_correction_history(correction_id,employee_id,event_id,manual_id,action,effective_at,kind,reason,manager_id) VALUES(new.id,new.employee_id,new.event_id,new.manual_id,new.action,new.effective_at,new.kind,new.reason,new.manager_id); END;"
 "CREATE TABLE IF NOT EXISTS attendance_absences(id INTEGER PRIMARY KEY, employee_id INTEGER NOT NULL REFERENCES employees(id), day TEXT NOT NULL, reason TEXT NOT NULL CHECK(reason IN ('Sick','Medical appointment','Personal day','Family emergency','Bereavement','Unpaid leave','Unexcused absence','Other')), note TEXT NOT NULL DEFAULT '', starts TEXT NOT NULL DEFAULT '', ends TEXT NOT NULL DEFAULT '', manager_id INTEGER NOT NULL REFERENCES employees(id), recorded_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')), cancelled_at TEXT, cancelled_by INTEGER REFERENCES employees(id), CHECK(starts='' OR (length(starts)=5 AND length(ends)=5 AND starts<ends)));"
 "CREATE TABLE IF NOT EXISTS attendance_absence_audit(id INTEGER PRIMARY KEY, absence_id INTEGER NOT NULL REFERENCES attendance_absences(id), manager_id INTEGER NOT NULL REFERENCES employees(id), action TEXT NOT NULL, reason TEXT NOT NULL, recorded_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
 "CREATE INDEX IF NOT EXISTS attendance_absences_employee_day ON attendance_absences(employee_id,day) WHERE cancelled_at IS NULL;"
 "CREATE TABLE IF NOT EXISTS period_cycle_settings(id INTEGER PRIMARY KEY CHECK(id=1),cycle TEXT NOT NULL DEFAULT 'semi_monthly' CHECK(cycle IN ('semi_monthly','biweekly','weekly','monthly')),anchor TEXT NOT NULL DEFAULT '2026-01-01',updated_by INTEGER,updated_at TEXT);"
 "INSERT OR IGNORE INTO period_cycle_settings(id) VALUES(1);"
 "CREATE TABLE IF NOT EXISTS pay_periods(id INTEGER PRIMARY KEY,starts TEXT NOT NULL,ends TEXT NOT NULL,status TEXT NOT NULL DEFAULT 'open' CHECK(status IN ('open','closed')),closed_by INTEGER REFERENCES employees(id),closed_at TEXT,reason TEXT NOT NULL DEFAULT '',UNIQUE(starts,ends),CHECK(starts<=ends));"
 "CREATE TABLE IF NOT EXISTS period_signoffs(period_id INTEGER NOT NULL REFERENCES pay_periods(id),employee_id INTEGER NOT NULL REFERENCES employees(id),role TEXT NOT NULL CHECK(role IN ('employee','manager')),signed_by INTEGER NOT NULL REFERENCES employees(id),signed_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),punch_count INTEGER NOT NULL DEFAULT 0,approved_seconds INTEGER NOT NULL DEFAULT 0,unconfirmed INTEGER NOT NULL DEFAULT 0,reason TEXT NOT NULL DEFAULT '',leave_fingerprint TEXT NOT NULL DEFAULT '',PRIMARY KEY(period_id,employee_id,role));"
 "CREATE TABLE IF NOT EXISTS period_signoff_history(id INTEGER PRIMARY KEY,period_id INTEGER NOT NULL,employee_id INTEGER NOT NULL,role TEXT NOT NULL,signed_by INTEGER NOT NULL,signed_at TEXT NOT NULL,punch_count INTEGER NOT NULL,approved_seconds INTEGER NOT NULL,leave_fingerprint TEXT NOT NULL,unconfirmed INTEGER NOT NULL,reason TEXT NOT NULL,superseded_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
 "CREATE TRIGGER IF NOT EXISTS period_signoff_removed BEFORE DELETE ON period_signoffs BEGIN INSERT INTO period_signoff_history(period_id,employee_id,role,signed_by,signed_at,punch_count,approved_seconds,leave_fingerprint,unconfirmed,reason) VALUES(old.period_id,old.employee_id,old.role,old.signed_by,old.signed_at,old.punch_count,old.approved_seconds,old.leave_fingerprint,old.unconfirmed,old.reason); END;"
 "CREATE TABLE IF NOT EXISTS period_approval_audit(id INTEGER PRIMARY KEY,period_id INTEGER NOT NULL,employee_id INTEGER NOT NULL,manager_id INTEGER NOT NULL,action TEXT NOT NULL,reason TEXT NOT NULL,approved_seconds INTEGER NOT NULL,recorded_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
 "DROP VIEW IF EXISTS effective_punches;"
 "CREATE VIEW effective_punches AS WITH RECURSIVE "
 "raw AS (SELECT s.employee_id,s.event_id AS id,coalesce(c.effective_at,s.swiped_at) AS at,coalesce(c.kind,a.kind,'IN') AS kind,CASE WHEN c.id IS NOT NULL AND c.action<>'void' THEN 1 ELSE 0 END AS anchored,CASE WHEN c.action='void' THEN 1 ELSE 0 END AS voided,0 AS manual FROM swipes s JOIN attendance_events a ON a.event_id=s.event_id LEFT JOIN punch_corrections c ON c.event_id=s.event_id WHERE s.employee_id IS NOT NULL AND a.exception NOT IN ('duplicate_tap','out_of_order') UNION ALL SELECT c.employee_id,c.manual_id,c.effective_at,c.kind,1,0,1 FROM punch_corrections c WHERE c.manual_id IS NOT NULL AND c.action<>'void'),"
 "ord AS (SELECT *,row_number() OVER (PARTITION BY employee_id ORDER BY at,id) AS rn FROM raw),"
 "calc(employee_id,rn,id,at,kind,manual,voided,last_kind,last_at,rebased) AS ("
 "SELECT employee_id,rn,id,at,kind,manual,voided,CASE WHEN voided=1 THEN 'OUT' ELSE kind END,CASE WHEN voided=1 THEN '' ELSE at END,CASE WHEN anchored=1 OR voided=1 THEN 1 ELSE 0 END FROM ord WHERE rn=1 "
 "UNION ALL SELECT n.employee_id,n.rn,n.id,n.at,CASE WHEN n.voided=1 THEN '' WHEN n.anchored=1 THEN n.kind WHEN c.rebased=1 THEN CASE WHEN c.last_at<>'' AND (strftime('%s',n.at)-strftime('%s',c.last_at))>72000 THEN 'IN' WHEN c.last_kind='IN' THEN 'OUT' ELSE 'IN' END ELSE n.kind END,n.manual,n.voided,CASE WHEN n.voided=1 THEN c.last_kind WHEN n.anchored=1 THEN n.kind WHEN c.rebased=1 THEN CASE WHEN c.last_at<>'' AND (strftime('%s',n.at)-strftime('%s',c.last_at))>72000 THEN 'IN' WHEN c.last_kind='IN' THEN 'OUT' ELSE 'IN' END ELSE n.kind END,CASE WHEN n.voided=1 THEN c.last_at ELSE n.at END,CASE WHEN n.anchored=1 OR n.voided=1 OR c.rebased=1 THEN 1 ELSE 0 END FROM calc c JOIN ord n ON n.employee_id=c.employee_id AND n.rn=c.rn+1)"
 "SELECT employee_id,id,at,kind,manual FROM calc WHERE voided=0;"

 "DROP VIEW IF EXISTS effective_shift_pairs;"
 "CREATE VIEW effective_shift_pairs AS WITH ordered AS (SELECT employee_id,id,at,kind,manual,lead(at) OVER (PARTITION BY employee_id ORDER BY at,id) AS next_at,lead(kind) OVER (PARTITION BY employee_id ORDER BY at,id) AS next_kind,lead(id) OVER (PARTITION BY employee_id ORDER BY at,id) AS next_id,lag(kind) OVER (PARTITION BY employee_id ORDER BY at,id) AS prior_kind,lag(at) OVER (PARTITION BY employee_id ORDER BY at,id) AS prior_at FROM effective_punches) SELECT employee_id,id AS in_id,at AS in_at,CASE WHEN kind='IN' AND next_kind='OUT' AND (strftime('%s',next_at)-strftime('%s',at)) BETWEEN 0 AND 72000 THEN next_at ELSE NULL END AS out_at,CASE WHEN kind='IN' AND next_kind='OUT' AND (strftime('%s',next_at)-strftime('%s',at)) BETWEEN 0 AND 72000 THEN next_id ELSE NULL END AS out_id,CASE WHEN kind='IN' AND next_kind='OUT' AND (strftime('%s',next_at)-strftime('%s',at)) BETWEEN 0 AND 72000 THEN strftime('%s',next_at)-strftime('%s',at) ELSE 0 END AS seconds,CASE WHEN kind='OUT' THEN 'missing_in' WHEN next_kind='OUT' AND (strftime('%s',next_at)-strftime('%s',at)) BETWEEN 0 AND 72000 THEN 'complete' ELSE 'missing_out' END AS state,manual FROM ordered WHERE kind='IN' OR (kind='OUT' AND (coalesce(prior_kind,'')<>'IN' OR (strftime('%s',at)-strftime('%s',prior_at)) NOT BETWEEN 0 AND 72000));"
 "CREATE TRIGGER IF NOT EXISTS period_signoff_revision BEFORE UPDATE ON period_signoffs BEGIN INSERT INTO period_signoff_history(period_id,employee_id,role,signed_by,signed_at,punch_count,approved_seconds,leave_fingerprint,unconfirmed,reason) VALUES(old.period_id,old.employee_id,old.role,old.signed_by,old.signed_at,old.punch_count,old.approved_seconds,old.leave_fingerprint,old.unconfirmed,old.reason); END;"
 );}
/* Stable leave-context fingerprint for an employee/period. Includes every recorded
   absence and approved request, even when an employee has no punches. */
static int rec_leave_fingerprint(int employee,const char*starts,const char*ends,char out[17]){
 static const char *query=
 "SELECT kind,day,details FROM ("
 "SELECT 'absence' kind,day,reason||'|'||note||'|'||starts||'|'||ends details FROM attendance_absences WHERE employee_id=?1 AND cancelled_at IS NULL AND day BETWEEN ?2 AND ?3 "
 "UNION ALL SELECT 'holiday',day,name FROM statutory_holidays WHERE day BETWEEN ?2 AND ?3 "
 "UNION ALL SELECT 'timeoff',d.day,t.kind||'|'||t.starts||'|'||t.ends FROM timeoff_requests t "
 "JOIN (WITH RECURSIVE dates(day) AS (SELECT ?2 UNION ALL SELECT date(day,'+1 day') FROM dates WHERE day<?3) SELECT day FROM dates) d "
 "ON d.day BETWEEN t.starts AND t.ends WHERE t.employee_id=?1 AND t.status='approved' AND t.cancelled_at IS NULL AND CAST(strftime('%w',d.day) AS INTEGER) BETWEEN 1 AND 5) "
 "ORDER BY kind,day,details";
 sqlite3_stmt*q=NULL;uint64_t h=UINT64_C(14695981039346656037);int ok=1;
 if(sqlite3_prepare_v2(db,query,-1,&q,NULL)!=SQLITE_OK)return 0;
 sqlite3_bind_int(q,1,employee);sqlite3_bind_text(q,2,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,ends,-1,SQLITE_TRANSIENT);
 int rc;while((rc=sqlite3_step(q))==SQLITE_ROW){for(int col=0;col<3;col++){
  const unsigned char*t=sqlite3_column_text(q,col);if(t)for(;*t;t++){h^=*t;h*=UINT64_C(1099511628211);}
  h^=0xff;h*=UINT64_C(1099511628211);
 }}
 if(rc!=SQLITE_DONE)ok=0;
 sqlite3_finalize(q);
 if(ok)snprintf(out,17,"%016llx",(unsigned long long)h);
 return ok;
}
static void rec_error(int fd,int status,const char *message){char o[260];snprintf(o,sizeof o,"{\"error\":\"%s\"}",message);send_response(fd,status,"application/json",o);}
static int rec_date(const char*s){if(strlen(s)!=10)return 0;for(int i=0;i<10;i++)if((i==4||i==7)?s[i]!='-':!isdigit((unsigned char)s[i]))return 0;sqlite3_stmt*q=NULL;int ok=0;if(sqlite3_prepare_v2(db,"SELECT date(?1)=?1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,s,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW)ok=sqlite3_column_int(q,0);}sqlite3_finalize(q);return ok;}
static int rec_stamp(const char*s){if(strlen(s)!=20||s[10]!='T'||s[19]!='Z')return 0;char date[11];memcpy(date,s,10);date[10]=0;if(!rec_date(date))return 0;time_t t;struct tm tm;return attendance_stamp(s,&t,&tm);}
static int rec_closed(const char*stamp){sqlite3_stmt*q=NULL;int closed=1;if(sqlite3_prepare_v2(db,"SELECT EXISTS(SELECT 1 FROM pay_periods WHERE status='closed' AND date(?1,'localtime') BETWEEN starts AND ends)",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,stamp,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW)closed=sqlite3_column_int(q,0);}sqlite3_finalize(q);return closed;}
static void rec_events(int fd){sqlite3_stmt*q=NULL;const char*query="SELECT e.id,e.name,p.id,p.at,p.kind,p.manual,coalesce(n.note,''),coalesce(a.late_minutes,0),EXISTS(SELECT 1 FROM punch_corrections c WHERE c.event_id=p.id AND c.action='change') FROM effective_punches p JOIN employees e ON e.id=p.employee_id LEFT JOIN swipe_notes n ON n.event_id=p.id LEFT JOIN attendance_events a ON a.event_id=p.id ORDER BY p.at DESC,p.id DESC LIMIT 150";if(sqlite3_prepare_v2(db,query,-1,&q,NULL)!=SQLITE_OK){rec_error(fd,500,"The clock could not complete this. Try again.");return;}sqlite3_str*s=sqlite3_str_new(db);sqlite3_str_appendall(s,"{\"punches\":[");int n=0;while(sqlite3_step(q)==SQLITE_ROW){if(n++)sqlite3_str_appendchar(s,1,',');sqlite3_str_appendf(s,"{\"employee_id\":%d,\"name\":",sqlite3_column_int(q,0));str_json(s,op_text(q,1));sqlite3_str_appendall(s,",\"id\":");str_json(s,op_text(q,2));sqlite3_str_appendall(s,",\"at\":");str_json(s,op_text(q,3));sqlite3_str_appendall(s,",\"kind\":");str_json(s,op_text(q,4));sqlite3_str_appendf(s,",\"manual\":%s,\"note\":",sqlite3_column_int(q,5)?"true":"false");str_json(s,op_text(q,6));sqlite3_str_appendf(s,",\"late\":%d,\"corrected\":%s}",sqlite3_column_int(q,7),sqlite3_column_int(q,8)?"true":"false");}sqlite3_finalize(q);
 /* Any unmatched swipe, including one older than the recovery lookback, is a payroll issue. */
 sqlite3_stmt *orphan=NULL;int missing=0;
 if(sqlite3_prepare_v2(db,"SELECT count(*) FROM effective_punches x WHERE NOT EXISTS(SELECT 1 FROM pay_periods p WHERE date(x.at,'localtime') BETWEEN p.starts AND p.ends)",-1,&orphan,NULL)==SQLITE_OK&&sqlite3_step(orphan)==SQLITE_ROW)missing=sqlite3_column_int(orphan,0);
 sqlite3_finalize(orphan);
 sqlite3_str_appendf(s,"],\"uncovered_punches\":%d}",missing);send_str(fd,"application/json",s);}
/* Compare full paired-shift state, not merely elapsed seconds. Changes to an
   OUT in the next period can otherwise silently rewrite closed payroll. */
static void rec_apply(int fd,int who,const char*body){
 char action[16]="",id[100]="",kind[8]="",stamp[32]="",why[260]="",employee_txt[20]="";
 if(!form_value(body,"action",action,sizeof action)||!form_value(body,"event_id",id,sizeof id)||!form_value(body,"kind",kind,sizeof kind)||!form_value(body,"at",stamp,sizeof stamp)||!form_value_multiline(body,"reason",why,sizeof why)||!form_value(body,"employee_id",employee_txt,sizeof employee_txt)){rec_error(fd,400,"Fill in all required fields.");return;}
 if((strcmp(action,"add")&&strcmp(action,"change")&&strcmp(action,"void"))||(strcmp(kind,"IN")&&strcmp(kind,"OUT"))){rec_error(fd,400,"Choose IN or OUT.");return;}
 if(strlen(why)<5||strlen(why)>240){rec_error(fd,400,"Enter a reason of 5 to 240 characters.");return;}
 if(!rec_stamp(stamp)){rec_error(fd,400,"Enter a valid date and time.");return;}
 char *end=NULL;long employee=strtol(employee_txt,&end,10);if(!*employee_txt||*end||employee<=0||employee>2147483647){rec_error(fd,400,"Employee not found. Reload the page.");return;}
 if(strcmp(action,"add")&&(!valid_id(id,80))){rec_error(fd,400,"Invalid event ID");return;}
 if(!sql("BEGIN IMMEDIATE")){rec_error(fd,503,"The clock is busy. Try again.");return;}
 sqlite3_stmt*q=NULL;int ok=1,found=0;char old_at[32]="",raw_at[32]="";int actual_employee=0,manual_target=0;
 if(sqlite3_prepare_v2(db,"SELECT id FROM employees WHERE id=?1 AND active=1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,(int)employee);found=sqlite3_step(q)==SQLITE_ROW;}sqlite3_finalize(q);q=NULL;
 if(!found){sql("ROLLBACK");rec_error(fd,404,"Employee not found. Reload the page.");return;}
 if(strcmp(action,"add")){
  if(sqlite3_prepare_v2(db,"SELECT employee_id,swiped_at FROM swipes WHERE event_id=?1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,id,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW){actual_employee=sqlite3_column_int(q,0);snprintf(old_at,sizeof old_at,"%s",op_text(q,1));snprintf(raw_at,sizeof raw_at,"%s",op_text(q,1));}}sqlite3_finalize(q);q=NULL;
  if(!old_at[0]){
   if(sqlite3_prepare_v2(db,"SELECT employee_id,effective_at FROM punch_corrections WHERE manual_id=?1 AND action<>'void'",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,id,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW){manual_target=1;actual_employee=sqlite3_column_int(q,0);snprintf(old_at,sizeof old_at,"%s",op_text(q,1));}}sqlite3_finalize(q);q=NULL;
  }
  if(!old_at[0]||actual_employee!=(int)employee){sql("ROLLBACK");rec_error(fd,404,"Original or manual punch not found for employee");return;}
  /* An edited event can already have moved to another calendar date. */
  if(sqlite3_prepare_v2(db,"SELECT effective_at FROM punch_corrections WHERE event_id=?1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,id,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW)snprintf(old_at,sizeof old_at,"%s",op_text(q,0));}sqlite3_finalize(q);q=NULL;
 }
 char new_id[65]="";if(!strcmp(action,"add")&&!random_hex(new_id,16))ok=0;
 const char*stmt=manual_target?"UPDATE punch_corrections SET action=?7,effective_at=?3,kind=?4,reason=?5,manager_id=?6 WHERE manual_id=?2 AND employee_id=?1":!strcmp(action,"add")?"INSERT INTO punch_corrections(employee_id,manual_id,action,effective_at,kind,reason,manager_id) VALUES(?1,?2,'add',?3,?4,?5,?6)":"INSERT INTO punch_corrections(employee_id,event_id,action,effective_at,kind,reason,manager_id) VALUES(?1,?2,?7,?3,?4,?5,?6) ON CONFLICT(event_id) DO UPDATE SET action=excluded.action,effective_at=excluded.effective_at,kind=excluded.kind,reason=excluded.reason,manager_id=excluded.manager_id";
 if(ok)ok=sqlite3_prepare_v2(db,stmt,-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,(int)employee);sqlite3_bind_text(q,2,!strcmp(action,"add")?new_id:id,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,stamp,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,4,kind,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,5,why,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,6,who);if(strcmp(action,"add"))sqlite3_bind_text(q,7,action,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 /* Any changed attendance invalidates prior employee and manager attestations.
    Even if total seconds happen to match, the approved punch set has changed. */
 if(ok){
  sqlite3_stmt*invalidate=NULL;
  const char*invalidation="DELETE FROM period_signoffs WHERE employee_id=?1 AND period_id IN (SELECT id FROM pay_periods WHERE status='open' AND (date(?2,'localtime') BETWEEN starts AND ends OR date(?3,'localtime') BETWEEN starts AND ends OR date(?4,'localtime') BETWEEN starts AND ends))";
  ok=sqlite3_prepare_v2(db,invalidation,-1,&invalidate,NULL)==SQLITE_OK;
  if(ok){sqlite3_bind_int(invalidate,1,(int)employee);sqlite3_bind_text(invalidate,2,stamp,-1,SQLITE_TRANSIENT);sqlite3_bind_text(invalidate,3,old_at,-1,SQLITE_TRANSIENT);sqlite3_bind_text(invalidate,4,raw_at,-1,SQLITE_TRANSIENT);ok=sqlite3_step(invalidate)==SQLITE_DONE;}
  sqlite3_finalize(invalidate);
 }
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");
 /* Notification is best-effort, separate from saving the audit-backed correction. */
 if(ok){
  sqlite3_stmt *notice=NULL;char address[256]="",name[192]="";
  if(sqlite3_prepare_v2(db,"SELECT coalesce(email,''),name FROM employees WHERE id=?1",-1,&notice,NULL)==SQLITE_OK){
   sqlite3_bind_int(notice,1,(int)employee);
   if(sqlite3_step(notice)==SQLITE_ROW){snprintf(address,sizeof address,"%s",op_text(notice,0));snprintf(name,sizeof name,"%s",op_text(notice,1));}
  }sqlite3_finalize(notice);notice=NULL;
  if(strchr(address,'@')&&!strpbrk(address,"\r\n")){
   long long change_id=0;
   if(sqlite3_prepare_v2(db,"SELECT max(id) FROM punch_correction_history WHERE employee_id=?1",-1,&notice,NULL)==SQLITE_OK){
    sqlite3_bind_int(notice,1,(int)employee);if(sqlite3_step(notice)==SQLITE_ROW)change_id=sqlite3_column_int64(notice,0);
   }sqlite3_finalize(notice);notice=NULL;
   if(change_id>0){
    char event[90],subject[170],message[500];
    snprintf(event,sizeof event,"punch-corrected:%lld",change_id);
    snprintf(subject,sizeof subject,"Timeclock: Your recorded hours were corrected");
    snprintf(message,sizeof message,"Hello %s,\n\nYour manager updated a recorded punch. Please reopen your most recent timesheet link and check your hours again.\n\nIf anything is still wrong, contact your manager.\n",name);
    if(sqlite3_prepare_v2(db,"INSERT OR IGNORE INTO email_queue(recipient,subject,body,status,next_attempt,event_id) VALUES(?1,?2,?3,'pending',strftime('%Y-%m-%dT%H:%M:%fZ','now'),?4)",-1,&notice,NULL)==SQLITE_OK){
     sqlite3_bind_text(notice,1,address,-1,SQLITE_TRANSIENT);sqlite3_bind_text(notice,2,subject,-1,SQLITE_TRANSIENT);sqlite3_bind_text(notice,3,message,-1,SQLITE_TRANSIENT);sqlite3_bind_text(notice,4,event,-1,SQLITE_TRANSIENT);sqlite3_step(notice);
    }sqlite3_finalize(notice);
   }
  }
 }
 if(ok)send_response(fd,200,"application/json","{\"status\":\"saved\"}");else rec_error(fd,500,"The clock could not complete this. Try again.");
}
static void rec_effective_export(int fd,const char*path){
 char from[16]="",to[16]="";
 if(!op_query_param(path,"from",from,sizeof from)||!op_query_param(path,"to",to,sizeof to)||!rec_date(from)||!rec_date(to)||strcmp(from,to)>0){rec_error(fd,400,"Choose valid dates.");return;}
 sqlite3_stmt*q=NULL;const char*query="SELECT e.id,e.name,p.id,p.at,p.kind,CASE WHEN p.manual THEN 'manual' ELSE 'rfid' END FROM effective_punches p JOIN employees e ON e.id=p.employee_id WHERE date(p.at,'localtime') BETWEEN ?1 AND ?2 ORDER BY e.name,p.at,p.id LIMIT 100001";
 if(sqlite3_prepare_v2(db,query,-1,&q,NULL)!=SQLITE_OK){rec_error(fd,500,"The clock could not complete this. Try again.");return;}
 sqlite3_bind_text(q,1,from,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,to,-1,SQLITE_TRANSIENT);
 sqlite3_str*out=sqlite3_str_new(db);sqlite3_str_appendall(out,"employee_id,employee,event_id,utc_timestamp,effective_direction,source\r\n");
 int n=0,rc;while((rc=sqlite3_step(q))==SQLITE_ROW){if(++n>100000){rc=SQLITE_TOOBIG;break;}char id[20];snprintf(id,sizeof id,"%d",sqlite3_column_int(q,0));op_csv(out,id);for(int i=1;i<6;i++){sqlite3_str_appendchar(out,1,',');op_csv(out,op_text(q,i));}sqlite3_str_appendall(out,"\r\n");}
 sqlite3_finalize(q);
 if(rc!=SQLITE_DONE){sqlite3_str_finish(out);rec_error(fd,409,"Too many records. Choose a shorter date range.");return;}
 op_export_send(fd,"text/csv; charset=utf-8","corrected_attendance.csv",out);
}
static void rec_presence(int fd){sqlite3_stmt*q=NULL;const char*query="SELECT e.name,p.at,p.id FROM effective_punches p JOIN employees e ON e.id=p.employee_id WHERE e.active=1 AND p.kind='IN' AND p.id=(SELECT p2.id FROM effective_punches p2 WHERE p2.employee_id=p.employee_id ORDER BY p2.at DESC,p2.id DESC LIMIT 1) ORDER BY e.name";if(sqlite3_prepare_v2(db,query,-1,&q,NULL)!=SQLITE_OK){rec_error(fd,500,"The clock could not complete this. Try again.");return;}sqlite3_str*s=sqlite3_str_new(db);sqlite3_str_appendall(s,"{\"people\":[");int n=0;while(sqlite3_step(q)==SQLITE_ROW){if(n++)sqlite3_str_appendchar(s,1,',');sqlite3_str_appendall(s,"{\"name\":");str_json(s,op_text(q,0));sqlite3_str_appendall(s,",\"since\":");str_json(s,op_text(q,1));sqlite3_str_appendall(s,"}");}sqlite3_finalize(q);sqlite3_str_appendf(s,"],\"count\":%d}",n);send_str(fd,"application/json",s);}
static void rec_cycle_get(int fd){
 sqlite3_stmt*q=NULL;sqlite3_str*out=sqlite3_str_new(db);sqlite3_str_appendall(out,"{\"cycle\":");
 if(sqlite3_prepare_v2(db,"SELECT cycle,anchor FROM period_cycle_settings WHERE id=1",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW){str_json(out,op_text(q,0));sqlite3_str_appendall(out,",\"anchor\":");str_json(out,op_text(q,1));}else{sqlite3_str_appendall(out,"\"semi_monthly\",\"anchor\":\"2026-01-01\"");}
 sqlite3_finalize(q);
 sqlite3_str_appendchar(out,1,'}');send_str(fd,"application/json",out);
}
static void rec_cycle_save(int fd,int who,const char*body){
 char cycle[24]="",anchor[16]="";
 
 if(!form_value(body,"cycle",cycle,sizeof cycle)||!form_value(body,"anchor",anchor,sizeof anchor)||!rec_date(anchor)||
 (strcmp(cycle,"semi_monthly")&&strcmp(cycle,"biweekly")&&strcmp(cycle,"weekly")&&strcmp(cycle,"monthly"))){rec_error(fd,400,"Choose a cycle and a valid first day.");return;}
 /* Changing cadence removes only unused future periods. A period that is closed,
    reviewed, exported or has a delivery queued cannot be silently replaced. */
 if(!sql("BEGIN IMMEDIATE")){rec_error(fd,503,"The clock is busy. Try again.");return;}
 sqlite3_stmt*q=NULL;int ok=1;
 if(!strcmp(cycle,"semi_monthly")){
  int invalid=0;
  if(sqlite3_prepare_v2(db,"SELECT anchor_payday<>'' AND CAST(substr(anchor_payday,9,2) AS INTEGER) NOT IN (1,15) FROM payroll_settings WHERE id=1",-1,&q,NULL)==SQLITE_OK){if(sqlite3_step(q)==SQLITE_ROW)invalid=sqlite3_column_int(q,0);}else ok=0;
  sqlite3_finalize(q);q=NULL;
  if(invalid){sql("ROLLBACK");rec_error(fd,409,"Set the Payroll report payday anchor to the 1st or 15th before choosing twice monthly.");return;}
 }
 /* Re-saving an unchanged configuration must never discard future periods. */
 int changed=1;
 if(sqlite3_prepare_v2(db,"SELECT cycle<>?1 OR anchor<>?2 FROM period_cycle_settings WHERE id=1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,cycle,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,anchor,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW)changed=sqlite3_column_int(q,0);else ok=0;}else ok=0;
 sqlite3_finalize(q);q=NULL;
 if(!ok){sql("ROLLBACK");rec_error(fd,500,"Could not read current pay cycle.");return;}
 if(!changed){sqlite3_stmt *a=NULL;int saved=sqlite3_prepare_v2(db,"UPDATE period_cycle_settings SET updated_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=1",-1,&a,NULL)==SQLITE_OK;if(saved){saved=sqlite3_step(a)==SQLITE_DONE;}sqlite3_finalize(a);if(saved)sql("COMMIT");else sql("ROLLBACK");if(saved)send_response(fd,200,"application/json","{\"status\":\"saved\"}");else rec_error(fd,500,"Could not save the pay schedule.");return;}
 const char *guard="SELECT count(*) FROM pay_periods p WHERE p.starts>date('now','localtime') AND (p.status<>'open' OR EXISTS(SELECT 1 FROM period_signoffs s WHERE s.period_id=p.id) OR EXISTS(SELECT 1 FROM lite_period_snapshots s WHERE s.period_id=p.id) OR EXISTS(SELECT 1 FROM email_queue e WHERE e.event_id='payroll-period:'||p.id) OR EXISTS(SELECT 1 FROM review_links r WHERE r.period_id=p.id))";
 int blocked=0;
 if(sqlite3_prepare_v2(db,guard,-1,&q,NULL)==SQLITE_OK){if(sqlite3_step(q)==SQLITE_ROW)blocked=sqlite3_column_int(q,0);else ok=0;}else ok=0;
 sqlite3_finalize(q);q=NULL;
 if(blocked){sql("ROLLBACK");rec_error(fd,409,"A future period already has review or delivery records. It cannot be replaced automatically.");return;}
 if(ok)ok=sql("DELETE FROM pay_periods WHERE starts>date('now','localtime') AND status='open' AND NOT EXISTS(SELECT 1 FROM period_signoffs s WHERE s.period_id=pay_periods.id) AND NOT EXISTS(SELECT 1 FROM lite_period_snapshots s WHERE s.period_id=pay_periods.id) AND NOT EXISTS(SELECT 1 FROM email_queue e WHERE e.event_id='payroll-period:'||pay_periods.id) AND NOT EXISTS(SELECT 1 FROM review_links r WHERE r.period_id=pay_periods.id)");
 if(ok)ok=sqlite3_prepare_v2(db,"UPDATE period_cycle_settings SET cycle=?1,anchor=?2,updated_by=?3,updated_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=1",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,cycle,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,anchor,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,3,who);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");
 if(ok)send_response(fd,200,"application/json","{\"status\":\"saved\"}");else rec_error(fd,500,"The cycle change could not be completed; previous periods were retained.");
}
/* Civil calendar arithmetic in SQLite handles variable month lengths and leap years. */
/* Read-only preview uses exactly the generation calendar logic and checks overlaps. */
/* One shared calendar source for preview and generation. */
#define REC_PERIOD_CALENDAR "WITH RECURSIVE cfg AS (SELECT cycle,anchor FROM period_cycle_settings WHERE id=1), " \
 "dates(n,starts,ends) AS (" \
 "SELECT 0,CASE WHEN cycle='semi_monthly' THEN date('now','localtime','start of month','-12 months') " \
 "WHEN cycle='monthly' THEN date('now','localtime','start of month','-12 months') " \
 "ELSE date(anchor,printf('%+d days',(CAST((julianday(date('now','localtime'))-julianday(anchor))/(CASE WHEN cycle='weekly' THEN 7 ELSE 14 END) AS INTEGER)-55)*(CASE WHEN cycle='weekly' THEN 7 ELSE 14 END))) END, '' FROM cfg " \
 "UNION ALL SELECT n+1,CASE WHEN (SELECT cycle FROM cfg) IN ('semi_monthly','monthly') THEN " \
 "CASE WHEN (SELECT cycle FROM cfg)='semi_monthly' AND CAST(strftime('%d',starts) AS INTEGER)=1 THEN date(starts,'+15 days') ELSE date(starts,'start of month','+1 month') END " \
 "ELSE date(starts,CASE WHEN (SELECT cycle FROM cfg)='weekly' THEN '+7 days' ELSE '+14 days' END) END,'' FROM dates WHERE n<85), " \
 "periods AS (SELECT starts,CASE WHEN (SELECT cycle FROM cfg)='semi_monthly' THEN CASE WHEN CAST(strftime('%d',starts) AS INTEGER)=1 THEN date(starts,'+14 days') ELSE date(starts,'start of month','+1 month','-1 day') END " \
 "WHEN (SELECT cycle FROM cfg)='monthly' THEN date(starts,'start of month','+1 month','-1 day') " \
 "WHEN (SELECT cycle FROM cfg)='weekly' THEN date(starts,'+6 days') ELSE date(starts,'+13 days') END ends FROM dates), " \
 "bridge AS (SELECT max(date(max_end,'+1 day'),date('now','localtime')) AS starts, date((SELECT min(starts) FROM periods WHERE starts>max_end),'-1 day') AS ends FROM (SELECT max(ends) AS max_end FROM pay_periods WHERE starts<=date('now','localtime')) WHERE max_end IS NOT NULL), " \
 "candidates AS (SELECT starts,ends FROM periods UNION ALL SELECT starts,ends FROM bridge WHERE starts<=ends) "
/* Primary server upkeep. Keep the current period and two future periods,
   with the shared calendar/bridge rules. No approval or delivery side effects. */
static const char *rec_period_sql_auto=REC_PERIOD_CALENDAR
 "INSERT INTO pay_periods(starts,ends) SELECT c.starts,c.ends FROM candidates c "
 "WHERE c.ends>=date('now','localtime') AND (c.starts<=date('now','localtime') "
 "OR c.starts IN (SELECT starts FROM candidates WHERE starts>date('now','localtime') AND starts>coalesce((SELECT max(ends) FROM pay_periods),'0000-01-01') ORDER BY starts LIMIT "
 "(SELECT max(0,2-count(*)) FROM pay_periods WHERE starts>date('now','localtime')))) "
 "AND c.starts>coalesce((SELECT max(ends) FROM pay_periods),'0000-01-01') "
 "AND NOT EXISTS(SELECT 1 FROM pay_periods p WHERE NOT(p.ends<c.starts OR p.starts>c.ends)) "
 "ORDER BY c.starts";
/* Recover historical periods only when actual records would otherwise be orphaned.
   These periods are not approved, closed, or emailed automatically. */
static const char *rec_period_sql_activity=REC_PERIOD_CALENDAR
 "INSERT INTO pay_periods(starts,ends) SELECT c.starts,c.ends FROM candidates c "
 "WHERE c.ends<date('now','localtime') "
 "AND (EXISTS(SELECT 1 FROM effective_punches p WHERE date(p.at,'localtime') BETWEEN c.starts AND c.ends) "
 "OR EXISTS(SELECT 1 FROM attendance_absences a WHERE a.cancelled_at IS NULL AND a.day BETWEEN c.starts AND c.ends) "
 "OR EXISTS(SELECT 1 FROM timeoff_requests t WHERE t.status='approved' AND t.cancelled_at IS NULL AND t.starts<=c.ends AND t.ends>=c.starts)) "
 "AND NOT EXISTS(SELECT 1 FROM pay_periods p WHERE NOT(p.ends<c.starts OR p.starts>c.ends))";
static void rec_period_auto_due(void){
 sqlite3_stmt*q=NULL;
 if(!sql("BEGIN IMMEDIATE")){fprintf(stderr,"Automatic pay periods: database busy\n");return;}
 int ok=sqlite3_prepare_v2(db,rec_period_sql_auto,-1,&q,NULL)==SQLITE_OK;
 if(ok)ok=sqlite3_step(q)==SQLITE_DONE;
 if(!ok)fprintf(stderr,"Automatic pay periods: %s\n",sqlite3_errmsg(db));
 sqlite3_finalize(q);
 if(ok){sqlite3_stmt *history=NULL;ok=sqlite3_prepare_v2(db,rec_period_sql_activity,-1,&history,NULL)==SQLITE_OK;if(ok)ok=sqlite3_step(history)==SQLITE_DONE;sqlite3_finalize(history);}
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");
 if(!ok)fprintf(stderr,"Automatic pay periods: unable to save future periods\n");
}
static int rec_employee_totals_source(int emp,const char*starts,const char*ends,int*count,long long*seconds,int cached);
static int rec_employee_totals(int emp,const char*starts,const char*ends,int*count,long long*seconds);
static void rec_period_people(int fd,const char*query){
 char txt[24]="";if(!form_value(query,"period_id",txt,sizeof txt)){rec_error(fd,400,"Select a pay period.");return;}
 char*end=NULL;long id=strtol(txt,&end,10);if(!*txt||*end||id<=0||id>2147483647){rec_error(fd,400,"Select a pay period.");return;}
 sqlite3_stmt*q=NULL;const char*sqltext="SELECT e.id,e.name,coalesce(ep.cnt,0),coalesce(ep.unpaired,0),coalesce(es.signed_at,''),coalesce(ms.signed_at,''),coalesce(ms.approved_seconds,0),coalesce(ms.unconfirmed,0),coalesce(ms.reason,''),coalesce(es.punch_count,-1),coalesce(es.approved_seconds,-1),coalesce(es.leave_fingerprint,''),coalesce(ms.punch_count,-1),coalesce(ms.leave_fingerprint,'') FROM employees e CROSS JOIN pay_periods p LEFT JOIN (SELECT employee_id,count(*) cnt,sum(CASE WHEN state<>'complete' THEN 1 ELSE 0 END) unpaired FROM effective_shift_pairs WHERE date(in_at,'localtime') BETWEEN (SELECT starts FROM pay_periods WHERE id=?1) AND (SELECT ends FROM pay_periods WHERE id=?1) GROUP BY employee_id) ep ON ep.employee_id=e.id LEFT JOIN period_signoffs es ON es.period_id=p.id AND es.employee_id=e.id AND es.role='employee' LEFT JOIN period_signoffs ms ON ms.period_id=p.id AND ms.employee_id=e.id AND ms.role='manager' WHERE p.id=?1 AND (ep.cnt>0 OR es.signed_at IS NOT NULL OR ms.signed_at IS NOT NULL OR EXISTS(SELECT 1 FROM attendance_absences a WHERE a.employee_id=e.id AND a.cancelled_at IS NULL AND a.day BETWEEN p.starts AND p.ends) OR EXISTS(SELECT 1 FROM timeoff_requests t WHERE t.employee_id=e.id AND t.status='approved' AND t.cancelled_at IS NULL AND t.starts<=p.ends AND t.ends>=p.starts)) ORDER BY e.name";
 if(sqlite3_prepare_v2(db,sqltext,-1,&q,NULL)!=SQLITE_OK){rec_error(fd,500,"The clock could not complete this. Try again.");return;}
 char period_starts[16]="",period_ends[16]="";sqlite3_stmt *bounds=NULL;
 if(sqlite3_prepare_v2(db,"SELECT starts,ends FROM pay_periods WHERE id=?1",-1,&bounds,NULL)==SQLITE_OK){sqlite3_bind_int(bounds,1,(int)id);if(sqlite3_step(bounds)==SQLITE_ROW){snprintf(period_starts,sizeof period_starts,"%s",op_text(bounds,0));snprintf(period_ends,sizeof period_ends,"%s",op_text(bounds,1));}}sqlite3_finalize(bounds);
 sqlite3_bind_int(q,1,(int)id);sqlite3_str*out=sqlite3_str_new(db);sqlite3_str_appendall(out,"{\"people\":[");int n=0;
 while(sqlite3_step(q)==SQLITE_ROW){if(n++)sqlite3_str_appendchar(out,1,',');sqlite3_str_appendf(out,"{\"id\":%d,\"name\":",sqlite3_column_int(q,0));str_json(out,op_text(q,1));sqlite3_str_appendf(out,",\"punches\":%d,\"unpaired\":%d,\"employee_signed\":%s,\"manager_signed\":%s,\"approved_seconds\":%lld,\"unconfirmed\":%s,\"override_reason\":",sqlite3_column_int(q,2),sqlite3_column_int(q,3),sqlite3_column_type(q,4)==SQLITE_TEXT&&!strcmp(op_text(q,4),"")?"false":"true",sqlite3_column_type(q,5)==SQLITE_TEXT&&!strcmp(op_text(q,5),"")?"false":"true",sqlite3_column_int64(q,6),sqlite3_column_int(q,7)?"true":"false");str_json(out,op_text(q,8));int current_count=0;long long current_seconds=0;rec_employee_totals(sqlite3_column_int(q,0),period_starts,period_ends,&current_count,&current_seconds);char fp[17]="";int fp_ok=rec_leave_fingerprint(sqlite3_column_int(q,0),period_starts,period_ends,fp);int emp_current=fp_ok&&current_count==sqlite3_column_int(q,9)&&current_seconds==sqlite3_column_int64(q,10)&&!strcmp(fp,op_text(q,11));int mgr_current=fp_ok&&current_count==sqlite3_column_int(q,12)&&current_seconds==sqlite3_column_int64(q,6)&&!strcmp(fp,op_text(q,13));sqlite3_str_appendf(out,",\"confirmation_current\":%s,\"manager_current\":%s,\"current_seconds\":%lld}",emp_current?"true":"false",mgr_current?"true":"false",current_seconds);}
 sqlite3_finalize(q);sqlite3_str_appendall(out,"]}");send_str(fd,"application/json",out);
}
static void rec_period_cache_clear(void){
 sql("DROP TABLE IF EXISTS temp.lite_period_report_intervals; DROP TABLE IF EXISTS temp.lite_period_pairs; DROP TABLE IF EXISTS temp.lite_period_shift_pairs; DROP TABLE IF EXISTS temp.lite_period_punches;");
}
static void rec_periods(int fd){
 /* Materialize paired attendance ONCE for this request. Re-running the
    recursive effective_punches view for each of 50 periods stalls RFID. */
 rec_period_cache_clear();
 if(!sql("CREATE TEMP TABLE lite_period_punches AS SELECT * FROM main.effective_punches")||
    !sql("CREATE INDEX temp.lite_punch_employee ON lite_period_punches(employee_id,at)")||
    !sql("CREATE TEMP TABLE lite_period_shift_pairs AS SELECT * FROM main.effective_shift_pairs")||
    !sql("CREATE INDEX temp.lite_pair_employee ON lite_period_shift_pairs(employee_id,in_at)")||
    !sql("CREATE TEMP TABLE lite_period_report_intervals AS WITH punches AS (SELECT p.*,lead(at) OVER (PARTITION BY employee_id ORDER BY at,id) next_at,lead(kind) OVER (PARTITION BY employee_id ORDER BY at,id) next_kind,lead(id) OVER (PARTITION BY employee_id ORDER BY at,id) next_id FROM lite_period_punches p) SELECT employee_id,id,at,next_at,next_id,manual,CAST(strftime('%s',next_at) AS INTEGER)-CAST(strftime('%s',at) AS INTEGER) seconds FROM punches WHERE kind='IN' AND next_kind='OUT' AND next_at IS NOT NULL")||
    !sql("CREATE TEMP TABLE lite_period_pairs AS SELECT employee_id,date(in_at,'localtime') AS day FROM lite_period_shift_pairs")||
    !sql("CREATE INDEX lite_period_pairs_day ON lite_period_pairs(day,employee_id)")){
  rec_period_cache_clear();rec_error(fd,500,"Unable to load pay periods.");return;
 }
sqlite3_stmt*q=NULL;sqlite3_str*s=sqlite3_str_new(db);sqlite3_str_appendall(s,"{\"periods\":[");int n=0;if(sqlite3_prepare_v2(db,"SELECT id,starts,ends,status,coalesce(closed_at,''),(SELECT count(*) FROM period_signoffs so WHERE so.period_id=p.id AND so.role='manager' AND so.employee_id IN (SELECT employee_id FROM lite_period_pairs WHERE day BETWEEN p.starts AND p.ends UNION SELECT employee_id FROM attendance_absences WHERE day BETWEEN p.starts AND p.ends AND cancelled_at IS NULL UNION SELECT employee_id FROM timeoff_requests WHERE status='approved' AND cancelled_at IS NULL AND starts<=p.ends AND ends>=p.starts)) FROM pay_periods p ORDER BY starts DESC LIMIT 50",-1,&q,NULL)==SQLITE_OK){while(sqlite3_step(q)==SQLITE_ROW){if(n++)sqlite3_str_appendchar(s,1,',');sqlite3_str_appendf(s,"{\"id\":%d,\"starts\":",sqlite3_column_int(q,0));str_json(s,op_text(q,1));sqlite3_str_appendall(s,",\"ends\":");str_json(s,op_text(q,2));sqlite3_str_appendall(s,",\"status\":");str_json(s,op_text(q,3));sqlite3_str_appendall(s,",\"closed_at\":");str_json(s,op_text(q,4));sqlite3_str_appendf(s,",\"manager_signoffs\":%d",sqlite3_column_int(q,5));
 sqlite3_stmt *totalq=NULL;int required=0;
 const char *total_sql="SELECT count(*) FROM (SELECT employee_id FROM lite_period_pairs WHERE day BETWEEN ?1 AND ?2 UNION SELECT employee_id FROM attendance_absences WHERE day BETWEEN ?1 AND ?2 AND cancelled_at IS NULL UNION SELECT employee_id FROM timeoff_requests WHERE status='approved' AND cancelled_at IS NULL AND starts<=?2 AND ends>=?1)";
 if(sqlite3_prepare_v2(db,total_sql,-1,&totalq,NULL)==SQLITE_OK){sqlite3_bind_text(totalq,1,op_text(q,1),-1,SQLITE_TRANSIENT);sqlite3_bind_text(totalq,2,op_text(q,2),-1,SQLITE_TRANSIENT);if(sqlite3_step(totalq)==SQLITE_ROW)required=sqlite3_column_int(totalq,0);}sqlite3_finalize(totalq);
 sqlite3_str_appendf(s,",\"approval_total\":%d",required);sqlite3_stmt *activity=NULL;int has_activity=0;if(sqlite3_prepare_v2(db,"SELECT EXISTS(SELECT 1 FROM lite_period_pairs WHERE day BETWEEN ?1 AND ?2) OR EXISTS(SELECT 1 FROM attendance_absences WHERE day BETWEEN ?1 AND ?2 AND cancelled_at IS NULL) OR EXISTS(SELECT 1 FROM timeoff_requests WHERE status='approved' AND cancelled_at IS NULL AND starts<=?2 AND ends>=?1)",-1,&activity,NULL)==SQLITE_OK){sqlite3_bind_text(activity,1,op_text(q,1),-1,SQLITE_TRANSIENT);sqlite3_bind_text(activity,2,op_text(q,2),-1,SQLITE_TRANSIENT);if(sqlite3_step(activity)==SQLITE_ROW)has_activity=sqlite3_column_int(activity,0);}sqlite3_finalize(activity);sqlite3_str_appendf(s,",\"has_activity\":%s",has_activity?"true":"false");
 if(!strcmp(op_text(q,3),"closed")){
  sqlite3_stmt *reviews=NULL;int changed=required>sqlite3_column_int(q,5),review_failed=0;
  if(sqlite3_prepare_v2(db,"SELECT employee_id,punch_count,approved_seconds,leave_fingerprint FROM period_signoffs WHERE period_id=?1 AND role='manager'",-1,&reviews,NULL)==SQLITE_OK){
   sqlite3_bind_int(reviews,1,sqlite3_column_int(q,0));int rc;
   while((rc=sqlite3_step(reviews))==SQLITE_ROW){int count=0;long long seconds=0;char fp[17]="";
    if(!rec_employee_totals_source(sqlite3_column_int(reviews,0),op_text(q,1),op_text(q,2),&count,&seconds,1)||!rec_leave_fingerprint(sqlite3_column_int(reviews,0),op_text(q,1),op_text(q,2),fp))review_failed=1;
    else if(count!=sqlite3_column_int(reviews,1)||seconds!=sqlite3_column_int64(reviews,2)||strcmp(fp,op_text(reviews,3)))changed=1;
   }if(rc!=SQLITE_DONE)review_failed=1;
  }else review_failed=1;
  sqlite3_finalize(reviews);
  sqlite3_stmt *latest=NULL;const char *saved=NULL;int rev=0,has=0;
  int ready=sqlite3_prepare_v2(db,"SELECT revision,csv FROM lite_period_snapshots WHERE period_id=?1 ORDER BY revision DESC LIMIT 1",-1,&latest,NULL)==SQLITE_OK;
  if(ready){sqlite3_bind_int(latest,1,sqlite3_column_int(q,0));int rc=sqlite3_step(latest);if(rc==SQLITE_ROW){rev=sqlite3_column_int(latest,0);saved=(const char*)sqlite3_column_text(latest,1);has=1;}else if(rc!=SQLITE_DONE)ready=0;}
  char *body=NULL,*csv=NULL;
  int built=ready&&payroll_build_timesheet_source(db,op_text(q,1),op_text(q,2),&body,&csv,1);
  int stale=built&&has&&saved&&csv&&strcmp(saved,csv)!=0;
  sqlite3_str_appendf(s,",\"changed_after_close\":%s,\"latest_revision\":%d,\"export_stale\":%s,\"new_export_required\":%s,\"export_check_failed\":%s",changed?"true":"false",rev,stale?"true":"false",built&&(!has||stale)?"true":"false",built&&!review_failed?"false":"true");
  sqlite3_free(body);sqlite3_free(csv);sqlite3_finalize(latest);
 }
 sqlite3_stmt *failure=NULL;
 if(sqlite3_prepare_v2(db,"SELECT reason FROM lite_payroll_queue_failures WHERE period_id=?1",-1,&failure,NULL)==SQLITE_OK){sqlite3_bind_int(failure,1,sqlite3_column_int(q,0));if(sqlite3_step(failure)==SQLITE_ROW){sqlite3_str_appendall(s,",\"queue_failure\":");str_json(s,op_text(failure,0));}}
 sqlite3_finalize(failure);sqlite3_str_appendchar(s,1,'}');}}sqlite3_finalize(q);sqlite3_str_appendall(s,"]}");send_str(fd,"application/json",s);rec_period_cache_clear();}
static int rec_employee_totals_source(int emp,const char*starts,const char*ends,int*count,long long*seconds,int cached){
 sqlite3_stmt*q=NULL;*count=0;*seconds=0;
 const char*sqltext=cached?"SELECT count(*),coalesce(sum(seconds),0) FROM temp.lite_period_shift_pairs WHERE employee_id=?1 AND date(in_at,'localtime') BETWEEN ?2 AND ?3":"SELECT count(*),coalesce(sum(seconds),0) FROM effective_shift_pairs WHERE employee_id=?1 AND date(in_at,'localtime') BETWEEN ?2 AND ?3";
 if(sqlite3_prepare_v2(db,sqltext,-1,&q,NULL)!=SQLITE_OK)return 0;
 sqlite3_bind_int(q,1,emp);sqlite3_bind_text(q,2,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,ends,-1,SQLITE_TRANSIENT);
 int ok=0;if(sqlite3_step(q)==SQLITE_ROW){*count=sqlite3_column_int(q,0);*seconds=sqlite3_column_int64(q,1);ok=1;}sqlite3_finalize(q);return ok;
}
static int rec_employee_totals(int emp,const char*starts,const char*ends,int*count,long long*seconds){
 return rec_employee_totals_source(emp,starts,ends,count,seconds,0);
}
static void rec_period_action(int fd,int who,const char*body){char action[24]="",starts[16]="",ends[16]="",reason[250]="",num[20]="";if(!form_value(body,"action",action,sizeof action)||!form_value(body,"starts",starts,sizeof starts)||!form_value(body,"ends",ends,sizeof ends)||!form_value_multiline(body,"reason",reason,sizeof reason)||!form_value(body,"employee_id",num,sizeof num)){rec_error(fd,400,"Fill in all required fields.");return;}if(!rec_date(starts)||!rec_date(ends)||strcmp(starts,ends)>0||strlen(reason)<5u||strlen(reason)>220){rec_error(fd,400,"Enter valid dates and a reason of 5 to 220 characters.");return;}
 char *end=NULL;long employee=strtol(num,&end,10);if(!*num||*end||employee<0||employee>2147483647){rec_error(fd,400,"Employee not found. Reload the page.");return;}
 if(strcmp(action,"create")&&strcmp(action,"sign")&&strcmp(action,"close")){rec_error(fd,400,"Unknown period action");return;}
 if(!sql("BEGIN IMMEDIATE")){rec_error(fd,503,"The clock is busy. Try again.");return;}
 sqlite3_stmt*q=NULL;int ok=1,id=0,closed=0;const char*fail="The period could not be updated. Reload the page and try again.";
 if(!strcmp(action,"create")){
  if(sqlite3_prepare_v2(db,"SELECT count(*) FROM pay_periods WHERE NOT (ends<?1 OR starts>?2)",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,ends,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW&&sqlite3_column_int(q,0)){ok=0;fail="These dates overlap an existing pay period.";}}else {ok=0;}sqlite3_finalize(q);q=NULL;
  if(ok)ok=sqlite3_prepare_v2(db,"INSERT INTO pay_periods(starts,ends) VALUES(?1,?2)",-1,&q,NULL)==SQLITE_OK;
  if(ok){sqlite3_bind_text(q,1,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,ends,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 }else{
  if(sqlite3_prepare_v2(db,"SELECT id,status='closed' FROM pay_periods WHERE starts=?1 AND ends=?2",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,ends,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW){id=sqlite3_column_int(q,0);closed=sqlite3_column_int(q,1);}}sqlite3_finalize(q);q=NULL;
  if(!id){ok=0;fail="Pay period not found. Reload the page.";}else if(closed&&strcmp(action,"sign")){ok=0;fail="This pay period is already closed.";}
  if(ok&&!strcmp(action,"sign")){
   if(employee==0){ok=0;fail="Choose an employee.";}
   int punches=0;long long secs=0;
   if(ok)ok=rec_employee_totals((int)employee,starts,ends,&punches,&secs);
   /* An approval requires actual period activity, not merely an employee record. */
   if(ok){sqlite3_stmt *v=NULL;int active=0;
    const char *sql="SELECT EXISTS(SELECT 1 FROM effective_shift_pairs WHERE employee_id=?1 AND date(in_at,'localtime') BETWEEN ?2 AND ?3) OR EXISTS(SELECT 1 FROM attendance_absences WHERE employee_id=?1 AND day BETWEEN ?2 AND ?3 AND cancelled_at IS NULL) OR EXISTS(SELECT 1 FROM timeoff_requests WHERE employee_id=?1 AND status='approved' AND cancelled_at IS NULL AND starts<=?3 AND ends>=?2)";
    if(sqlite3_prepare_v2(db,sql,-1,&v,NULL)==SQLITE_OK){sqlite3_bind_int(v,1,(int)employee);sqlite3_bind_text(v,2,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(v,3,ends,-1,SQLITE_TRANSIENT);if(sqlite3_step(v)==SQLITE_ROW)active=sqlite3_column_int(v,0);}sqlite3_finalize(v);
    if(!active){ok=0;fail="This employee has no punches or leave in this pay period.";}
   }
   /* Manager may approve with employee confirmation or override it with a reason. */
   if(ok){
    sqlite3_stmt *v=NULL;int bad=0,open=0,seen=0;
    if(sqlite3_prepare_v2(db,"SELECT state FROM effective_shift_pairs WHERE employee_id=?1 AND date(in_at,'localtime') BETWEEN ?2 AND ?3 ORDER BY in_at,in_id",-1,&v,NULL)==SQLITE_OK){
     sqlite3_bind_int(v,1,(int)employee);sqlite3_bind_text(v,2,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(v,3,ends,-1,SQLITE_TRANSIENT);
     while(sqlite3_step(v)==SQLITE_ROW){const char*k=op_text(v,0);seen=1;if(strcmp(k,"complete"))bad=1;}
     if(open)bad=1;
    }else bad=1;
    sqlite3_finalize(v);
    if(bad){ok=0;fail="Fix unmatched punches before approval.";}
    (void)seen; /* An employee with recorded leave only may have zero punches. */
   }
   int employee_confirmed=0,manager_changed=0;
   if(ok){sqlite3_stmt*ec=NULL;if(sqlite3_prepare_v2(db,"SELECT 1 FROM period_signoffs WHERE period_id=?1 AND employee_id=?2 AND role='employee' AND punch_count=?3 AND approved_seconds=?4 AND leave_fingerprint=?5",-1,&ec,NULL)==SQLITE_OK){sqlite3_bind_int(ec,1,id);sqlite3_bind_int(ec,2,(int)employee);sqlite3_bind_int(ec,3,punches);sqlite3_bind_int64(ec,4,secs);char fp[17]="";if(rec_leave_fingerprint((int)employee,starts,ends,fp)){sqlite3_bind_text(ec,5,fp,-1,SQLITE_TRANSIENT);employee_confirmed=sqlite3_step(ec)==SQLITE_ROW;}}sqlite3_finalize(ec);}
   if(ok)ok=sqlite3_prepare_v2(db,"INSERT INTO period_signoffs(period_id,employee_id,role,signed_by,punch_count,approved_seconds,unconfirmed,reason,leave_fingerprint) SELECT ?1,id,'manager',?3,?4,?5,?7,?6,?8 FROM employees WHERE id=?2 ON CONFLICT(period_id,employee_id,role) DO UPDATE SET signed_by=excluded.signed_by,punch_count=excluded.punch_count,approved_seconds=excluded.approved_seconds,unconfirmed=excluded.unconfirmed,reason=excluded.reason,leave_fingerprint=excluded.leave_fingerprint,signed_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE period_signoffs.punch_count<>excluded.punch_count OR period_signoffs.approved_seconds<>excluded.approved_seconds OR period_signoffs.leave_fingerprint<>excluded.leave_fingerprint OR period_signoffs.unconfirmed<>excluded.unconfirmed",-1,&q,NULL)==SQLITE_OK;
   if(ok){sqlite3_bind_int(q,1,id);sqlite3_bind_int(q,2,(int)employee);sqlite3_bind_int(q,3,who);sqlite3_bind_int(q,4,punches);sqlite3_bind_int64(q,5,secs);sqlite3_bind_text(q,6,reason,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,7,!employee_confirmed);char fp[17]="";ok=rec_leave_fingerprint((int)employee,starts,ends,fp);if(ok){sqlite3_bind_text(q,8,fp,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;if(ok)manager_changed=sqlite3_changes(db)>0;}}sqlite3_finalize(q);q=NULL;
   if(ok&&manager_changed&&!employee_confirmed){sqlite3_stmt*a=NULL;ok=sqlite3_prepare_v2(db,"INSERT INTO period_approval_audit(period_id,employee_id,manager_id,action,reason,approved_seconds) VALUES(?1,?2,?3,'manager_override',?4,?5)",-1,&a,NULL)==SQLITE_OK;if(ok){sqlite3_bind_int(a,1,id);sqlite3_bind_int(a,2,(int)employee);sqlite3_bind_int(a,3,who);sqlite3_bind_text(a,4,reason,-1,SQLITE_TRANSIENT);sqlite3_bind_int64(a,5,secs);ok=sqlite3_step(a)==SQLITE_DONE;}sqlite3_finalize(a);}
  }else if(ok){
   /* Close only after each employee with effective punches in this period has manager approval and no unpaired IN. */
   if(sqlite3_prepare_v2(db,"SELECT count(*) FROM (SELECT employee_id FROM effective_shift_pairs WHERE date(in_at,'localtime') BETWEEN ?1 AND ?2 UNION SELECT employee_id FROM attendance_absences WHERE day BETWEEN ?1 AND ?2 AND cancelled_at IS NULL UNION SELECT employee_id FROM timeoff_requests WHERE status='approved' AND cancelled_at IS NULL AND starts<=?2 AND ends>=?1 EXCEPT SELECT employee_id FROM period_signoffs WHERE period_id=?3 AND role='manager')",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,ends,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,3,id);if(sqlite3_step(q)==SQLITE_ROW&&sqlite3_column_int(q,0)){ok=0;fail="Approve every employee with punches or leave in this period first.";}}else {ok=0;}sqlite3_finalize(q);q=NULL;
   if(ok){
    /* Attendance edits after review invalidate signoff rather than silently changing approved hours. */
    sqlite3_stmt*checks=NULL;
    if(sqlite3_prepare_v2(db,"SELECT employee_id,punch_count,approved_seconds,leave_fingerprint FROM period_signoffs s WHERE period_id=?1 AND (role='manager' OR NOT EXISTS(SELECT 1 FROM period_signoffs m WHERE m.period_id=s.period_id AND m.employee_id=s.employee_id AND m.role='manager' AND m.unconfirmed=1))",-1,&checks,NULL)==SQLITE_OK){
     sqlite3_bind_int(checks,1,id);
     while(sqlite3_step(checks)==SQLITE_ROW){int count=0;long long seconds=0;char fp[17]="";if(!rec_employee_totals(sqlite3_column_int(checks,0),starts,ends,&count,&seconds)||count!=sqlite3_column_int(checks,1)||seconds!=sqlite3_column_int64(checks,2)||!rec_leave_fingerprint(sqlite3_column_int(checks,0),starts,ends,fp)||strcmp(fp,op_text(checks,3))){ok=0;fail="Attendance or leave changed after approval. Review and approve the affected employees again.";}}
    }else ok=0;
    sqlite3_finalize(checks);
   }
   if(ok){
    /* Reject periods containing unmatched IN or consecutive repeated directions. */
    sqlite3_stmt*v=NULL;
    const char*query="SELECT employee_id,state FROM effective_shift_pairs WHERE date(in_at,'localtime') BETWEEN ?1 AND ?2 ORDER BY employee_id,in_at,in_id";
    if(sqlite3_prepare_v2(db,query,-1,&v,NULL)==SQLITE_OK){
     sqlite3_bind_text(v,1,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(v,2,ends,-1,SQLITE_TRANSIENT);
     while(sqlite3_step(v)==SQLITE_ROW){const char*kind=op_text(v,1);if(strcmp(kind,"complete"))ok=0;}
     if(!ok)fail="Some punches have no matching IN or OUT. Fix them under Punches and corrections.";
    }else ok=0;
    sqlite3_finalize(v);
   }
   if(ok)ok=sqlite3_prepare_v2(db,"UPDATE pay_periods SET status='closed',closed_by=?1,closed_at=strftime('%Y-%m-%dT%H:%M:%fZ','now'),reason=?2 WHERE id=?3 AND status='open'",-1,&q,NULL)==SQLITE_OK;
   if(ok){sqlite3_bind_int(q,1,who);sqlite3_bind_text(q,2,reason,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,3,id);ok=sqlite3_step(q)==SQLITE_DONE&&sqlite3_changes(db)==1;}sqlite3_finalize(q);
  }
 }
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");if(ok)send_response(fd,200,"application/json","{\"status\":\"saved\"}");else rec_error(fd,409,fail);
}
static void rec_absences(int fd){
 sqlite3_stmt*q=NULL;
 const char *query="SELECT a.id,a.employee_id,e.name,a.day,a.reason,a.note,a.starts,a.ends,a.cancelled_at IS NOT NULL,coalesce(m.name,''),EXISTS(SELECT 1 FROM effective_punches p WHERE p.employee_id=a.employee_id AND date(p.at,'localtime')=a.day) FROM attendance_absences a JOIN employees e ON e.id=a.employee_id LEFT JOIN employees m ON m.id=a.manager_id ORDER BY a.day DESC,a.id DESC LIMIT 300";
 if(sqlite3_prepare_v2(db,query,-1,&q,NULL)!=SQLITE_OK){rec_error(fd,500,"The clock could not load absences.");return;}
 sqlite3_str*j=sqlite3_str_new(db);sqlite3_str_appendall(j,"{\"absences\":[");int n=0;
 while(sqlite3_step(q)==SQLITE_ROW){if(n++)sqlite3_str_appendchar(j,1,',');sqlite3_str_appendf(j,"{\"id\":%d,\"employee_id\":%d,\"employee\":",sqlite3_column_int(q,0),sqlite3_column_int(q,1));str_json(j,op_text(q,2));sqlite3_str_appendall(j,",\"day\":");str_json(j,op_text(q,3));sqlite3_str_appendall(j,",\"reason\":");str_json(j,op_text(q,4));sqlite3_str_appendall(j,",\"note\":");str_json(j,op_text(q,5));sqlite3_str_appendall(j,",\"starts\":");str_json(j,op_text(q,6));sqlite3_str_appendall(j,",\"ends\":");str_json(j,op_text(q,7));sqlite3_str_appendf(j,",\"cancelled\":%s,\"manager\":",sqlite3_column_int(q,8)?"true":"false");str_json(j,op_text(q,9));sqlite3_str_appendf(j,",\"has_punches\":%s}",sqlite3_column_int(q,10)?"true":"false");}
 sqlite3_finalize(q);sqlite3_str_appendall(j,"]}");send_str(fd,"application/json",j);
}
static void rec_absence_save(int fd,int who,const char *body){
 char emp[24]="",date[16]="",reason[64]="",note[256]="",start[16]="",end[16]="",action[24]="",aid[24]="";
 if(who<=0){rec_error(fd,403,"Manager sign-in required.");return;}
 form_value(body,"action",action,sizeof action);form_value(body,"id",aid,sizeof aid);
 if(!strcmp(action,"cancel")){
  char *tail=NULL;long id=strtol(aid,&tail,10);
  if(id<=0||*tail||!form_value_multiline(body,"note",note,sizeof note)||strlen(note)<5){rec_error(fd,400,"Enter a cancellation reason of at least 5 characters.");return;}
  if(!sql("BEGIN IMMEDIATE")){rec_error(fd,503,"The clock is busy. Try again.");return;}
  sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(db,"UPDATE attendance_absences SET cancelled_at=strftime('%Y-%m-%dT%H:%M:%fZ','now'),cancelled_by=?1 WHERE id=?2 AND cancelled_at IS NULL",-1,&q,NULL)==SQLITE_OK;
  if(ok){sqlite3_bind_int(q,1,who);sqlite3_bind_int(q,2,(int)id);ok=sqlite3_step(q)==SQLITE_DONE && sqlite3_changes(db)==1;}sqlite3_finalize(q);q=NULL;
  if(ok)ok=sqlite3_prepare_v2(db,"INSERT INTO attendance_absence_audit(absence_id,manager_id,action,reason) VALUES(?1,?2,'cancel',?3)",-1,&q,NULL)==SQLITE_OK;
  if(ok){sqlite3_bind_int(q,1,(int)id);sqlite3_bind_int(q,2,who);sqlite3_bind_text(q,3,note,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
  if(ok)ok=sql("COMMIT");else sql("ROLLBACK");if(ok)send_response(fd,200,"application/json","{\"status\":\"cancelled\"}");else rec_error(fd,409,"Absence not found or already cancelled.");return;
 }
 if(!form_value(body,"employee_id",emp,sizeof emp)||!form_value(body,"day",date,sizeof date)||!form_value(body,"reason",reason,sizeof reason)||!form_value_multiline(body,"note",note,sizeof note)||!form_value(body,"starts",start,sizeof start)||!form_value(body,"ends",end,sizeof end)){rec_error(fd,400,"Complete the absence form.");return;}
 int valid_reason=!strcmp(reason,"Sick")||!strcmp(reason,"Medical appointment")||!strcmp(reason,"Personal day")||!strcmp(reason,"Family emergency")||!strcmp(reason,"Bereavement")||!strcmp(reason,"Unpaid leave")||!strcmp(reason,"Unexcused absence")||!strcmp(reason,"Other");
 char *tail=NULL;long employee=strtol(emp,&tail,10);
 if(employee<=0||*tail||!rec_date(date)||!valid_reason||strlen(note)>220||(!strcmp(reason,"Other")&&strlen(note)<5)){rec_error(fd,400,"Select an employee, valid date and reason.");return;}
 int partial=start[0]||end[0];
 int time_ok=!partial || (strlen(start)==5&&strlen(end)==5&&start[2]==':'&&end[2]==':'&&start[0]>='0'&&start[0]<='2'&&end[0]>='0'&&end[0]<='2'&&start[1]>='0'&&start[1]<='9'&&end[1]>='0'&&end[1]<='9'&&start[3]>='0'&&start[3]<='5'&&end[3]>='0'&&end[3]<='5'&&start[4]>='0'&&start[4]<='9'&&end[4]>='0'&&end[4]<='9'&&((start[0]-'0')*10+start[1]-'0')<24&&((end[0]-'0')*10+end[1]-'0')<24&&strcmp(start,end)<0);
 if(!time_ok){rec_error(fd,400,"Enter a valid partial-day time range.");return;}
 sqlite3_stmt*q=NULL;int ok=0;
 if(!sql("BEGIN IMMEDIATE")){rec_error(fd,503,"The clock is busy. Try again.");return;}
 ok=sqlite3_prepare_v2(db,"INSERT INTO attendance_absences(employee_id,day,reason,note,starts,ends,manager_id) SELECT id,?2,?3,?4,?5,?6,?7 FROM employees WHERE id=?1 AND active=1 AND NOT EXISTS(SELECT 1 FROM attendance_absences a WHERE a.employee_id=?1 AND a.day=?2 AND a.cancelled_at IS NULL AND (a.starts='' OR ?5='' OR a.starts<?6 AND ?5<a.ends))",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,(int)employee);sqlite3_bind_text(q,2,date,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,reason,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,4,note,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,5,start,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,6,end,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,7,who);ok=sqlite3_step(q)==SQLITE_DONE && sqlite3_changes(db)==1;}sqlite3_finalize(q);q=NULL;
 sqlite3_int64 id=sqlite3_last_insert_rowid(db);
 if(ok)ok=sqlite3_prepare_v2(db,"INSERT INTO attendance_absence_audit(absence_id,manager_id,action,reason) VALUES(?1,?2,'create',?3)",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int64(q,1,id);sqlite3_bind_int(q,2,who);sqlite3_bind_text(q,3,reason,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");if(ok)send_response(fd,200,"application/json","{\"status\":\"saved\"}");else rec_error(fd,409,"Employee not found or an absence already covers this time.");
}
static void rec_page(int fd){send_response(fd,200,"text/html; charset=utf-8",UI_HEAD("Punches and corrections","corrections")
 "<h1>Punches and corrections</h1><p>Times are local. Every correction is logged and the original swipe is kept.</p><p id='punchbanner' role='alert' hidden></p>"
 "<section><div class='actions'><a href='/pay-periods'>Pay periods</a> <a href='/attendance-export'>Attendance export</a></div><label>Employee <select id='employee'></select></label><div class='actions'><button type='button' id='add'>Add missed punch</button></div><p id='status' role='status'></p></section>"
 "<section><h2>Recorded absences</h2><p>Absences explain missed attendance without creating punches or paid hours.</p><form id='absenceForm'><label>Date<input type='date' name='day' required></label><label>Reason<select name='reason'><option>Sick</option><option>Medical appointment</option><option>Personal day</option><option>Family emergency</option><option>Bereavement</option><option>Unpaid leave</option><option>Unexcused absence</option><option>Other</option></select></label><div class='actions'><label>From (optional)<input type='time' name='starts'></label><label>To (optional)<input type='time' name='ends'></label></div><label>Manager note<textarea name='note' maxlength='220'></textarea><small class='muted'>Don't enter medical details.</small></label><button type='submit'>Record absence</button><p id='absenceStatus' role='status'></p></form><div class='table-scroll'><table class='absences-table'><thead><tr><th>Employee</th><th>Date</th><th>Reason</th><th>Hours</th><th>Note</th><th>Status</th><th></th></tr></thead><tbody id='absenceRows'></tbody></table></div></section>"
 "<section><h2>Punches</h2><table class='punch-review'><thead><tr><th>Employee</th><th>Time</th><th>Punch</th><th>Source</th><th>Employee note</th><th>Actions</th></tr></thead><tbody id='rows'></tbody></table></section>"
 "<dialog id='editor'><form id='editform'><h2 id='heading'>Punch correction</h2><input type='hidden' name='action' id='action'><input type='hidden' name='event_id' id='event_id'><input type='hidden' name='employee_id' id='employee_id'><label id='whenlabel'>Local date and time<input id='local_at' type='datetime-local'></label><label id='kindlabel'>Direction<select name='kind' id='kind'><option>IN</option><option>OUT</option></select></label><label>Reason<textarea name='reason' required minlength='5' maxlength='240'></textarea></label><p id='editstatus' role='status' aria-live='polite'></p><div class='actions'><button type='button' id='cancel'>Cancel</button><button type='submit' id='save'>Save correction</button></div></form></dialog>"
 "<script>"
 "const emp=document.getElementById('employee'),rows=document.getElementById('rows'),dialog=document.getElementById('editor'),form=document.getElementById('editform'),msg=document.getElementById('status'),editstatus=document.getElementById('editstatus'),save=document.getElementById('save');let punches=[];"
 "const api=tc.api;"
 "function localValue(s){let d=new Date(s);if(!Number.isFinite(d.getTime()))return '';let z=n=>String(n).padStart(2,'0');return d.getFullYear()+'-'+z(d.getMonth()+1)+'-'+z(d.getDate())+'T'+z(d.getHours())+':'+z(d.getMinutes())}"
 "function labelTime(s){let d=new Date(s);return Number.isFinite(d.getTime())?tc.datetime(d):s}"
 "function openEdit(action,p){form.reset();editstatus.textContent='';save.disabled=false;form.elements.action.value=action;form.elements.employee_id.value=p?p.employee_id:emp.value;form.elements.event_id.value=p?p.id:'';heading.textContent=action==='add'?'Add missed punch':action==='change'?'Change punch':'Void punch';whenlabel.hidden=action==='void';kindlabel.hidden=action==='void';local_at.required=action!=='void';local_at.value=p?localValue(p.at):localValue(new Date().toISOString());if(p)kind.value=p.kind;dialog.showModal()}"
 "function flags(list){let out=new Map(),by=new Map();for(let p of list){if(!by.has(p.employee_id))by.set(p.employee_id,[]);by.get(p.employee_id).push(p)}"
 "for(let rows of by.values()){rows.sort((a,b)=>a.at<b.at?-1:a.at>b.at?1:0);rows.forEach((p,i)=>{let f=[],next=rows[i+1],prev=rows[i-1];"
 "if(p.kind==='IN'&&((next&&next.kind==='IN')||(!next&&Date.now()-new Date(p.at).getTime()>72000000)))f.push(['No OUT','problem']);"
 "if(p.kind==='OUT'&&prev&&prev.kind==='OUT')f.push(['No IN','problem']);if(p.late>0)f.push(['Late '+p.late+' min','check']);"
 "if(p.manual)f.push(['Added by manager','check']);else if(p.corrected)f.push(['Corrected','check']);out.set(p.id,f)})}return out}"
 "function draw(){rows.replaceChildren();let all=flags(punches);for(let p of punches.filter(p=>String(p.employee_id)===emp.value)){let tr=document.createElement('tr'),f=all.get(p.id)||[];for(let v of [p.name,labelTime(p.at),p.kind,p.manual?'Manager':'Card',p.note||'']){let td=document.createElement('td');td.textContent=v;tr.append(td)}for(let [t,l] of f)tr.children[2].append(' ',tc.flag(t,l));if(f.some(x=>x[1]==='problem'))tr.className='row-problem';else if(f.length)tr.className='row-check';let td=document.createElement('td');for(let [name,op] of [['Change','change'],['Void','void']]){let b=document.createElement('button');b.type='button';b.className='secondary';b.textContent=name;b.onclick=()=>openEdit(op,p);td.append(b,' ')}tr.append(td);rows.append(tr)}if(!rows.children.length){let tr=document.createElement('tr'),td=document.createElement('td');td.colSpan=6;td.textContent='No punches.';tr.append(td);rows.append(tr)}"
 "let count=new Map();for(let p of punches)if((all.get(p.id)||[]).some(x=>x[1]==='problem'))count.set(p.name,(count.get(p.name)||0)+1);let n=[...count.values()].reduce((a,b)=>a+b,0);"
 "tc.banner(document.getElementById('punchbanner'),'problem','Problem',n?n+(n===1?' punch has':' punches have')+' no matching IN or OUT: '+[...count].map(x=>x[0]+' ('+x[1]+')').join(', ')+'.':'');"
 "for(let o of emp.options){let c=[...punches].filter(p=>String(p.employee_id)===o.value&&(all.get(p.id)||[]).some(x=>x[1]==='problem')).length;o.textContent=o.dataset.name+(c?' ('+c+' to fix)':'')}}"
 "async function refresh(){let [a,b]=await Promise.all([api('/api/v1/admin/employees'),api('/api/v1/admin/punches')]);let chosen=emp.value;emp.replaceChildren();for(let e of a.employees){let o=document.createElement('option');o.value=e.id;o.dataset.name=[e.first_name,e.last_name].filter(Boolean).join(' ')||e.name||'Employee '+e.id;o.textContent=o.dataset.name;emp.append(o)}punches=b.punches;if([...emp.options].some(o=>o.value===chosen))emp.value=chosen;else{let all=flags(punches),first=punches.find(p=>(all.get(p.id)||[]).some(x=>x[1]==='problem'))||punches[0];if(first)emp.value=String(first.employee_id)}draw()}"
 "emp.onchange=draw;add.onclick=()=>openEdit('add',null);cancel.onclick=()=>dialog.close();form.onsubmit=async e=>{e.preventDefault();let data=new URLSearchParams(new FormData(form));if(data.get('action')==='void'){let p=punches.find(x=>String(x.id)===data.get('event_id'));if(!p){tc.err(editstatus,'This punch no longer exists. Reload the page.');return}data.set('at',p.at);data.set('kind',p.kind)}else{let d=new Date(local_at.value);if(!Number.isFinite(d.getTime())){tc.err(editstatus,'Enter a valid date and time.');return}data.set('at',d.toISOString().slice(0,19)+'Z')}if(!confirm('Save correction?'))return;save.disabled=true;editstatus.textContent='Saving correction...';try{await api('/api/v1/admin/punches',{method:'POST',body:data});dialog.close();tc.ok(msg,'Correction saved.');await refresh()}catch(e){tc.err(editstatus,e)}finally{save.disabled=false}};"
 "const absenceForm=document.getElementById('absenceForm'),absenceRows=document.getElementById('absenceRows'),absenceStatus=document.getElementById('absenceStatus');"
 "async function loadAbsences(){let data=await api('/api/v1/admin/absences');absenceRows.replaceChildren();for(let a of data.absences.filter(x=>String(x.employee_id)===emp.value)){let tr=document.createElement('tr');for(let v of [a.employee,a.day,a.reason,a.starts?a.starts+' to '+a.ends:'Full day',a.note,a.cancelled?'Cancelled':a.has_punches?'Punches recorded':'Recorded']){let td=document.createElement('td');if(a.has_punches&&!a.cancelled&&v==='Punches recorded'){td.append(tc.flag('Punches recorded','check'));tr.className='row-check'}else td.textContent=v;tr.append(td)}let td=document.createElement('td');if(!a.cancelled){let b=document.createElement('button');b.type='button';b.textContent='Cancel';b.onclick=async()=>{let note=prompt('Reason for cancelling absence (at least 5 characters):');if(!note)return;try{await api('/api/v1/admin/absences',{method:'POST',body:new URLSearchParams({action:'cancel',id:a.id,note})});tc.ok(absenceStatus,'Absence cancelled.');await loadAbsences()}catch(e){tc.err(absenceStatus,e)}};td.append(b)}tr.append(td);absenceRows.append(tr)}if(!absenceRows.children.length){let tr=document.createElement('tr'),td=document.createElement('td');td.colSpan=6;td.textContent='No recorded absences.';tr.append(td);absenceRows.append(tr)}}"
 "absenceForm.onsubmit=async e=>{e.preventDefault();let d=new URLSearchParams(new FormData(absenceForm));d.set('employee_id',emp.value);try{await api('/api/v1/admin/absences',{method:'POST',body:d});tc.ok(absenceStatus,'Absence recorded.');absenceForm.reset();await loadAbsences()}catch(e){tc.err(absenceStatus,e)}};emp.addEventListener('change',()=>loadAbsences().catch(e=>tc.err(absenceStatus,e)));loadAbsences().catch(e=>tc.err(absenceStatus,e));"
 "refresh().then(loadAbsences).catch(e=>tc.err(msg,e))"
 "</script>"
 UI_FOOT);}
static void rec_presence_page(int fd){send_response(fd,200,"text/html; charset=utf-8",UI_HEAD("Who is in","presence")"<h1>Who is in</h1><p>Estimate from the latest punches. Not an evacuation roll count. Updates every 10 seconds.</p><section><h2 id='total'>Loading...</h2><table><thead><tr><th>Employee</th><th>In since</th><th>Check</th></tr></thead><tbody id='rows'></tbody></table><p id='status' role='status'></p></section><script>const status=document.getElementById('status');async function refresh(){try{let r=await tc.request('/api/v1/admin/presence'),j=await tc.json(r);if(!r.ok)throw Error(j.error||'Unavailable');total.textContent=j.count+' in';status.textContent='';rows.replaceChildren();for(let p of j.people){let tr=document.createElement('tr');let when=new Date(p.since),hours=(Date.now()-when.getTime())/3600000;let time=Number.isNaN(when.getTime())?p.since:tc.datetime(p.since);for(let v of [p.name,time]){let td=document.createElement('td');td.textContent=v;tr.append(td)}let c=document.createElement('td');if(hours>=16){c.append(tc.flag('In '+Math.floor(hours)+' h','check'),' Confirm they are here');tr.className='row-check'}tr.append(c);rows.append(tr)}}catch(e){tc.err(status,e)}}refresh();setInterval(refresh,10000)</script>"UI_FOOT);}
/* Manager-initiated queue request, deduplicated by the payroll event ID. */
static void rec_period_send_report(int fd,int who,const char*body){
 char idbuf[24]="";if(!who||!form_value(body,"period_id",idbuf,sizeof idbuf)){rec_error(fd,400,"Choose a closed period.");return;}
 for(const char*c=idbuf;*c;c++)if(*c<'0'||*c>'9'){rec_error(fd,400,"Invalid period.");return;}
 int id=atoi(idbuf);if(id<=0){rec_error(fd,400,"Invalid period.");return;}
 sqlite3_stmt*auth=NULL;int manager=0;
 if(sqlite3_prepare_v2(db,"SELECT 1 FROM employee_roles WHERE employee_id=?1 AND role='manager'",-1,&auth,NULL)==SQLITE_OK){sqlite3_bind_int(auth,1,who);manager=sqlite3_step(auth)==SQLITE_ROW;}
 sqlite3_finalize(auth);if(!manager){rec_error(fd,403,"Manager access required.");return;}
 sqlite3_stmt*q=NULL;int exists=0,already=0,configured=0,revision=0;
 if(sqlite3_prepare_v2(db,"SELECT 1 FROM pay_periods WHERE id=?1 AND status='closed'",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,id);exists=sqlite3_step(q)==SQLITE_ROW;}sqlite3_finalize(q);
 if(sqlite3_prepare_v2(db,"SELECT coalesce(max(revision),0) FROM lite_period_snapshots WHERE period_id=?1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,id);if(sqlite3_step(q)==SQLITE_ROW)revision=sqlite3_column_int(q,0);}sqlite3_finalize(q);
 char event[100];if(revision>1)snprintf(event,sizeof event,"payroll-period:%d:revision:%d",id,revision);else snprintf(event,sizeof event,"payroll-period:%d",id);
 if(sqlite3_prepare_v2(db,"SELECT 1 FROM email_queue WHERE event_id=?1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,event,-1,SQLITE_TRANSIENT);already=sqlite3_step(q)==SQLITE_ROW;}sqlite3_finalize(q);
 if(sqlite3_prepare_v2(db,"SELECT 1 FROM payroll_settings WHERE id=1 AND instr(recipient,'@')>1",-1,&q,NULL)==SQLITE_OK){configured=sqlite3_step(q)==SQLITE_ROW;}sqlite3_finalize(q);
 if(!exists||already||!configured){rec_error(fd,409,!exists?"Close the period before sending.":already?"This report revision is already in the email queue.":"Configure the report recipient under Payroll report first.");return;}
 if(sqlite3_prepare_v2(db,"INSERT OR IGNORE INTO payroll_manual_requests(period_id) VALUES(?1)",-1,&q,NULL)!=SQLITE_OK){rec_error(fd,500,"Cannot queue request.");return;}
 sqlite3_bind_int(q,1,id);int ok=sqlite3_step(q)==SQLITE_DONE;sqlite3_finalize(q);
 if(!ok){rec_error(fd,500,"Cannot queue request.");return;}
 send_response(fd,200,"application/json","{\"queued\":true}");
}

/* Immutable Lite exports: revision creation is explicit and never sends email. */
static int lite_export_period(int fd,const char*idtext,int *id,char start[11],char end[11]){
 if(!idtext||!*idtext||strlen(idtext)>9){rec_error(fd,400,"Invalid period ID.");return 0;}
 for(const char*c=idtext;*c;c++)if(*c<'0'||*c>'9'){rec_error(fd,400,"Invalid period ID.");return 0;}
 *id=atoi(idtext);if(*id<1){rec_error(fd,400,"Invalid period ID.");return 0;}
 sqlite3_stmt*q=NULL;int found=0;
 if(sqlite3_prepare_v2(db,"SELECT starts,ends FROM pay_periods WHERE id=?1 AND status='closed'",-1,&q,NULL)==SQLITE_OK){
 sqlite3_bind_int(q,1,*id);if(sqlite3_step(q)==SQLITE_ROW){snprintf(start,11,"%s",op_text(q,0));snprintf(end,11,"%s",op_text(q,1));found=1;}}
 sqlite3_finalize(q);
 if(!found){rec_error(fd,409,"Close the period before exporting.");}
 return found;
}
static void lite_export_list(int fd,const char*query){
 char idtext[24]="",starts[11]="",ends[11]="";int id=0;
 if(!form_value(query,"period_id",idtext,sizeof idtext)||!lite_export_period(fd,idtext,&id,starts,ends))return;
 sqlite3_str*o=sqlite3_str_new(db);sqlite3_str_appendall(o,"{\"revisions\":[");
 sqlite3_stmt*q=NULL;int first=1;
 if(sqlite3_prepare_v2(db,"SELECT revision,created_at FROM lite_period_snapshots WHERE period_id=?1 ORDER BY revision",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,id);while(sqlite3_step(q)==SQLITE_ROW){
 if(!first){sqlite3_str_appendall(o,",");}first=0;
 sqlite3_str_appendf(o,"{\"revision\":%d,\"created_at\":\"%s\"}",sqlite3_column_int(q,0),op_text(q,1));}}
 sqlite3_finalize(q);sqlite3_str_appendall(o,"]}");
 char*result=sqlite3_str_finish(o);if(!result){rec_error(fd,500,"Cannot read exports.");return;}send_response(fd,200,"application/json",result);sqlite3_free(result);
}
static void lite_export_create(int fd,const char*body){
 char idtext[24]="",starts[11]="",ends[11]="";int id=0;
 if(!form_value(body,"period_id",idtext,sizeof idtext)||!lite_export_period(fd,idtext,&id,starts,ends))return;
 char *report=NULL,*csv=NULL;
 if(!payroll_build_timesheet(db,starts,ends,&report,&csv)){rec_error(fd,500,"Cannot calculate timesheet.");return;}
 sqlite3_free(report);
 if(sqlite3_exec(db,"SAVEPOINT lite_export_create",NULL,NULL,NULL)!=SQLITE_OK){sqlite3_free(csv);rec_error(fd,500,"Cannot start export.");return;}
 sqlite3_stmt*q=NULL;int revision=0,unchanged=0,ok=1;
 if(sqlite3_prepare_v2(db,"SELECT revision,csv=?2 FROM lite_period_snapshots WHERE period_id=?1 ORDER BY revision DESC LIMIT 1",-1,&q,NULL)==SQLITE_OK){
 sqlite3_bind_int(q,1,id);sqlite3_bind_text(q,2,csv,-1,SQLITE_TRANSIENT);
 if(sqlite3_step(q)==SQLITE_ROW){revision=sqlite3_column_int(q,0);unchanged=sqlite3_column_int(q,1);}}
 else {ok=0;}sqlite3_finalize(q);q=NULL;
 if(ok&&!unchanged&&sqlite3_prepare_v2(db,"INSERT INTO lite_period_snapshots(period_id,revision,starts,ends,csv) VALUES(?1,?2,?3,?4,?5)",-1,&q,NULL)==SQLITE_OK){
 sqlite3_bind_int(q,1,id);sqlite3_bind_int(q,2,revision+1);sqlite3_bind_text(q,3,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,4,ends,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,5,csv,-1,SQLITE_TRANSIENT);
 ok=sqlite3_step(q)==SQLITE_DONE&&sqlite3_changes(db)==1;
 }else if(!unchanged)ok=0;
 sqlite3_finalize(q);sqlite3_free(csv);
 if(ok&&!unchanged&&sqlite3_exec(db,"RELEASE SAVEPOINT lite_export_create",NULL,NULL,NULL)==SQLITE_OK){
 char result[100];snprintf(result,sizeof result,"{\"revision\":%d}",revision+1);send_response(fd,200,"application/json",result);return;}
 sqlite3_exec(db,"ROLLBACK TO SAVEPOINT lite_export_create",NULL,NULL,NULL);sqlite3_exec(db,"RELEASE SAVEPOINT lite_export_create",NULL,NULL,NULL);
 rec_error(fd,unchanged?409:500,unchanged?"No changes since the latest export.":"Could not preserve this export.");
}
/* Generate XLSX using the same trusted converter as the SMTP mailer. */
static void lite_export_download(int fd,const char*query){
 char idtext[24]="",revtext[24]="",starts[11]="",ends[11]="";int id=0;
 if(!form_value(query,"period_id",idtext,sizeof idtext)||!form_value(query,"revision",revtext,sizeof revtext)||!lite_export_period(fd,idtext,&id,starts,ends))return;
 for(const char*c=revtext;*c;c++)if(*c<'0'||*c>'9'){rec_error(fd,400,"Invalid revision.");return;}
 int revision=strlen(revtext)<=9?atoi(revtext):0;if(revision<1){rec_error(fd,400,"Invalid revision.");return;}
 sqlite3_stmt*q=NULL;char *csv=NULL;
 if(sqlite3_prepare_v2(db,"SELECT csv FROM lite_period_snapshots WHERE period_id=?1 AND revision=?2",-1,&q,NULL)==SQLITE_OK){
 sqlite3_bind_int(q,1,id);sqlite3_bind_int(q,2,revision);if(sqlite3_step(q)==SQLITE_ROW){const char*v=op_text(q,0);csv=strdup(v);}}
 sqlite3_finalize(q);if(!csv){rec_error(fd,404,"Export revision not found.");return;}
 char input[]="/tmp/lite-input-XXXXXX",output[]="/tmp/lite-output-XXXXXX";
 int fi=mkstemp(input),fo=mkstemp(output);int ok=fi>=0&&fo>=0;
 if(fi>=0){fchmod(fi,0600);}
 if(fo>=0){fchmod(fo,0600);close(fo);}
 if(ok){size_t len=strlen(csv),written=0;while(written<len){ssize_t n=write(fi,csv+written,len-written);if(n<=0){ok=0;break;}written+=(size_t)n;}}
 if(fi>=0){close(fi);}
 free(csv);
 if(ok){const char *script=access("/opt/icubedev-timeclock/bin/timeclock-payroll-xlsx.py",R_OK)==0?"/opt/icubedev-timeclock/bin/timeclock-payroll-xlsx.py":"timeclock-payroll-xlsx.py";
 struct sigaction old_chld,new_chld;memset(&new_chld,0,sizeof new_chld);new_chld.sa_handler=SIG_DFL;sigemptyset(&new_chld.sa_mask);
 sigaction(SIGCHLD,&new_chld,&old_chld);
 pid_t pid=fork();if(pid==0){execlp("python3","python3",script,input,output,starts,ends,(char*)NULL);_exit(127);}
 int status=0;pid_t waited=pid>0?waitpid(pid,&status,0):-1;sigaction(SIGCHLD,&old_chld,NULL);ok=pid>0&&waited==pid&&WIFEXITED(status)&&WEXITSTATUS(status)==0;}
 unlink(input);
 struct stat st;if(ok)ok=stat(output,&st)==0&&st.st_size>0&&st.st_size<=12000000;
 if(!ok){unlink(output);rec_error(fd,500,"Excel generation failed.");return;}
 int f=open(output,O_RDONLY);if(f<0){unlink(output);rec_error(fd,500,"Cannot open export.");return;}
 size_t len=(size_t)st.st_size;char*bytes=malloc(len);ok=bytes!=NULL;
 if(ok){size_t got=0;while(got<len){ssize_t n=read(f,bytes+got,len-got);if(n<=0){ok=0;break;}got+=(size_t)n;}}
 close(f);unlink(output);if(!ok){free(bytes);rec_error(fd,500,"Cannot read export.");return;}
 char filename[120],header[700];snprintf(filename,sizeof filename,"Timesheet_%s_to_%s_rev%d.xlsx",starts,ends,revision);
 int n=snprintf(header,sizeof header,"HTTP/1.1 200 OK\r\nContent-Type: application/vnd.openxmlformats-officedocument.spreadsheetml.sheet\r\nContent-Disposition: attachment; filename=\"%s\"\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n",filename,len);
 long long deadline=now_ms()+60000;if(n>0&&(size_t)n<sizeof header&&op_send_all(fd,header,(size_t)n,deadline))op_send_all(fd,bytes,len,deadline);
 free(bytes);
}
static void rec_period_page(int fd){send_response(fd,200,"text/html; charset=utf-8",UI_HEAD("Pay periods","periods")
 "<h1>Pay periods</h1><p id='uncoveredWarning' role='alert' hidden></p><p id='cyclebanner' role='alert' hidden></p><p id='cycleMismatch' role='alert' hidden></p>"
 "<section><h2>Pay schedule</h2><form id='settings'><label>Frequency<select name='cycle' id='cycle'><option value='semi_monthly'>Twice monthly (1st to 15th, 16th to last day)</option><option value='biweekly'>Every two weeks</option><option value='weekly'>Weekly</option><option value='monthly'>Monthly</option></select></label><label id='anchorLabel'>First period start<input type='date' name='anchor' id='anchor' required></label><div class='actions'><button>Save pay schedule</button></div></form><p class='muted'>Current and future pay periods are created automatically. Closed and reviewed periods are preserved.</p></section>"
 "<section><h2>Periods</h2><table class='periods-table'><thead><tr><th>Start</th><th>End</th><th>Status</th><th>Approved</th><th></th></tr></thead><tbody id='rows'></tbody></table><p id='status' role='status'></p></section>"
 "<section><h2>Historical Excel exports</h2><p>Close a period before exporting. Existing revisions are never changed, and revisions do not resend email.</p><label>Pay period<select id='exportPeriod'></select></label><button type='button' id='exportCreate'>Create export / revision</button><ul id='exportVersions'></ul><p id='exportStatus' role='status'></p></section>"
 "<section><h2 id='detailTitle'>Select a pay period</h2><table><thead><tr><th>Employee</th><th>Shifts</th><th>Incomplete shifts</th><th>Manager</th><th>Hours</th><th></th></tr></thead><tbody id='people'><tr><td colspan='7'>Select Review on a period above.</td></tr></tbody></table><p id='closeReason' role='status'></p><div class='actions'><button type='button' id='closePeriod' disabled>Close period</button><a href='/punch-corrections'>Punches and corrections</a><a href='/attendance-export'>Attendance export</a></div></section>"
 "<script>let selected=null,periods=[];const message=document.getElementById('status');const api=tc.api;function post(url,data){return api(url,{method:'POST',body:new URLSearchParams(data)})}async function refresh(){let res=await api('/api/v1/admin/periods');periods=res.periods;tc.banner(document.getElementById('uncoveredWarning'),'problem','Problem',[res.uncovered_punches?res.uncovered_punches+' punches are outside all pay periods. Check payroll records.':'',''].filter(Boolean).join(' '));let old=exportPeriod.value;exportPeriod.replaceChildren();for(let p of periods.filter(p=>p.status==='closed')){let o=document.createElement('option');o.value=p.id;o.textContent=p.starts+' to '+p.ends;exportPeriod.append(o)}if([...exportPeriod.options].some(o=>o.value===old))exportPeriod.value=old;await loadVersions();rows.replaceChildren();let today=new Date().toLocaleDateString('en-CA'),due=0;for(let p of periods){let tr=document.createElement('tr'),ended=p.ends<today;tr.dataset.periodId=p.id;for(let value of [p.starts,p.ends,'',p.manager_signoffs+' of '+p.approval_total]){let td=document.createElement('td');td.textContent=value;tr.append(td)}if(ended&&p.has_activity&&p.manager_signoffs<p.approval_total){tr.children[3].append(' ',tc.flag('Needs approval','check'));tr.className='row-check'}if(p.status==='closed'){tr.children[2].append(tc.flag('Closed','muted'));if(p.changed_after_close){let f=tc.flag('Changed after close','problem');f.dataset.changedAfterClose='1';tr.children[2].append(f)}if(p.new_export_required)tr.children[2].append(tc.flag('New export required','problem'));if(p.export_stale)tr.children[2].append(tc.flag('Changed since revision '+p.latest_revision,'problem'));if(p.export_check_failed)tr.children[2].append(tc.flag('Cannot check latest export','problem'));if(p.queue_failure)tr.children[2].append(tc.flag('Report not queued: '+p.queue_failure,'problem'));}else if(ended&&p.has_activity){tr.children[2].append(tc.flag('Ended, not closed','problem'));tr.className='row-problem';due++}else if(ended){tr.children[2].textContent='Ended (empty)'}else tr.children[2].append(tc.flag('Open','ok'));let td=document.createElement('td');if(p.status==='closed'){let send=document.createElement('button');send.type='button';send.textContent='Send report';send.onclick=async()=>{if(!confirm('Queue report for '+p.starts+' to '+p.ends+'?'))return;try{await post('/api/v1/admin/periods/send-report',{period_id:p.id});tc.ok(message,'Report requested. See Email queue.')}catch(x){tc.err(message,x)}};td.append(send)}let b=document.createElement('button');b.type='button';b.textContent='Review';b.onclick=()=>selectPeriod(p);td.append(b);tr.append(td);rows.append(tr)}tc.banner(document.getElementById('cyclebanner'),'problem','Problem',due?due+(due===1?' pay period has':' pay periods have')+' ended without being closed.':'');if(selected){let match=periods.find(p=>p.id===selected.id);if(match)await selectPeriod(match)}}async function loadVersions(){exportVersions.replaceChildren();if(!exportPeriod.value)return;try{let j=await api('/api/v1/admin/periods/exports?period_id='+encodeURIComponent(exportPeriod.value));for(let x of j.revisions){let li=document.createElement('li'),a=document.createElement('a');a.textContent='Revision '+x.revision+' ('+tc.datetime(x.created_at)+') - Download Excel';a.href='/api/v1/admin/periods/export.xlsx?period_id='+encodeURIComponent(exportPeriod.value)+'&revision='+x.revision;li.append(a);exportVersions.append(li)}}catch(x){tc.err(exportStatus,x)}}exportPeriod.onchange=loadVersions;exportCreate.onclick=async()=>{try{let x=await post('/api/v1/admin/periods/exports',{period_id:exportPeriod.value});tc.ok(exportStatus,'Revision '+x.revision+' preserved. No email sent.');await refresh()}catch(x){tc.err(exportStatus,x)}};async function selectPeriod(p){selected=p;detailTitle.textContent=p.starts+' to '+p.ends+(p.status==='closed'?' (closed)':'');closePeriod.disabled=p.status==='closed';let j=await api('/api/v1/admin/periods/people?period_id='+encodeURIComponent(p.id));people.replaceChildren();let bad=0,unapproved=0;for(let e of j.people){let tr=document.createElement('tr');let cells=[e.name,e.punches,e.unpaired?tc.flag('Fix punches','problem'):'None',e.manager_signed&&e.manager_current?tc.flag(e.unconfirmed?'Manager override':'Approved','ok'):(e.manager_signed?tc.flag('Changed since approval','check'):(e.employee_signed&&e.confirmation_current?tc.flag('Employee approved','ok'):tc.flag('Awaiting approval','check'))),(e.current_seconds/3600).toFixed(2)+' h'+(e.manager_signed&&e.manager_current?' approved':' to approve')];for(let v of cells){let td=document.createElement('td');td.append(...[].concat(v));tr.append(td)}if(e.unpaired)tr.className='row-problem';else if(!e.manager_signed||!e.manager_current)tr.className='row-check';let td=document.createElement('td');if((!e.manager_signed||!e.manager_current)&&!e.unpaired ){let b=document.createElement('button');b.textContent=e.employee_signed&&e.confirmation_current?'Approve':'Manager override';b.onclick=()=>approve(p,e);td.append(b)}tr.append(td);people.append(tr);if(e.unpaired)bad++;if(!e.manager_signed||!e.manager_current)unapproved++}if(!j.people.length){let tr=document.createElement('tr'),td=document.createElement('td');td.colSpan=6;td.textContent='No punches or approved leave in this period.';tr.append(td);people.append(tr)}let reason=document.getElementById('closeReason'),why=[];if(bad)why.push(bad+(bad===1?' employee has':' employees have')+' punch problems');if(unapproved)why.push(unapproved+' not approved');if(p.status==='closed'&&unapproved){let r=[...rows.children].find(r=>r.dataset.periodId===String(p.id));if(r&&!r.querySelector('[data-changed-after-close]')){let x=tc.flag('Changed after close','problem');x.dataset.changedAfterClose='1';r.children[2].append(' ',x);r.classList.add('row-problem')}}if(p.status==='open'){closePeriod.disabled=why.length>0;if(why.length){reason.replaceChildren(tc.flag('Cannot close','problem'),' '+why.join(', ')+'.')}else reason.textContent='Ready to close.'}else reason.textContent=unapproved?'Changed after close. Reapprove affected employees and create a revised export.':''}async function approve(p,e){let override=!(e.employee_signed&&e.confirmation_current);let reason=override?prompt('Reason for manager override (required):','Employee approval unavailable'): 'Employee-approved timesheet reviewed';if(reason===null||reason.trim().length<5)return;if(!confirm((override?'Override employee approval for ':'Approve ')+e.name+'?'))return;try{await post('/api/v1/admin/periods',{action:'sign',starts:p.starts,ends:p.ends,employee_id:e.id,reason});tc.ok(message,'Approved '+e.name+'.');await refresh()}catch(x){tc.err(message,x)}}cycle.onchange=()=>{anchorLabel.hidden=(cycle.value==='semi_monthly'||cycle.value==='monthly')};settings.onsubmit=async ev=>{ev.preventDefault();if(!confirm('Change future payroll period generation? Unused future open periods will be replaced. Reviewed and closed periods stay unchanged.'))return;try{await post('/api/v1/admin/periods/cycle',{...Object.fromEntries(new FormData(settings))});tc.ok(message,'Pay schedule saved.');await refresh()}catch(x){tc.err(message,x)}};closePeriod.onclick=async()=>{let p=selected;if(!p||p.status==='closed'||!confirm('Close '+p.starts+' to '+p.ends+'? Later corrections require a new payroll export revision.'))return;let reason=prompt('Reason for closing this pay period:','Timesheets reviewed and approved');if(!reason)return;try{await post('/api/v1/admin/periods',{action:'close',starts:p.starts,ends:p.ends,employee_id:0,reason});tc.ok(message,'Period closed.');await refresh()}catch(x){tc.err(message,x)}};(async()=>{try{let c=await api('/api/v1/admin/periods/cycle');cycle.value=c.cycle;anchor.value=c.anchor;anchorLabel.hidden=(c.cycle==='semi_monthly'||c.cycle==='monthly');await refresh()}catch(x){tc.err(message,x)}})();</script>" UI_FOOT);}

