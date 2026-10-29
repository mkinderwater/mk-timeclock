/* RC4.3 active-shift rule, shared by period review and employee links. */
#define REVIEW_ACTIVE_AT_SQL(x,status,at) "(" status "='open' AND " x ".state='missing_out' AND (" at "-CAST(strftime('%s'," x ".in_at) AS INTEGER)) BETWEEN 0 AND 71999 AND NOT EXISTS(SELECT 1 FROM effective_punches later WHERE later.employee_id=" x ".employee_id AND (later.at>" x ".in_at OR (later.at=" x ".in_at AND later.id>" x ".in_id))))"
#define REVIEW_ACTIVE_SQL(x,status) REVIEW_ACTIVE_AT_SQL(x,status,"CAST(strftime('%s','now') AS INTEGER)")
#include "payroll_timesheet.h"
/* v0.9.43: audited attendance corrections and closed periods. Original swipes never modified. */
static int rec_schema(void){int ok=sql(
 "DROP TRIGGER IF EXISTS period_signoff_removed;DROP TRIGGER IF EXISTS period_signoff_revision;"
 "CREATE TABLE IF NOT EXISTS punch_corrections(id INTEGER PRIMARY KEY,employee_id INTEGER NOT NULL REFERENCES employees(id),event_id TEXT UNIQUE,manual_id TEXT UNIQUE,action TEXT NOT NULL CHECK(action IN ('add','change','void')),effective_at TEXT NOT NULL,kind TEXT NOT NULL CHECK(kind IN ('IN','OUT')),reason TEXT NOT NULL CHECK(length(trim(reason))>=5),late_minutes INTEGER NOT NULL DEFAULT 0,early_minutes INTEGER,departure_finish INTEGER NOT NULL DEFAULT 0,departure_due INTEGER NOT NULL DEFAULT 0,manager_id INTEGER NOT NULL REFERENCES employees(id),created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
 "CREATE TABLE IF NOT EXISTS punch_correction_history(id INTEGER PRIMARY KEY,correction_id INTEGER,employee_id INTEGER,event_id TEXT,manual_id TEXT,action TEXT,effective_at TEXT,kind TEXT,reason TEXT,manager_id INTEGER,recorded_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
 "CREATE TRIGGER IF NOT EXISTS correction_history_insert AFTER INSERT ON punch_corrections BEGIN INSERT INTO punch_correction_history(correction_id,employee_id,event_id,manual_id,action,effective_at,kind,reason,manager_id) VALUES(new.id,new.employee_id,new.event_id,new.manual_id,new.action,new.effective_at,new.kind,new.reason,new.manager_id); END;"
 "DROP TRIGGER IF EXISTS correction_history_update;"
 "CREATE TRIGGER correction_history_update AFTER UPDATE ON punch_corrections WHEN old.action IS NOT new.action OR old.effective_at IS NOT new.effective_at OR old.kind IS NOT new.kind OR old.reason IS NOT new.reason OR old.manager_id IS NOT new.manager_id BEGIN INSERT INTO punch_correction_history(correction_id,employee_id,event_id,manual_id,action,effective_at,kind,reason,manager_id) VALUES(new.id,new.employee_id,new.event_id,new.manual_id,new.action,new.effective_at,new.kind,new.reason,new.manager_id); END;"
 "CREATE TABLE IF NOT EXISTS attendance_absences(id INTEGER PRIMARY KEY, employee_id INTEGER NOT NULL REFERENCES employees(id), day TEXT NOT NULL, reason TEXT NOT NULL CHECK(reason IN ('Sick','Medical appointment','Personal day','Family emergency','Bereavement','Unpaid leave','Unexcused absence','Other')), note TEXT NOT NULL DEFAULT '', starts TEXT NOT NULL DEFAULT '', ends TEXT NOT NULL DEFAULT '', manager_id INTEGER NOT NULL REFERENCES employees(id), recorded_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')), cancelled_at TEXT, cancelled_by INTEGER REFERENCES employees(id), CHECK(starts='' OR (length(starts)=5 AND length(ends)=5 AND starts<ends)));"
 "CREATE TABLE IF NOT EXISTS attendance_absence_audit(id INTEGER PRIMARY KEY, absence_id INTEGER NOT NULL REFERENCES attendance_absences(id), manager_id INTEGER NOT NULL REFERENCES employees(id), action TEXT NOT NULL, reason TEXT NOT NULL, recorded_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
 "CREATE INDEX IF NOT EXISTS attendance_absences_employee_day ON attendance_absences(employee_id,day) WHERE cancelled_at IS NULL;"
 "CREATE TABLE IF NOT EXISTS period_cycle_settings(id INTEGER PRIMARY KEY CHECK(id=1),cycle TEXT NOT NULL DEFAULT 'semi_monthly' CHECK(cycle IN ('semi_monthly','biweekly','weekly','monthly')),anchor TEXT NOT NULL DEFAULT '2026-01-01',updated_by INTEGER,updated_at TEXT);"
 "INSERT OR IGNORE INTO period_cycle_settings(id) VALUES(1);"
 "CREATE TABLE IF NOT EXISTS pay_periods(id INTEGER PRIMARY KEY,starts TEXT NOT NULL,ends TEXT NOT NULL,status TEXT NOT NULL DEFAULT 'open' CHECK(status IN ('open','closed')),closed_by INTEGER REFERENCES employees(id),closed_at TEXT,reason TEXT NOT NULL DEFAULT '',closure_method TEXT NOT NULL DEFAULT 'manual' CHECK(closure_method IN ('manual','automatic')),UNIQUE(starts,ends),CHECK(starts<=ends));"
 "CREATE TABLE IF NOT EXISTS period_signoffs(period_id INTEGER NOT NULL REFERENCES pay_periods(id),employee_id INTEGER NOT NULL REFERENCES employees(id),role TEXT NOT NULL CHECK(role IN ('employee','manager')),signed_by INTEGER NOT NULL REFERENCES employees(id),signed_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),punch_count INTEGER NOT NULL DEFAULT 0,approved_seconds INTEGER NOT NULL DEFAULT 0,reason TEXT NOT NULL DEFAULT '',leave_fingerprint TEXT NOT NULL DEFAULT '',PRIMARY KEY(period_id,employee_id,role));"
 "CREATE TABLE IF NOT EXISTS period_signoff_history(id INTEGER PRIMARY KEY,period_id INTEGER NOT NULL,employee_id INTEGER NOT NULL,role TEXT NOT NULL,signed_by INTEGER NOT NULL,signed_at TEXT NOT NULL,punch_count INTEGER NOT NULL,approved_seconds INTEGER NOT NULL,leave_fingerprint TEXT NOT NULL,unconfirmed INTEGER NOT NULL,reason TEXT NOT NULL,superseded_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
 "CREATE TRIGGER IF NOT EXISTS period_signoff_removed BEFORE DELETE ON period_signoffs BEGIN INSERT INTO period_signoff_history(period_id,employee_id,role,signed_by,signed_at,punch_count,approved_seconds,leave_fingerprint,unconfirmed,reason) VALUES(old.period_id,old.employee_id,old.role,old.signed_by,old.signed_at,old.punch_count,old.approved_seconds,old.leave_fingerprint,0,old.reason); END;"
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
 /* Pair consecutive recorded IN/OUT events irrespective of duration.
    Duration limits belong to validation, not the relationship itself. */
 "CREATE VIEW effective_shift_pairs AS WITH ordered AS (SELECT employee_id,id,at,kind,manual,lead(at) OVER (PARTITION BY employee_id ORDER BY at,id) AS next_at,lead(kind) OVER (PARTITION BY employee_id ORDER BY at,id) AS next_kind,lead(id) OVER (PARTITION BY employee_id ORDER BY at,id) AS next_id,lag(kind) OVER (PARTITION BY employee_id ORDER BY at,id) AS prior_kind FROM effective_punches) SELECT employee_id,id AS in_id,at AS in_at,CASE WHEN kind='IN' AND next_kind='OUT' THEN next_at ELSE NULL END AS out_at,CASE WHEN kind='IN' AND next_kind='OUT' THEN next_id ELSE NULL END AS out_id,CASE WHEN kind='IN' AND next_kind='OUT' THEN strftime('%s',next_at)-strftime('%s',at) ELSE 0 END AS seconds,CASE WHEN kind='OUT' THEN 'missing_in' WHEN next_kind='OUT' THEN 'complete' ELSE 'missing_out' END AS state,manual FROM ordered WHERE kind='IN' OR (kind='OUT' AND coalesce(prior_kind,'')<>'IN');"
 "CREATE TRIGGER IF NOT EXISTS period_signoff_revision BEFORE UPDATE ON period_signoffs BEGIN INSERT INTO period_signoff_history(period_id,employee_id,role,signed_by,signed_at,punch_count,approved_seconds,leave_fingerprint,unconfirmed,reason) VALUES(old.period_id,old.employee_id,old.role,old.signed_by,old.signed_at,old.punch_count,old.approved_seconds,old.leave_fingerprint,0,old.reason); END;"
 );
 if(!ok)return 0;
 sqlite3_stmt*q=NULL;int obsolete=0;
 if(sqlite3_prepare_v2(db,"PRAGMA table_info(period_signoffs)",-1,&q,NULL)!=SQLITE_OK)return 0;
 while(sqlite3_step(q)==SQLITE_ROW)if(!strcmp((const char*)sqlite3_column_text(q,1),"unconfirmed"))obsolete=1;
 sqlite3_finalize(q);
 if(obsolete){
  if(!sql("SAVEPOINT remove_confirmation"))return 0;
  ok=sql("INSERT INTO period_signoff_history(period_id,employee_id,role,signed_by,signed_at,punch_count,approved_seconds,leave_fingerprint,unconfirmed,reason) SELECT period_id,employee_id,role,signed_by,signed_at,punch_count,approved_seconds,leave_fingerprint,unconfirmed,reason FROM period_signoffs WHERE unconfirmed<>0;ALTER TABLE period_signoffs DROP COLUMN unconfirmed;");
  if(!ok)sql("ROLLBACK TO remove_confirmation");
  sql("RELEASE remove_confirmation");
 }
 return ok;}
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
static int rec_stamp(const char*s){if(strlen(s)!=20||s[10]!='T'||s[19]!='Z')return 0;char date[11];memcpy(date,s,10);date[10]=0;if(!date_valid(date))return 0;time_t t;struct tm tm;return attendance_stamp(s,&t,&tm);}
static void rec_events(int fd){sqlite3_stmt*q=NULL;const char*query="WITH recent AS MATERIALIZED (SELECT * FROM effective_punches ORDER BY at DESC,id DESC LIMIT 150), pairs AS MATERIALIZED (SELECT * FROM effective_shift_pairs), pair_map AS MATERIALIZED (SELECT in_id AS punch_id,* FROM pairs UNION ALL SELECT out_id AS punch_id,* FROM pairs WHERE out_id IS NOT NULL) SELECT e.id,e.name,p.id,p.at,p.kind,p.manual,coalesce(n.note,''),coalesce((SELECT c.late_minutes FROM punch_corrections c WHERE c.event_id=p.id OR c.manual_id=p.id),a.late_minutes,0),EXISTS(SELECT 1 FROM punch_corrections c WHERE c.event_id=p.id AND c.action='change'),coalesce((SELECT c.early_minutes FROM punch_corrections c WHERE c.event_id=p.id OR c.manual_id=p.id),a.early_minutes,0),date(p.at,'localtime'),CAST(strftime('%H',p.at,'localtime') AS INTEGER)*60+CAST(strftime('%M',p.at,'localtime') AS INTEGER),coalesce(x.state,'')," REVIEW_ACTIVE_SQL("x","'open'") ",coalesce(x.out_at,''),coalesce(x.seconds,0),coalesce(date(x.out_at,'localtime'),''),coalesce(CAST(strftime('%H',x.out_at,'localtime') AS INTEGER)*60+CAST(strftime('%M',x.out_at,'localtime') AS INTEGER),0) FROM recent p LEFT JOIN pair_map x ON x.punch_id=p.id AND x.employee_id=p.employee_id JOIN employees e ON e.id=p.employee_id LEFT JOIN swipe_notes n ON n.event_id=p.id LEFT JOIN attendance_events a ON a.event_id=p.id ORDER BY p.at DESC,p.id DESC LIMIT 150";if(sqlite3_prepare_v2(db,query,-1,&q,NULL)!=SQLITE_OK){rec_error(fd,500,"The clock could not complete this. Try again.");return;}sqlite3_str*s=sqlite3_str_new(db);sqlite3_str_appendall(s,"{\"punches\":[");int n=0,rc;while((rc=sqlite3_step(q))==SQLITE_ROW){if(n++)sqlite3_str_appendchar(s,1,',');sqlite3_str_appendf(s,"{\"employee_id\":%d,\"name\":",sqlite3_column_int(q,0));json_text(s,col_text(q,1));sqlite3_str_appendall(s,",\"id\":");json_text(s,col_text(q,2));sqlite3_str_appendall(s,",\"at\":");json_text(s,col_text(q,3));sqlite3_str_appendall(s,",\"kind\":");json_text(s,col_text(q,4));sqlite3_str_appendf(s,",\"manual\":%s,\"note\":",sqlite3_column_int(q,5)?"true":"false");json_text(s,col_text(q,6));sqlite3_str_appendf(s,",\"late\":%d,\"corrected\":%s,\"early_minutes\":%d,\"day\":",sqlite3_column_int(q,7),sqlite3_column_int(q,8)?"true":"false",sqlite3_column_int(q,9));json_text(s,col_text(q,10));sqlite3_str_appendf(s,",\"min\":%d,\"shift_state\":",sqlite3_column_int(q,11));json_text(s,col_text(q,12));sqlite3_str_appendf(s,",\"active_shift\":%s,\"paired_out_at\":",sqlite3_column_int(q,13)?"true":"false");json_text(s,col_text(q,14));sqlite3_str_appendf(s,",\"shift_seconds\":%lld,\"paired_out_day\":",sqlite3_column_int64(q,15));json_text(s,col_text(q,16));sqlite3_str_appendf(s,",\"paired_out_min\":%d}",sqlite3_column_int(q,17));}sqlite3_finalize(q);
 if(rc!=SQLITE_DONE){sqlite3_free(sqlite3_str_finish(s));rec_error(fd,500,"Attendance could not be loaded. Try again.");return;}
 /* Any unmatched swipe, including one older than the recovery lookback, is a payroll issue. */
 sqlite3_stmt *orphan=NULL;int missing=0;
 rc=sqlite3_prepare_v2(db,"SELECT count(*) FROM effective_punches x WHERE NOT EXISTS(SELECT 1 FROM pay_periods p WHERE date(x.at,'localtime') BETWEEN p.starts AND p.ends)",-1,&orphan,NULL);
 if(rc==SQLITE_OK){rc=sqlite3_step(orphan);if(rc==SQLITE_ROW){missing=sqlite3_column_int(orphan,0);rc=sqlite3_step(orphan);}}
 sqlite3_finalize(orphan);
 if(rc!=SQLITE_DONE){sqlite3_free(sqlite3_str_finish(s));rec_error(fd,500,"Attendance could not be loaded. Try again.");return;}
 sqlite3_str_appendf(s,"],\"uncovered_punches\":%d}",missing);send_str(fd,"application/json",s);}
/* Compare full paired-shift state, not merely elapsed seconds. Changes to an
   OUT in the next period can otherwise silently rewrite closed payroll. */
static void rec_apply(int fd,int who,const char*body){
 char action[16]="",id[100]="",kind[8]="",stamp[32]="",why[260]="",employee_txt[20]="";
 char note_action[16]="";form_value(body,"action",note_action,sizeof note_action);
 if(!strcmp(note_action,"note")){
  char event[90]="",note[241]="";
  if(!form_value(body,"event_id",event,sizeof event)||!valid_id(event,80)||!form_value_multiline(body,"reason",note,sizeof note)||strlen(note)<5||strlen(note)>240){rec_error(fd,400,"Enter a note of 5 to 240 characters.");return;}
  sqlite3_stmt *n=NULL;
  int ok=sqlite3_prepare_v2(db,"INSERT INTO swipe_notes(event_id,note,updated_by) SELECT p.id,?2,?3 FROM effective_punches p JOIN attendance_events a ON a.event_id=p.id WHERE p.id=?1 AND ((p.kind='OUT' AND a.early_minutes>0) OR (p.kind='IN' AND a.late_minutes>0)) ON CONFLICT(event_id) DO UPDATE SET note=excluded.note,updated_by=excluded.updated_by,updated_at=strftime('%Y-%m-%dT%H:%M:%fZ','now')",-1,&n,NULL)==SQLITE_OK;
  if(ok){sqlite3_bind_text(n,1,event,-1,SQLITE_TRANSIENT);sqlite3_bind_text(n,2,note,-1,SQLITE_TRANSIENT);sqlite3_bind_int(n,3,who);ok=sqlite3_step(n)==SQLITE_DONE&&sqlite3_changes(db)>0;}sqlite3_finalize(n);
  if(ok)send_response(fd,200,"application/json","{\"status\":\"saved\"}");else rec_error(fd,400,"Flagged attendance punch not found.");return;
 }

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
  if(sqlite3_prepare_v2(db,"SELECT employee_id,swiped_at FROM swipes WHERE event_id=?1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,id,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW){actual_employee=sqlite3_column_int(q,0);snprintf(old_at,sizeof old_at,"%s",col_text(q,1));snprintf(raw_at,sizeof raw_at,"%s",col_text(q,1));}}sqlite3_finalize(q);q=NULL;
  if(!old_at[0]){
   if(sqlite3_prepare_v2(db,"SELECT employee_id,effective_at FROM punch_corrections WHERE manual_id=?1 AND action<>'void'",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,id,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW){manual_target=1;actual_employee=sqlite3_column_int(q,0);snprintf(old_at,sizeof old_at,"%s",col_text(q,1));}}sqlite3_finalize(q);q=NULL;
  }
  if(!old_at[0]||actual_employee!=(int)employee){sql("ROLLBACK");rec_error(fd,404,"Original or manual punch not found for employee");return;}
  /* An edited event can already have moved to another calendar date. */
  if(sqlite3_prepare_v2(db,"SELECT effective_at FROM punch_corrections WHERE event_id=?1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,id,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW)snprintf(old_at,sizeof old_at,"%s",col_text(q,0));}sqlite3_finalize(q);q=NULL;
 }
 char new_id[65]="";if(!strcmp(action,"add")&&!random_hex(new_id,16))ok=0;
 const char*stmt=manual_target?"UPDATE punch_corrections SET action=?7,effective_at=?3,kind=?4,reason=?5,manager_id=?6 WHERE manual_id=?2 AND employee_id=?1":!strcmp(action,"add")?"INSERT INTO punch_corrections(employee_id,manual_id,action,effective_at,kind,reason,manager_id) VALUES(?1,?2,'add',?3,?4,?5,?6)":"INSERT INTO punch_corrections(employee_id,event_id,action,effective_at,kind,reason,manager_id) VALUES(?1,?2,?7,?3,?4,?5,?6) ON CONFLICT(event_id) DO UPDATE SET action=excluded.action,effective_at=excluded.effective_at,kind=excluded.kind,reason=excluded.reason,manager_id=excluded.manager_id";
 if(ok)ok=sqlite3_prepare_v2(db,stmt,-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,(int)employee);sqlite3_bind_text(q,2,!strcmp(action,"add")?new_id:id,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,stamp,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,4,kind,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,5,why,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,6,who);if(strcmp(action,"add"))sqlite3_bind_text(q,7,action,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 /* Any changed attendance invalidates prior employee and manager attestations.
    Even if total seconds happen to match, the approved punch set has changed. */
 if(ok){
  sqlite3_stmt*invalidate=NULL;
  const char*invalidation="DELETE FROM period_signoffs WHERE employee_id=?1 AND period_id IN (SELECT id FROM pay_periods WHERE (date(?2,'localtime') BETWEEN starts AND ends OR date(?3,'localtime') BETWEEN starts AND ends OR date(?4,'localtime') BETWEEN starts AND ends))";
  ok=sqlite3_prepare_v2(db,invalidation,-1,&invalidate,NULL)==SQLITE_OK;
  if(ok){sqlite3_bind_int(invalidate,1,(int)employee);sqlite3_bind_text(invalidate,2,stamp,-1,SQLITE_TRANSIENT);sqlite3_bind_text(invalidate,3,old_at,-1,SQLITE_TRANSIENT);sqlite3_bind_text(invalidate,4,raw_at,-1,SQLITE_TRANSIENT);ok=sqlite3_step(invalidate)==SQLITE_DONE;}
  sqlite3_finalize(invalidate);
 }
 /* Corrections are authoritative; never retain automatic flags based on the replaced time. */
 if(ok&&strcmp(action,"add")){
  sqlite3_stmt *clear=NULL;
  ok=sqlite3_prepare_v2(db,"UPDATE email_queue SET status='cancelled',last_error='Punch corrected; automatic attendance notice withdrawn' WHERE status='pending' AND (instr(event_id,'late:'||?1||':')=1 OR instr(event_id,'early:'||?1||':')=1 OR (?2='void' AND (event_id IN (SELECT 'early:'||event_id||':employee' FROM attendance_events WHERE paired_in_event=?1) OR event_id IN (SELECT 'early:'||a.event_id||':manager:'||e.id FROM attendance_events a CROSS JOIN employees e WHERE a.paired_in_event=?1))))",-1,&clear,NULL)==SQLITE_OK;
  if(ok){sqlite3_bind_text(clear,1,id,-1,SQLITE_TRANSIENT);sqlite3_bind_text(clear,2,action,-1,SQLITE_TRANSIENT);ok=sqlite3_step(clear)==SQLITE_DONE;}sqlite3_finalize(clear);clear=NULL;
  if(ok)ok=sqlite3_prepare_v2(db,"UPDATE attendance_events SET late_minutes=CASE WHEN event_id=?1 THEN 0 ELSE late_minutes END,early_minutes=0,departure_due=0 WHERE event_id=?1 OR (paired_in_event=?1 AND ?2='void')",-1,&clear,NULL)==SQLITE_OK;
  if(ok){sqlite3_bind_text(clear,1,id,-1,SQLITE_TRANSIENT);sqlite3_bind_text(clear,2,action,-1,SQLITE_TRANSIENT);ok=sqlite3_step(clear)==SQLITE_DONE;}sqlite3_finalize(clear);
 }
 /* Recalculate corrected/manual IN lateness from the effective time, without another late email. */
 if(ok){
  int minutes=0,early=0,grace=5;time_t planned=0,finish=0,due=0;sqlite3_stmt *recalc=NULL;
  const char *target=!strcmp(action,"add")?new_id:id;
  if(strcmp(action,"void")){
   if(sqlite3_prepare_v2(db,"SELECT coalesce((SELECT l.grace_minutes FROM swipes s JOIN clock_late_settings l ON l.clock_id=s.clock_id WHERE s.event_id=?1),5)",-1,&recalc,NULL)==SQLITE_OK){sqlite3_bind_text(recalc,1,target,-1,SQLITE_TRANSIENT);if(sqlite3_step(recalc)==SQLITE_ROW)grace=sqlite3_column_int(recalc,0);}sqlite3_finalize(recalc);recalc=NULL;
   if(!strcmp(kind,"IN"))minutes=attendance_late((int)employee,target,stamp,grace,&planned);
   else{early=attendance_corrected_early((int)employee,target,grace,&finish,0);if(finish&&time(NULL)<=finish+grace*60){due=finish+grace*60;early=0;}}
  }
  ok=sqlite3_prepare_v2(db,"UPDATE punch_corrections SET late_minutes=?2,early_minutes=?3,departure_finish=?4,departure_due=?5 WHERE event_id=?1 OR manual_id=?1",-1,&recalc,NULL)==SQLITE_OK;
  if(ok){sqlite3_bind_text(recalc,1,target,-1,SQLITE_TRANSIENT);sqlite3_bind_int(recalc,2,minutes);sqlite3_bind_int(recalc,3,early);sqlite3_bind_int64(recalc,4,finish);sqlite3_bind_int64(recalc,5,due);ok=sqlite3_step(recalc)==SQLITE_DONE;}sqlite3_finalize(recalc);recalc=NULL;
  if(ok)ok=sqlite3_prepare_v2(db,"UPDATE attendance_events SET late_minutes=?2,early_minutes=?3 WHERE event_id=?1",-1,&recalc,NULL)==SQLITE_OK;
  if(ok){sqlite3_bind_text(recalc,1,target,-1,SQLITE_TRANSIENT);sqlite3_bind_int(recalc,2,minutes);sqlite3_bind_int(recalc,3,early);ok=sqlite3_step(recalc)==SQLITE_DONE;}sqlite3_finalize(recalc);
 }
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");
 /* Notification is best-effort, separate from saving the audit-backed correction. */
 if(ok){
  sqlite3_stmt *notice=NULL;char address[256]="",name[192]="";
  if(sqlite3_prepare_v2(db,"SELECT coalesce(email,''),name FROM employees WHERE id=?1",-1,&notice,NULL)==SQLITE_OK){
   sqlite3_bind_int(notice,1,(int)employee);
   if(sqlite3_step(notice)==SQLITE_ROW){snprintf(address,sizeof address,"%s",col_text(notice,0));snprintf(name,sizeof name,"%s",col_text(notice,1));}
  }sqlite3_finalize(notice);notice=NULL;
  if(email_address_ok(address)){
   long long change_id=0;
   if(sqlite3_prepare_v2(db,"SELECT max(id) FROM punch_correction_history WHERE employee_id=?1",-1,&notice,NULL)==SQLITE_OK){
    sqlite3_bind_int(notice,1,(int)employee);if(sqlite3_step(notice)==SQLITE_ROW)change_id=sqlite3_column_int64(notice,0);
   }sqlite3_finalize(notice);notice=NULL;
   if(change_id>0){
    char event[90],subject[170],message[500];
    snprintf(event,sizeof event,"punch-corrected:%lld",change_id);
    snprintf(subject,sizeof subject,"Timeclock: Your recorded hours were corrected");
    snprintf(message,sizeof message,"Hello %s,\n\nYour manager updated a recorded punch. Please reopen your most recent timesheet link and check your hours again.\n\nIf anything is still wrong, contact your manager.\n",name);
    (void)email_enqueue(db,address,subject,message,event,NULL);
   }
  }
 }
 if(ok)send_response(fd,200,"application/json","{\"status\":\"saved\"}");else rec_error(fd,500,"The clock could not complete this. Try again.");
}
static void rec_effective_export(int fd,const char*path){
 char from[16]="",to[16]="";
 if(!op_query_param(path,"from",from,sizeof from)||!op_query_param(path,"to",to,sizeof to)||!date_valid(from)||!date_valid(to)||strcmp(from,to)>0){rec_error(fd,400,"Choose valid dates.");return;}
 sqlite3_stmt*q=NULL;const char*query="SELECT e.id,e.name,p.id,p.at,p.kind,CASE WHEN p.manual THEN 'manual' ELSE 'rfid' END FROM effective_punches p JOIN employees e ON e.id=p.employee_id WHERE date(p.at,'localtime') BETWEEN ?1 AND ?2 ORDER BY e.name,p.at,p.id LIMIT 100001";
 if(sqlite3_prepare_v2(db,query,-1,&q,NULL)!=SQLITE_OK){rec_error(fd,500,"The clock could not complete this. Try again.");return;}
 sqlite3_bind_text(q,1,from,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,to,-1,SQLITE_TRANSIENT);
 sqlite3_str*out=sqlite3_str_new(db);sqlite3_str_appendall(out,"employee_id,employee,event_id,utc_timestamp,effective_direction,source\r\n");
 int n=0,rc;while((rc=sqlite3_step(q))==SQLITE_ROW){if(++n>100000){rc=SQLITE_TOOBIG;break;}char id[20];snprintf(id,sizeof id,"%d",sqlite3_column_int(q,0));op_csv(out,id);for(int i=1;i<6;i++){sqlite3_str_appendchar(out,1,',');op_csv(out,col_text(q,i));}sqlite3_str_appendall(out,"\r\n");}
 sqlite3_finalize(q);
 if(rc!=SQLITE_DONE){sqlite3_str_finish(out);rec_error(fd,409,"Too many records. Choose a shorter date range.");return;}
 op_export_send(fd,"text/csv; charset=utf-8","corrected_attendance.csv",out);
}

static void rec_today(int fd){
 sqlite3_str*s=sqlite3_str_new(db);sqlite3_stmt*q=NULL;
 if(sqlite3_prepare_v2(db,"SELECT date('now','localtime'),CAST(strftime('%H','now','localtime') AS INTEGER)*60+CAST(strftime('%M','now','localtime') AS INTEGER),coalesce((SELECT max(grace_minutes) FROM clock_late_settings),5),(SELECT name FROM statutory_holidays WHERE day=date('now','localtime'))",-1,&q,NULL)!=SQLITE_OK||sqlite3_step(q)!=SQLITE_ROW){sqlite3_finalize(q);sqlite3_free(sqlite3_str_finish(s));rec_error(fd,500,"The clock could not complete this. Try again.");return;}
 sqlite3_str_appendall(s,"{\"date\":");json_text(s,col_text(q,0));
 sqlite3_str_appendf(s,",\"now\":%d,\"grace\":%d,\"holiday\":",sqlite3_column_int(q,1),sqlite3_column_int(q,2));
 if(sqlite3_column_type(q,3)==SQLITE_NULL)sqlite3_str_appendall(s,"null");else json_text(s,col_text(q,3));
 sqlite3_finalize(q);q=NULL;
 sqlite3_str_appendall(s,",\"people\":[");
 /* Minutes from local midnight today; negative for a shift that began yesterday. */
 #define TODAY_MIN(col) "CAST(round((julianday(" col ",'localtime')-julianday(date('now','localtime')))*1440) AS INTEGER)"
 const char *shift_text=
  "SELECT x.state," TODAY_MIN("x.in_at") ",CASE WHEN x.out_at IS NULL THEN NULL ELSE " TODAY_MIN("x.out_at") " END,"
  "coalesce((SELECT c.late_minutes FROM punch_corrections c WHERE c.event_id=x.in_id OR c.manual_id=x.in_id),(SELECT late_minutes FROM attendance_events WHERE event_id=x.in_id),0),"
  "coalesce((SELECT c.early_minutes FROM punch_corrections c WHERE c.event_id=x.out_id OR c.manual_id=x.out_id),(SELECT early_minutes FROM attendance_events WHERE event_id=x.out_id),0)," REVIEW_ACTIVE_SQL("x","'open'") ",x.seconds "
  "FROM effective_shift_pairs x WHERE x.employee_id=?1 AND (date(x.in_at,'localtime')=date('now','localtime') OR date(x.out_at,'localtime')=date('now','localtime') "
  "OR (x.state='missing_out' AND x.in_at>=strftime('%Y-%m-%dT%H:%M:%fZ','now','-20 hours')) "
  "OR (x.state<>'complete' AND date(x.in_at,'localtime')=date('now','localtime','-1 day'))) ORDER BY x.in_at,x.in_id";
 #undef TODAY_MIN
 const char *people_sql="SELECT e.id,e.name,"
  "(SELECT kind FROM timeoff_requests t WHERE t.employee_id=e.id AND t.status='approved' AND t.cancelled_at IS NULL AND date('now','localtime') BETWEEN t.starts AND t.ends LIMIT 1),"
  "(SELECT reason FROM attendance_absences a WHERE a.employee_id=e.id AND a.cancelled_at IS NULL AND a.day=date('now','localtime') LIMIT 1) "
  "FROM employees e WHERE e.active=1 AND e.attendance_mode='punch_required' ORDER BY e.name COLLATE NOCASE,e.id";
 const char *sched_sql="SELECT start_time,end_time FROM schedule_profiles WHERE employee_id=?1 AND schedule_mode='recurring_weekly' "
  "AND (weekday_mask & (1 << ((CAST(strftime('%w','now','localtime') AS INTEGER)+6)%7)))<>0 "
  "UNION ALL SELECT ss.start_time,ss.end_time FROM schedule_shifts ss JOIN schedule_profiles sp ON sp.employee_id=ss.employee_id AND sp.schedule_mode='dated_shifts' "
  "WHERE ss.employee_id=?1 AND ss.work_date=date('now','localtime') ORDER BY 1";
 sqlite3_stmt *sh=NULL,*sc=NULL;int ok=sqlite3_prepare_v2(db,shift_text,-1,&sh,NULL)==SQLITE_OK&&sqlite3_prepare_v2(db,sched_sql,-1,&sc,NULL)==SQLITE_OK&&sqlite3_prepare_v2(db,people_sql,-1,&q,NULL)==SQLITE_OK;
 int first=1;
 while(ok&&sqlite3_step(q)==SQLITE_ROW){
  int id=sqlite3_column_int(q,0);
  sqlite3_str_appendf(s,"%s{\"id\":%d,\"name\":",first?"":",",id);first=0;json_text(s,col_text(q,1));
  sqlite3_str_appendall(s,",\"leave\":");
  if(sqlite3_column_type(q,2)!=SQLITE_NULL)json_text(s,col_text(q,2));else sqlite3_str_appendall(s,"null");
  sqlite3_str_appendall(s,",\"absence\":");
  if(sqlite3_column_type(q,3)!=SQLITE_NULL)json_text(s,col_text(q,3));else sqlite3_str_appendall(s,"null");
  sqlite3_str_appendall(s,",\"schedule\":[");
  sqlite3_reset(sc);sqlite3_bind_int(sc,1,id);int n=0;
  while(sqlite3_step(sc)==SQLITE_ROW){int a=0,b=0,c=0,d=0;
   if(sscanf(col_text(sc,0),"%d:%d",&a,&b)==2&&sscanf(col_text(sc,1),"%d:%d",&c,&d)==2){int st=a*60+b,en=c*60+d;if(en<=st)en+=1440;sqlite3_str_appendf(s,"%s[%d,%d]",n++?",":"",st,en);}}
  sqlite3_str_appendall(s,"],\"shifts\":[");
  sqlite3_reset(sh);sqlite3_bind_int(sh,1,id);n=0;
  while(sqlite3_step(sh)==SQLITE_ROW){
   sqlite3_str_appendf(s,"%s{\"state\":",n++?",":"");json_text(s,col_text(sh,0));
   sqlite3_str_appendf(s,",\"in\":%d,\"out\":",sqlite3_column_int(sh,1));
   if(sqlite3_column_type(sh,2)==SQLITE_NULL)sqlite3_str_appendall(s,"null");else sqlite3_str_appendf(s,"%d",sqlite3_column_int(sh,2));
   sqlite3_str_appendf(s,",\"late\":%d,\"early\":%d,\"active\":%s,\"seconds\":%lld}",sqlite3_column_int(sh,3),sqlite3_column_int(sh,4),sqlite3_column_int(sh,5)?"true":"false",sqlite3_column_int64(sh,6));
  }
  sqlite3_str_appendall(s,"]}");
 }
 sqlite3_finalize(q);sqlite3_finalize(sh);sqlite3_finalize(sc);
 if(!ok){sqlite3_free(sqlite3_str_finish(s));rec_error(fd,500,"The clock could not complete this. Try again.");return;}
 sqlite3_str_appendall(s,"]}");send_str(fd,"application/json",s);
}
static void rec_cycle_get(int fd){
 sqlite3_stmt*q=NULL;sqlite3_str*out=sqlite3_str_new(db);sqlite3_str_appendall(out,"{\"cycle\":");
 if(sqlite3_prepare_v2(db,"SELECT cycle,anchor FROM period_cycle_settings WHERE id=1",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW){json_text(out,col_text(q,0));sqlite3_str_appendall(out,",\"anchor\":");json_text(out,col_text(q,1));}else{sqlite3_str_appendall(out,"\"semi_monthly\",\"anchor\":\"2026-01-01\"");}
 sqlite3_finalize(q);
 sqlite3_str_appendchar(out,1,'}');send_str(fd,"application/json",out);
}
static void rec_cycle_save(int fd,int who,const char*body){
 char cycle[24]="",anchor[16]="";
 
 if(!form_value(body,"cycle",cycle,sizeof cycle)||!form_value(body,"anchor",anchor,sizeof anchor)||!date_valid(anchor)||
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
   They are closed only once their calendar end is past; reporting remains independently scheduled. */
static const char *rec_period_sql_activity=REC_PERIOD_CALENDAR
 "INSERT INTO pay_periods(starts,ends) SELECT c.starts,c.ends FROM candidates c "
 "WHERE c.ends<date('now','localtime') "
 "AND (EXISTS(SELECT 1 FROM effective_punches p WHERE date(p.at,'localtime') BETWEEN c.starts AND c.ends) "
 "OR " PERIOD_LEAVE_SQL("c.starts","c.ends") ") "
 "AND NOT EXISTS(SELECT 1 FROM pay_periods p WHERE NOT(p.ends<c.starts OR p.starts>c.ends))";
/* One closure transition for manager and calendar-driven attendance snapshots.
   Callers own the transaction. Review and payroll decisions are not prerequisites. */
static int rec_period_close(int period_id,int manager_id,const char *reason,int automatic){
 sqlite3_stmt *q=NULL;
 const char *query="UPDATE pay_periods SET status='closed',closed_at=strftime('%Y-%m-%dT%H:%M:%fZ','now'),closed_by=?1,reason=?2,closure_method=?3 WHERE status='open' AND ((?4=1 AND ends<date('now','localtime')) OR (?4=0 AND id=?5))";
 int ok=sqlite3_prepare_v2(db,query,-1,&q,NULL)==SQLITE_OK;
 if(ok){
  if(automatic)sqlite3_bind_null(q,1);else sqlite3_bind_int(q,1,manager_id);
  sqlite3_bind_text(q,2,reason,-1,SQLITE_TRANSIENT);
  sqlite3_bind_text(q,3,automatic?"automatic":"manual",-1,SQLITE_STATIC);
  sqlite3_bind_int(q,4,automatic);sqlite3_bind_int(q,5,period_id);
  ok=sqlite3_step(q)==SQLITE_DONE;
  if(ok&&!automatic)ok=sqlite3_changes(db)==1;
 }
 sqlite3_finalize(q);return ok;
}
static void rec_period_auto_due(void){
 sqlite3_stmt*q=NULL;
 if(!sql("BEGIN IMMEDIATE")){fprintf(stderr,"Automatic pay periods: database busy\n");return;}
 int ok=sqlite3_prepare_v2(db,rec_period_sql_auto,-1,&q,NULL)==SQLITE_OK;
 if(ok)ok=sqlite3_step(q)==SQLITE_DONE;
 if(!ok)fprintf(stderr,"Automatic pay periods: %s\n",sqlite3_errmsg(db));
 sqlite3_finalize(q);
 if(ok){sqlite3_stmt *history=NULL;ok=sqlite3_prepare_v2(db,rec_period_sql_activity,-1,&history,NULL)==SQLITE_OK;if(ok)ok=sqlite3_step(history)==SQLITE_DONE;sqlite3_finalize(history);}
 /* A completed calendar period is an attendance snapshot, not payroll approval.
    Late corrections and reviews remain independent; the mailer runs separately. */
 if(ok)ok=rec_period_close(0,0,"Period ended; attendance snapshot closed automatically",1);
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");
 if(!ok)fprintf(stderr,"Automatic pay periods: unable to save or close periods\n");
}
static int rec_employee_totals_source(int emp,const char*starts,const char*ends,int*count,long long*seconds,int cached);
static int rec_employee_totals(int emp,const char*starts,const char*ends,int*count,long long*seconds);
/* Approval follows the period history, never the current profile setting. */
static int rec_review_required(int employee,const char *starts,const char *ends){
 sqlite3_stmt *q=NULL;int required=1;
 if(sqlite3_prepare_v2(db,"SELECT " PERIOD_REVIEW_SQL("?1","?2","?3","effective_shift_pairs"),-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,employee);sqlite3_bind_text(q,2,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,ends,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW)required=sqlite3_column_int(q,0);}
 sqlite3_finalize(q);return required;
}
static void rec_period_people(int fd,const char*query){
 char txt[24]="";if(!form_value(query,"period_id",txt,sizeof txt)){rec_error(fd,400,"Select a pay period.");return;}
 char*end=NULL;long id=strtol(txt,&end,10);if(!*txt||*end||id<=0||id>2147483647){rec_error(fd,400,"Select a pay period.");return;}
 sqlite3_stmt*q=NULL;const char*sqltext="SELECT e.id,e.name,coalesce(ep.cnt,0),coalesce(ep.unpaired,0),coalesce(ms.signed_at,''),coalesce(ms.approved_seconds,0),coalesce(ms.reason,''),coalesce(ms.punch_count,-1),coalesce(ms.leave_fingerprint,'')," PERIOD_REVIEW_SQL("e.id","p.starts","p.ends","effective_shift_pairs") ",coalesce(ep.active,0) FROM employees e CROSS JOIN pay_periods p LEFT JOIN (SELECT employee_id,count(*) cnt,sum(CASE WHEN state<>'complete' AND NOT " REVIEW_ACTIVE_SQL("x","(SELECT status FROM pay_periods WHERE id=?1)") " THEN 1 ELSE 0 END) unpaired,sum(CASE WHEN " REVIEW_ACTIVE_SQL("x","(SELECT status FROM pay_periods WHERE id=?1)") " THEN 1 ELSE 0 END) active FROM effective_shift_pairs x WHERE date(in_at,'localtime') BETWEEN (SELECT starts FROM pay_periods WHERE id=?1) AND (SELECT ends FROM pay_periods WHERE id=?1) GROUP BY employee_id) ep ON ep.employee_id=e.id LEFT JOIN period_signoffs ms ON ms.period_id=p.id AND ms.employee_id=e.id AND ms.role='manager' WHERE p.id=?1 AND (ep.cnt>0 OR ms.signed_at IS NOT NULL OR " EMPLOYEE_LEAVE_SQL("e.id","p.starts","p.ends") ") ORDER BY e.name";
 if(sqlite3_prepare_v2(db,sqltext,-1,&q,NULL)!=SQLITE_OK){rec_error(fd,500,"The clock could not complete this. Try again.");return;}
 char period_starts[16]="",period_ends[16]="";sqlite3_stmt *bounds=NULL;
 if(sqlite3_prepare_v2(db,"SELECT starts,ends FROM pay_periods WHERE id=?1",-1,&bounds,NULL)==SQLITE_OK){sqlite3_bind_int(bounds,1,(int)id);if(sqlite3_step(bounds)==SQLITE_ROW){snprintf(period_starts,sizeof period_starts,"%s",col_text(bounds,0));snprintf(period_ends,sizeof period_ends,"%s",col_text(bounds,1));}}sqlite3_finalize(bounds);
 sqlite3_bind_int(q,1,(int)id);sqlite3_str*out=sqlite3_str_new(db);sqlite3_str_appendall(out,"{\"people\":[");int n=0;
 while(sqlite3_step(q)==SQLITE_ROW){if(n++)sqlite3_str_appendchar(out,1,',');sqlite3_str_appendf(out,"{\"id\":%d,\"name\":",sqlite3_column_int(q,0));json_text(out,col_text(q,1));sqlite3_str_appendf(out,",\"punches\":%d,\"unpaired\":%d,\"manager_signed\":%s,\"approved_seconds\":%lld,\"review_reason\":",sqlite3_column_int(q,2),sqlite3_column_int(q,3),*col_text(q,4)?"true":"false",sqlite3_column_int64(q,5));json_text(out,col_text(q,6));int current_count=0;long long current_seconds=0;rec_employee_totals(sqlite3_column_int(q,0),period_starts,period_ends,&current_count,&current_seconds);char fp[17]="";int fp_ok=rec_leave_fingerprint(sqlite3_column_int(q,0),period_starts,period_ends,fp);int mgr_current=fp_ok&&current_count==sqlite3_column_int(q,7)&&current_seconds==sqlite3_column_int64(q,5)&&!strcmp(fp,col_text(q,8));int long_shift=0;sqlite3_stmt *ls=NULL;
  if(sqlite3_prepare_v2(db,"SELECT EXISTS(SELECT 1 FROM effective_shift_pairs WHERE employee_id=?1 AND seconds>72000 AND state='complete' AND date(in_at,'localtime') BETWEEN ?2 AND ?3)",-1,&ls,NULL)==SQLITE_OK){sqlite3_bind_int(ls,1,sqlite3_column_int(q,0));sqlite3_bind_text(ls,2,period_starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(ls,3,period_ends,-1,SQLITE_TRANSIENT);if(sqlite3_step(ls)==SQLITE_ROW)long_shift=sqlite3_column_int(ls,0);}sqlite3_finalize(ls);
  sqlite3_str_appendf(out,",\"active_shifts\":%d",sqlite3_column_int(q,10));sqlite3_str_appendf(out,",\"long_shift\":%s",long_shift?"true":"false");sqlite3_str_appendf(out,",\"review_required\":%s,\"manager_current\":%s,\"current_seconds\":%lld}",sqlite3_column_int(q,9)?"true":"false",mgr_current?"true":"false",current_seconds);}
 sqlite3_finalize(q);sqlite3_str_appendall(out,"]}");send_str(fd,"application/json",out);
}
static void rec_period_cache_clear(void){
 sql("DROP TABLE IF EXISTS temp.lite_period_report_intervals; DROP TABLE IF EXISTS temp.lite_period_pairs; DROP TABLE IF EXISTS temp.lite_period_shift_pairs; DROP TABLE IF EXISTS temp.lite_period_punches; DROP TABLE IF EXISTS temp.lite_period_unpaired;");
}
static void rec_periods(int fd){
 /* Materialize paired attendance ONCE for this request. Re-running the
    recursive effective_punches view for each of 50 periods stalls RFID. */
 rec_period_cache_clear();
 if(!sql("CREATE TEMP TABLE lite_period_punches AS SELECT * FROM main.effective_punches")||
    !sql("CREATE INDEX temp.lite_punch_employee ON lite_period_punches(employee_id,at)")||
    !sql("CREATE TEMP TABLE lite_period_unpaired AS WITH ordered AS (SELECT employee_id,id,at,kind,manual,lag(kind) OVER (PARTITION BY employee_id ORDER BY at,id) prev_kind,lag(at) OVER (PARTITION BY employee_id ORDER BY at,id) prev_at,lead(kind) OVER (PARTITION BY employee_id ORDER BY at,id) next_kind,lead(at) OVER (PARTITION BY employee_id ORDER BY at,id) next_at FROM lite_period_punches) SELECT * FROM ordered WHERE (kind='IN' AND coalesce(next_kind,'')<>'OUT') OR (kind='OUT' AND coalesce(prev_kind,'')<>'IN')")||
    !sql("CREATE INDEX temp.lite_unpaired_at ON lite_period_unpaired(at)")||
    !sql("CREATE TEMP TABLE lite_period_shift_pairs AS SELECT * FROM main.effective_shift_pairs")||
    !sql("CREATE INDEX temp.lite_pair_employee ON lite_period_shift_pairs(employee_id,in_at)")||
    !sql("CREATE TEMP TABLE lite_period_report_intervals AS WITH punches AS (SELECT p.*,lead(at) OVER (PARTITION BY employee_id ORDER BY at,id) next_at,lead(kind) OVER (PARTITION BY employee_id ORDER BY at,id) next_kind,lead(id) OVER (PARTITION BY employee_id ORDER BY at,id) next_id FROM lite_period_punches p) SELECT employee_id,id,at,next_at,next_id,manual,CAST(strftime('%s',next_at) AS INTEGER)-CAST(strftime('%s',at) AS INTEGER) seconds FROM punches WHERE kind='IN' AND next_kind='OUT' AND next_at IS NOT NULL")||
    !sql("CREATE TEMP TABLE lite_period_pairs AS SELECT employee_id,date(in_at,'localtime') AS day FROM lite_period_shift_pairs")||
    !sql("CREATE INDEX lite_period_pairs_day ON lite_period_pairs(day,employee_id)")){
  rec_period_cache_clear();rec_error(fd,500,"Unable to load pay periods.");return;
 }
sqlite3_stmt*q=NULL;sqlite3_str*s=sqlite3_str_new(db);sqlite3_str_appendall(s,"{\"periods\":[");int n=0;if(sqlite3_prepare_v2(db,"SELECT id,starts,ends,status,coalesce(closed_at,'') FROM pay_periods p ORDER BY starts DESC LIMIT 50",-1,&q,NULL)==SQLITE_OK){while(sqlite3_step(q)==SQLITE_ROW){if(n++)sqlite3_str_appendchar(s,1,',');sqlite3_str_appendf(s,"{\"id\":%d,\"starts\":",sqlite3_column_int(q,0));json_text(s,col_text(q,1));sqlite3_str_appendall(s,",\"ends\":");json_text(s,col_text(q,2));sqlite3_str_appendall(s,",\"status\":");json_text(s,col_text(q,3));sqlite3_str_appendall(s,",\"closed_at\":");json_text(s,col_text(q,4));
 sqlite3_stmt *totalq=NULL;int required=0,manager_signoffs=0;
 const char *total_sql="SELECT count(*),coalesce(sum(ms.signed_at IS NOT NULL),0) FROM (SELECT employee_id FROM lite_period_pairs WHERE day BETWEEN ?1 AND ?2 UNION " LEAVE_EMPLOYEES_SQL("?1","?2") ") a LEFT JOIN period_signoffs ms ON ms.employee_id=a.employee_id AND ms.period_id=?3 AND ms.role='manager' WHERE " PERIOD_REVIEW_SQL("a.employee_id","?1","?2","temp.lite_period_shift_pairs");
 if(sqlite3_prepare_v2(db,total_sql,-1,&totalq,NULL)==SQLITE_OK){sqlite3_bind_text(totalq,1,col_text(q,1),-1,SQLITE_TRANSIENT);sqlite3_bind_text(totalq,2,col_text(q,2),-1,SQLITE_TRANSIENT);sqlite3_bind_int(totalq,3,sqlite3_column_int(q,0));if(sqlite3_step(totalq)==SQLITE_ROW){required=sqlite3_column_int(totalq,0);manager_signoffs=sqlite3_column_int(totalq,1);}}sqlite3_finalize(totalq);
 sqlite3_str_appendf(s,",\"review_total\":%d,\"manager_signoffs\":%d",required,manager_signoffs);sqlite3_stmt *activity=NULL;int has_activity=0;if(sqlite3_prepare_v2(db,"SELECT EXISTS(SELECT 1 FROM lite_period_pairs WHERE day BETWEEN ?1 AND ?2) OR " PERIOD_LEAVE_SQL("?1","?2"),-1,&activity,NULL)==SQLITE_OK){sqlite3_bind_text(activity,1,col_text(q,1),-1,SQLITE_TRANSIENT);sqlite3_bind_text(activity,2,col_text(q,2),-1,SQLITE_TRANSIENT);if(sqlite3_step(activity)==SQLITE_ROW)has_activity=sqlite3_column_int(activity,0);}sqlite3_finalize(activity);sqlite3_str_appendf(s,",\"has_activity\":%s",has_activity?"true":"false");
 if(!strcmp(col_text(q,3),"closed")){
  sqlite3_stmt *reviews=NULL;int changed=required>manager_signoffs,review_failed=0;
  if(sqlite3_prepare_v2(db,"SELECT employee_id,punch_count,approved_seconds,leave_fingerprint FROM period_signoffs s WHERE period_id=?1 AND role='manager' AND " PERIOD_REVIEW_SQL("s.employee_id","?2","?3","temp.lite_period_shift_pairs") "",-1,&reviews,NULL)==SQLITE_OK){
   sqlite3_bind_int(reviews,1,sqlite3_column_int(q,0));sqlite3_bind_text(reviews,2,col_text(q,1),-1,SQLITE_TRANSIENT);sqlite3_bind_text(reviews,3,col_text(q,2),-1,SQLITE_TRANSIENT);int rc;
   while((rc=sqlite3_step(reviews))==SQLITE_ROW){int count=0;long long seconds=0;char fp[17]="";
    if(!rec_employee_totals_source(sqlite3_column_int(reviews,0),col_text(q,1),col_text(q,2),&count,&seconds,1)||!rec_leave_fingerprint(sqlite3_column_int(reviews,0),col_text(q,1),col_text(q,2),fp))review_failed=1;
    else if(count!=sqlite3_column_int(reviews,1)||seconds!=sqlite3_column_int64(reviews,2)||strcmp(fp,col_text(reviews,3)))changed=1;
   }if(rc!=SQLITE_DONE)review_failed=1;
  }else review_failed=1;
  sqlite3_finalize(reviews);
  sqlite3_stmt *latest=NULL;const char *saved=NULL;int rev=0,has=0;
  int ready=sqlite3_prepare_v2(db,"SELECT revision,csv FROM lite_period_snapshots WHERE period_id=?1 ORDER BY revision DESC LIMIT 1",-1,&latest,NULL)==SQLITE_OK;
  if(ready){sqlite3_bind_int(latest,1,sqlite3_column_int(q,0));int rc=sqlite3_step(latest);if(rc==SQLITE_ROW){rev=sqlite3_column_int(latest,0);saved=(const char*)sqlite3_column_text(latest,1);has=1;}else if(rc!=SQLITE_DONE)ready=0;}
  char *body=NULL,*csv=NULL;
  int built=ready&&payroll_build_timesheet_source(db,col_text(q,1),col_text(q,2),&body,&csv,1,0);
  int stale=built&&has&&saved&&csv&&strcmp(saved,csv)!=0;
  sqlite3_str_appendf(s,",\"changed_after_close\":%s,\"latest_revision\":%d,\"export_stale\":%s,\"new_export_required\":%s,\"export_check_failed\":%s",changed?"true":"false",rev,stale?"true":"false",built&&(!has||stale)?"true":"false",built&&!review_failed?"false":"true");
  sqlite3_free(body);sqlite3_free(csv);sqlite3_finalize(latest);
 }
 sqlite3_stmt *failure=NULL;
 if(sqlite3_prepare_v2(db,"SELECT reason FROM lite_payroll_queue_failures WHERE period_id=?1",-1,&failure,NULL)==SQLITE_OK){sqlite3_bind_int(failure,1,sqlite3_column_int(q,0));if(sqlite3_step(failure)==SQLITE_ROW){sqlite3_str_appendall(s,",\"queue_failure\":");json_text(s,col_text(failure,0));}}
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
static void rec_period_action(int fd,int who,const char*body){char action[24]="",starts[16]="",ends[16]="",reason[250]="",num[20]="";if(!form_value(body,"action",action,sizeof action)||!form_value(body,"starts",starts,sizeof starts)||!form_value(body,"ends",ends,sizeof ends)||!form_value_multiline(body,"reason",reason,sizeof reason)||!form_value(body,"employee_id",num,sizeof num)){rec_error(fd,400,"Fill in all required fields.");return;}if(!date_valid(starts)||!date_valid(ends)||strcmp(starts,ends)>0||strlen(reason)<5u||strlen(reason)>220){rec_error(fd,400,"Enter valid dates and a reason of 5 to 220 characters.");return;}
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
   if(ok&&!rec_review_required((int)employee,starts,ends)){ok=0;fail="This employee does not require manager review for this pay period.";}
   int punches=0;long long secs=0;
   if(ok)ok=rec_employee_totals((int)employee,starts,ends,&punches,&secs);
   /* A review requires actual period activity, not merely an employee record. */
   if(ok){sqlite3_stmt *v=NULL;int active=0;
    const char *sql="SELECT EXISTS(SELECT 1 FROM effective_shift_pairs WHERE employee_id=?1 AND date(in_at,'localtime') BETWEEN ?2 AND ?3) OR " EMPLOYEE_LEAVE_SQL("?1","?2","?3");
    if(sqlite3_prepare_v2(db,sql,-1,&v,NULL)==SQLITE_OK){sqlite3_bind_int(v,1,(int)employee);sqlite3_bind_text(v,2,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(v,3,ends,-1,SQLITE_TRANSIENT);if(sqlite3_step(v)==SQLITE_ROW)active=sqlite3_column_int(v,0);}sqlite3_finalize(v);
    if(!active){ok=0;fail="This employee has no punches or leave in this pay period.";}
   }
   /* Only the manager reviews attendance, recording an auditable reason. */
   if(ok){
    sqlite3_stmt *v=NULL;int bad=0;
    if(sqlite3_prepare_v2(db,"SELECT state FROM effective_shift_pairs WHERE employee_id=?1 AND date(in_at,'localtime') BETWEEN ?2 AND ?3 ORDER BY in_at,in_id",-1,&v,NULL)==SQLITE_OK){
     sqlite3_bind_int(v,1,(int)employee);sqlite3_bind_text(v,2,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(v,3,ends,-1,SQLITE_TRANSIENT);
     while(sqlite3_step(v)==SQLITE_ROW){const char*k=col_text(v,0);if(strcmp(k,"complete"))bad=1;}
    }else bad=1;
    sqlite3_finalize(v);
    if(bad){ok=0;fail="Fix unmatched punches before manager review.";}
   }
   int manager_changed=0;
   if(ok)ok=sqlite3_prepare_v2(db,"INSERT INTO period_signoffs(period_id,employee_id,role,signed_by,punch_count,approved_seconds,reason,leave_fingerprint) SELECT ?1,id,'manager',?3,?4,?5,?6,?7 FROM employees WHERE id=?2 ON CONFLICT(period_id,employee_id,role) DO UPDATE SET signed_by=excluded.signed_by,punch_count=excluded.punch_count,approved_seconds=excluded.approved_seconds,reason=excluded.reason,leave_fingerprint=excluded.leave_fingerprint,signed_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE period_signoffs.punch_count<>excluded.punch_count OR period_signoffs.approved_seconds<>excluded.approved_seconds OR period_signoffs.leave_fingerprint<>excluded.leave_fingerprint",-1,&q,NULL)==SQLITE_OK;
   if(ok){sqlite3_bind_int(q,1,id);sqlite3_bind_int(q,2,(int)employee);sqlite3_bind_int(q,3,who);sqlite3_bind_int(q,4,punches);sqlite3_bind_int64(q,5,secs);sqlite3_bind_text(q,6,reason,-1,SQLITE_TRANSIENT);char fp[17]="";ok=rec_leave_fingerprint((int)employee,starts,ends,fp);if(ok){sqlite3_bind_text(q,7,fp,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;if(ok)manager_changed=sqlite3_changes(db)>0;}}sqlite3_finalize(q);q=NULL;
   if(ok&&manager_changed){sqlite3_stmt*a=NULL;ok=sqlite3_prepare_v2(db,"INSERT INTO period_approval_audit(period_id,employee_id,manager_id,action,reason,approved_seconds) VALUES(?1,?2,?3,'manager_review',?4,?5)",-1,&a,NULL)==SQLITE_OK;if(ok){sqlite3_bind_int(a,1,id);sqlite3_bind_int(a,2,(int)employee);sqlite3_bind_int(a,3,who);sqlite3_bind_text(a,4,reason,-1,SQLITE_TRANSIENT);sqlite3_bind_int64(a,5,secs);ok=sqlite3_step(a)==SQLITE_DONE;}sqlite3_finalize(a);}
  }else if(ok){
   /* Closing captures attendance as recorded. Review status and missing punches
      remain visible exceptions, not gates on delivering attendance information. */
   if(ok)ok=rec_period_close(id,who,reason,0);
  }
 }
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");if(ok)send_response(fd,200,"application/json","{\"status\":\"saved\"}");else rec_error(fd,409,fail);
}
static void rec_absences(int fd){
 sqlite3_stmt*q=NULL;
 const char *query="SELECT a.id,a.employee_id,e.name,a.day,a.reason,a.note,a.starts,a.ends,a.cancelled_at IS NOT NULL,coalesce(m.name,''),EXISTS(SELECT 1 FROM effective_punches p WHERE p.employee_id=a.employee_id AND date(p.at,'localtime')=a.day) FROM attendance_absences a JOIN employees e ON e.id=a.employee_id LEFT JOIN employees m ON m.id=a.manager_id ORDER BY a.day DESC,a.id DESC LIMIT 300";
 if(sqlite3_prepare_v2(db,query,-1,&q,NULL)!=SQLITE_OK){rec_error(fd,500,"The clock could not load absences.");return;}
 sqlite3_str*j=sqlite3_str_new(db);sqlite3_str_appendall(j,"{\"absences\":[");int n=0;
 while(sqlite3_step(q)==SQLITE_ROW){if(n++)sqlite3_str_appendchar(j,1,',');sqlite3_str_appendf(j,"{\"id\":%d,\"employee_id\":%d,\"employee\":",sqlite3_column_int(q,0),sqlite3_column_int(q,1));json_text(j,col_text(q,2));sqlite3_str_appendall(j,",\"day\":");json_text(j,col_text(q,3));sqlite3_str_appendall(j,",\"reason\":");json_text(j,col_text(q,4));sqlite3_str_appendall(j,",\"note\":");json_text(j,col_text(q,5));sqlite3_str_appendall(j,",\"starts\":");json_text(j,col_text(q,6));sqlite3_str_appendall(j,",\"ends\":");json_text(j,col_text(q,7));sqlite3_str_appendf(j,",\"cancelled\":%s,\"manager\":",sqlite3_column_int(q,8)?"true":"false");json_text(j,col_text(q,9));sqlite3_str_appendf(j,",\"has_punches\":%s}",sqlite3_column_int(q,10)?"true":"false");}
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
 if(employee<=0||*tail||!date_valid(date)||!valid_reason||strlen(note)>220||(!strcmp(reason,"Other")&&strlen(note)<5)){rec_error(fd,400,"Select an employee, valid date and reason.");return;}
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
static void rec_page(int fd){send_response(fd,200,"text/html; charset=utf-8",UI_HEAD("Attendance","corrections") CLOCK_RECORD_WARNING
 "<h1>Attendance</h1><p>Times are local. Every correction is logged and the original swipe is kept.</p><p id='punchbanner' role='alert' hidden></p>"
 "<section><div class='actions'><a href='/pay-periods'>Pay periods</a> <a href='/attendance-export'>Raw swipe export</a></div><label>Employee <select id='employee'></select></label><div class='actions'><button type='button' id='add'>Add missed punch</button></div><p id='status' role='status'></p></section>"
 "<section id='shifts'><div class='today-head'><h2>Shifts by day</h2><p id='daysSummary' class='today-summary'></p></div><div id='daysLegend'></div><div id='days' class='days' role='list' aria-label='Shifts by day'></div></section>"
 "<section id='punches'><h2>Punches</h2><table class='punch-review'><thead><tr><th>Employee</th><th>Time</th><th>Punch</th><th>Source</th><th>Note</th><th>Actions</th></tr></thead><tbody id='rows'></tbody></table></section>"
 "<section><h2>Recorded absences</h2><p>Absences explain missed attendance, such as sickness or an unexpected absence. Record vacation on the <a href='/time-off-requests'>Vacation page</a>. Neither creates punches or worked hours.</p><form id='absenceForm'><label>Date<input type='date' name='day' required></label><label>Reason<select name='reason'><option>Sick</option><option>Medical appointment</option><option>Personal day</option><option>Family emergency</option><option>Bereavement</option><option>Unpaid leave</option><option>Unexcused absence</option><option>Other</option></select></label><div class='actions'><label>From (optional)<input type='time' name='starts'></label><label>To (optional)<input type='time' name='ends'></label></div><label>Manager note<textarea name='note' maxlength='220' aria-describedby='absenceNoteHint'></textarea></label><p id='absenceNoteHint' class='muted'>Don't enter medical details.</p><div class='actions'><button type='submit'>Record absence</button></div><p id='absenceStatus' role='status'></p></form><div class='table-scroll'><table class='absences-table'><thead><tr><th>Employee</th><th>Date</th><th>Reason</th><th>Hours</th><th>Note</th><th>Status</th><th></th></tr></thead><tbody id='absenceRows'></tbody></table></div></section>"
 "<dialog id='editor'><form id='editform'><h2 id='heading'>Punch correction</h2><input type='hidden' name='action' id='action'><input type='hidden' name='event_id' id='event_id'><input type='hidden' name='employee_id' id='employee_id'><label id='whenlabel'>Local date and time<input id='local_at' type='datetime-local'></label><label id='kindlabel'>Direction<select name='kind' id='kind'><option>IN</option><option>OUT</option></select></label><label>Reason<textarea name='reason' required minlength='5' maxlength='240'></textarea></label><p id='editstatus' role='status' aria-live='polite'></p><div class='actions'><button class='secondary' type='button' id='cancel'>Cancel</button><button type='submit' id='save'>Save correction</button></div></form></dialog>"
 "<script>"
 "const add=document.getElementById('add'),cancel=document.getElementById('cancel'),heading=document.getElementById('heading'),kind=document.getElementById('kind'),kindlabel=document.getElementById('kindlabel'),local_at=document.getElementById('local_at'),whenlabel=document.getElementById('whenlabel'),emp=document.getElementById('employee'),rows=document.getElementById('rows'),dialog=document.getElementById('editor'),form=document.getElementById('editform'),msg=document.getElementById('status'),editstatus=document.getElementById('editstatus'),save=document.getElementById('save');let punches=[];"
 "const api=tc.api;"
 "function localValue(s){let d=new Date(s);if(!Number.isFinite(d.getTime()))return '';let z=n=>String(n).padStart(2,'0');return d.getFullYear()+'-'+z(d.getMonth()+1)+'-'+z(d.getDate())+'T'+z(d.getHours())+':'+z(d.getMinutes())}"
 "function labelTime(s){let d=new Date(s);return Number.isFinite(d.getTime())?tc.datetime(d):s}"
 "function openEdit(action,p){form.reset();editstatus.textContent='';save.disabled=false;form.elements.action.value=action;form.elements.employee_id.value=p?p.employee_id:emp.value;form.elements.event_id.value=p?p.id:'';heading.textContent=action==='add'?'Add missed punch':action==='change'?'Change punch':'Void punch';whenlabel.hidden=action==='void';kindlabel.hidden=action==='void';local_at.required=action!=='void';local_at.value=p?localValue(p.at):localValue(new Date().toISOString());if(p)kind.value=p.kind;dialog.showModal()}"
 "function flags(list){let out=new Map();for(const p of list){let f=[];if(p.shift_state==='missing_out'&&!p.active_shift)f.push(['No OUT','problem']);if(p.shift_state==='missing_in')f.push(['No IN','problem']);if(p.late>0)f.push(['Late '+p.late+' min','check']);if(p.early_minutes>0)f.push(['Left early '+p.early_minutes+' min','check']);if(p.manual)f.push(['Added by manager','check']);else if(p.corrected)f.push(['Corrected','check']);out.set(p.id,f)}return out}"
 "const daysGrid=document.getElementById('days'),daysSummary=document.getElementById('daysSummary');let schedules={employees:[],shifts:[]};\n"
 "document.getElementById('daysLegend').replaceWith(tc.legend([['work','Worked'],['open','In now'],['sched','Scheduled'],['late','Late'],['long','Long shift'],['miss','Missing punch']]));\n"
 "function dayLabel(d){return tc.date(d,'day')}\n"
 "function dayGap(a,b){return Math.round((Date.parse(b+'T12:00:00Z')-Date.parse(a+'T12:00:00Z'))/86400000)}\n"
 "function scheduleFor(id,day){let e=schedules.employees.find(x=>String(x.id)===String(id)),out=[],t=w=>{let [h,m]=w.split(':').map(Number);return h*60+m};if(!e)return out;\n"
 " if(e.schedule_mode==='recurring_weekly'&&e.start&&e.end&&(e.mask&(1<<((new Date(day+'T12:00:00').getDay()+6)%7)))){let s=t(e.start),f=t(e.end);out.push([s,f<=s?f+1440:f])}\n"
 " if(e.schedule_mode==='dated_shifts')for(let x of schedules.shifts)if(String(x.employee_id)===String(id)&&x.date===day){let s=t(x.start),f=t(x.end);out.push([s,f<=s?f+1440:f])}\n"
 " return out}\n"
 "function drawDays(){\n"
 " let list=punches.filter(p=>String(p.employee_id)===emp.value).sort((a,b)=>a.at<b.at?-1:a.at>b.at?1:0),days=new Map(),day=d=>{if(!days.has(d))days.set(d,{marks:[],seconds:0,problems:[],words:[]});return days.get(d)};\n"
 " for(const p of list){if(p.kind==='OUT'&&p.shift_state!=='missing_in')continue;let d=day(p.day);if(p.late>0)d.marks.push({s:p.min-p.late,e:p.min,c:'late',t:'Late '+p.late+' min'});if(p.shift_state==='complete'){let e=p.paired_out_min+1440*dayGap(p.day,p.paired_out_day),long=p.shift_seconds>72000;d.marks.push({s:p.min,e,c:long?'long':'work',t:tc.hm(p.min)+' to '+tc.hm(p.paired_out_min)+(long?', long shift':'')});d.seconds+=p.shift_seconds;d.words.push(tc.hm(p.min)+' to '+tc.hm(p.paired_out_min))}else if(p.active_shift){let age=(Date.now()-Date.parse(p.at))/60000;d.marks.push({s:p.min,e:p.min+age,c:'open',t:'In since '+tc.hm(p.min)});d.words.push('in since '+tc.hm(p.min));d.open=true}else{let missing=p.shift_state==='missing_in'?'IN':'OUT';d.marks.push({s:p.min,e:p.min+45,c:'miss',t:'Missing '+missing});d.problems.push('Missing '+missing);d.words.push('missing '+missing)}}"
 " let keys=[...days.keys()].sort().reverse(),edges=[];\n"
 " for(let k of keys){let d=days.get(k);for(let w of scheduleFor(emp.value,k))d.marks.unshift({s:w[0],e:w[1],c:'sched',t:'Scheduled '+tc.hm(w[0])+' to '+tc.hm(w[1])});for(let m of d.marks)edges.push(Math.max(0,m.s),Math.min(1440,m.e))}\n"
 " let [from,to]=tc.range(edges,480,1020);\n"
 " daysGrid.replaceChildren();\n"
 " if(!keys.length){daysSummary.textContent='No punches loaded for this employee.';return}\n"
 " let axisRow=document.createElement('div');axisRow.className='day-row is-axis';axisRow.setAttribute('aria-hidden','true');let ax=document.createElement('div');ax.className='strip-cell';ax.append(tc.axis(from,to));axisRow.append(document.createElement('div'),ax,document.createElement('div'));daysGrid.append(axisRow);\n"
 " let total=0,bad=0;\n"
 " for(let k of keys){let d=days.get(k),row=document.createElement('div'),name=document.createElement('div'),cell=document.createElement('div'),hours=document.createElement('div'),a=document.createElement('a');\n"
 "  total+=d.seconds;if(d.problems.length)bad++;\n"
 "  row.className='day-row';row.setAttribute('role','listitem');row.dataset.day=k;name.className='day-name';a.className='day-pick';a.href='#punches';a.textContent=dayLabel(k);\n"
 "  a.onclick=ev=>{ev.preventDefault();pickDay(k)};name.append(a);cell.className='strip-cell';\n"
 "  cell.append(tc.strip({from,to,label:dayLabel(k)+': '+(d.words.join(', ')||'no complete shifts'),marks:d.marks}));\n"
 "  hours.className='day-hours';\n"
 "  if(d.problems.length)hours.append(tc.flag([...new Set(d.problems)].join(', '),'problem'));else if(d.open)hours.append(tc.flag('In now','ok'));else hours.textContent=(d.seconds/3600).toFixed(2)+' h';\n"
 "  row.append(name,cell,hours);daysGrid.append(row)}\n"
 " daysSummary.textContent=keys.length+(keys.length===1?' day':' days')+' loaded, '+(total/3600).toFixed(2)+' h recorded'+(bad?', '+bad+(bad===1?' day needs':' days need')+' fixing.':'.');\n"
 "}\n"
 "function pickDay(k){for(let r of daysGrid.children)r.classList.toggle('is-selected',r.dataset.day===k);let first=null;for(let tr of rows.children){let on=tr.dataset.day===k;tr.classList.toggle('is-highlight',on);if(on&&!first)first=tr}if(first)first.scrollIntoView({behavior:matchMedia('(prefers-reduced-motion: reduce)').matches?'auto':'smooth',block:'center'})}\n"
 "function draw(){rows.replaceChildren();let all=flags(punches),lastDay='';for(let p of punches.filter(p=>String(p.employee_id)===emp.value)){let tr=document.createElement('tr'),f=all.get(p.id)||[];for(let v of [p.name,labelTime(p.at),p.kind,p.manual?'Manager':'Card',p.note||'']){let td=document.createElement('td');td.textContent=v;tr.append(td)}for(let [t,l] of f)tr.children[2].append(' ',tc.flag(t,l));if(f.some(x=>x[1]==='problem'))tr.className='row-problem';else if(f.length)tr.className='row-check';tr.dataset.day=p.day;if(lastDay&&lastDay!==p.day)tr.classList.add('is-day-start');lastDay=p.day;let td=document.createElement('td');if(!p.manual&&(p.early_minutes>0||p.late>0)){let n=document.createElement('button');n.type='button';n.className='secondary';n.textContent='Manager note';n.onclick=async()=>{let reason=await tc.reason({title:p.kind==='OUT'?'Explain the approved departure':'Explain the late arrival',label:'Manager note',value:p.note||'',ok:'Save note'});if(reason===null)return;try{await api('/api/v1/admin/punches',{method:'POST',body:new URLSearchParams({action:'note',event_id:p.id,reason})});tc.ok(msg,'Manager note saved.');await refresh()}catch(e){tc.err(msg,e)}};td.append(n,' ')}for(let [name,op] of [['Change','change'],['Void','void']]){let b=document.createElement('button');b.type='button';b.className='secondary';b.textContent=name;b.onclick=()=>openEdit(op,p);td.append(b,' ')}tr.append(td);rows.append(tr)}if(!rows.children.length){let tr=document.createElement('tr'),td=document.createElement('td');td.colSpan=6;td.textContent='No punches.';tr.append(td);rows.append(tr)}"
 "let count=new Map();for(let p of punches)if((all.get(p.id)||[]).some(x=>x[1]==='problem'))count.set(p.name,(count.get(p.name)||0)+1);let n=[...count.values()].reduce((a,b)=>a+b,0);"
 "tc.banner(document.getElementById('punchbanner'),'problem','Problem',n?n+(n===1?' punch has':' punches have')+' no matching IN or OUT: '+[...count].map(x=>x[0]+' ('+x[1]+')').join(', ')+'.':'');"
 "for(let o of emp.options){let c=[...punches].filter(p=>String(p.employee_id)===o.value&&(all.get(p.id)||[]).some(x=>x[1]==='problem')).length;o.textContent=o.dataset.name+(c?' ('+c+' to fix)':'')}drawDays()}"
 "async function refresh(){let [a,b,c]=await Promise.all([api('/api/v1/admin/employees'),api('/api/v1/admin/punches'),api('/api/v1/admin/schedules').catch(()=>({employees:[],shifts:[]}))]);schedules=c;let chosen=emp.value||new URLSearchParams(location.search).get('employee_id');emp.replaceChildren();for(let e of a.employees.sort((x,y)=>{let name=e=>[e.first_name,e.last_name].filter(Boolean).join(' ')||e.name||'Employee '+e.id;return name(x).localeCompare(name(y),undefined,{sensitivity:'base'})||x.id-y.id})){let o=document.createElement('option');o.value=e.id;o.dataset.name=[e.first_name,e.last_name].filter(Boolean).join(' ')||e.name||'Employee '+e.id;o.textContent=o.dataset.name;emp.append(o)}punches=b.punches;if([...emp.options].some(o=>o.value===chosen))emp.value=chosen;else{let all=flags(punches),first=punches.find(p=>(all.get(p.id)||[]).some(x=>x[1]==='problem'))||punches[0];if(first)emp.value=String(first.employee_id)}draw()}"
 "emp.onchange=draw;add.onclick=()=>openEdit('add',null);cancel.onclick=()=>dialog.close();form.onsubmit=async e=>{e.preventDefault();let data=new URLSearchParams(new FormData(form));if(data.get('action')==='void'){let p=punches.find(x=>String(x.id)===data.get('event_id'));if(!p){tc.err(editstatus,'This punch no longer exists. Reload the page.');return}data.set('at',p.at);data.set('kind',p.kind)}else{let d=new Date(local_at.value);if(!Number.isFinite(d.getTime())){tc.err(editstatus,'Enter a valid date and time.');return}data.set('at',d.toISOString().slice(0,19)+'Z')}if(!await tc.ask({title:'Save correction?',ok:'Save correction'}))return;save.disabled=true;editstatus.textContent='Saving correction...';try{await api('/api/v1/admin/punches',{method:'POST',body:data});dialog.close();tc.ok(msg,'Correction saved.');await refresh()}catch(e){tc.err(editstatus,e)}finally{save.disabled=false}};"
 "const absenceForm=document.getElementById('absenceForm'),absenceRows=document.getElementById('absenceRows'),absenceStatus=document.getElementById('absenceStatus');"
 "async function loadAbsences(){let data=await api('/api/v1/admin/absences');absenceRows.replaceChildren();for(let a of data.absences.filter(x=>String(x.employee_id)===emp.value)){let tr=document.createElement('tr');for(let v of [a.employee,a.day,a.reason,a.starts?a.starts+' to '+a.ends:'Full day',a.note,a.cancelled?'Cancelled':a.has_punches?'Punches recorded':'Recorded']){let td=document.createElement('td');if(a.has_punches&&!a.cancelled&&v==='Punches recorded'){td.append(tc.flag('Punches recorded','check'));tr.className='row-check'}else td.textContent=v;tr.append(td)}let td=document.createElement('td');if(!a.cancelled){let b=document.createElement('button');b.type='button';b.className='secondary';b.textContent='Cancel';b.onclick=async()=>{let note=await tc.reason({title:'Cancel absence',label:'Reason (at least 5 characters)',min:5,ok:'Cancel absence',cancel:'Keep absence'});if(!note)return;try{await api('/api/v1/admin/absences',{method:'POST',body:new URLSearchParams({action:'cancel',id:a.id,note})});tc.ok(absenceStatus,'Absence cancelled.');await loadAbsences()}catch(e){tc.err(absenceStatus,e)}};td.append(b)}tr.append(td);absenceRows.append(tr)}if(!absenceRows.children.length){let tr=document.createElement('tr'),td=document.createElement('td');td.colSpan=6;td.textContent='No recorded absences.';tr.append(td);absenceRows.append(tr)}}"
 "absenceForm.onsubmit=async e=>{e.preventDefault();let d=new URLSearchParams(new FormData(absenceForm));d.set('employee_id',emp.value);try{await api('/api/v1/admin/absences',{method:'POST',body:d});tc.ok(absenceStatus,'Absence recorded.');absenceForm.reset();await loadAbsences()}catch(e){tc.err(absenceStatus,e)}};emp.addEventListener('change',()=>loadAbsences().catch(e=>tc.err(absenceStatus,e)));loadAbsences().catch(e=>tc.err(absenceStatus,e));"
 "refresh().then(loadAbsences).catch(e=>tc.err(msg,e))"
 "</script>"
 UI_FOOT);}

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
 sqlite3_bind_int(q,1,*id);if(sqlite3_step(q)==SQLITE_ROW){snprintf(start,11,"%s",col_text(q,0));snprintf(end,11,"%s",col_text(q,1));found=1;}}
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
 sqlite3_str_appendf(o,"{\"revision\":%d,\"created_at\":\"%s\"}",sqlite3_column_int(q,0),col_text(q,1));}}
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
static void lite_export_download(int fd,const char*query,int preview){
 char idtext[24]="",revtext[24]="",starts[11]="",ends[11]="";int id=0;
 if(!form_value(query,"period_id",idtext,sizeof idtext)){rec_error(fd,400,"Invalid period ID.");return;}
 char *csv=NULL;int revision=0;
 if(preview){
  if(strlen(idtext)>9||!*idtext||strspn(idtext,"0123456789")!=strlen(idtext)||(id=atoi(idtext))<1){rec_error(fd,400,"Invalid period ID.");return;}
  sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(db,"SELECT starts,ends FROM pay_periods WHERE id=?1",-1,&q,NULL)==SQLITE_OK;
  if(ok){sqlite3_bind_int(q,1,id);ok=sqlite3_step(q)==SQLITE_ROW;if(ok){snprintf(starts,sizeof starts,"%s",col_text(q,0));snprintf(ends,sizeof ends,"%s",col_text(q,1));}}
  sqlite3_finalize(q);if(!ok){rec_error(fd,404,"Pay period not found.");return;}
  char *report=NULL;
  if(!payroll_build_timesheet_preview(db,starts,ends,&report,&csv)){rec_error(fd,500,"Cannot calculate attendance preview.");return;}
  sqlite3_free(report);
  char *copy=strdup(csv);sqlite3_free(csv);csv=copy;
  if(!csv){rec_error(fd,500,"Cannot allocate attendance preview.");return;}
 }else{
  if(!form_value(query,"revision",revtext,sizeof revtext)||!lite_export_period(fd,idtext,&id,starts,ends))return;
  for(const char*c=revtext;*c;c++)if(*c<'0'||*c>'9'){rec_error(fd,400,"Invalid revision.");return;}
  revision=strlen(revtext)<=9?atoi(revtext):0;if(revision<1){rec_error(fd,400,"Invalid revision.");return;}
  sqlite3_stmt*q=NULL;
  if(sqlite3_prepare_v2(db,"SELECT csv FROM lite_period_snapshots WHERE period_id=?1 AND revision=?2",-1,&q,NULL)==SQLITE_OK){
   sqlite3_bind_int(q,1,id);sqlite3_bind_int(q,2,revision);if(sqlite3_step(q)==SQLITE_ROW){const char*v=col_text(q,0);csv=strdup(v);}}
  sqlite3_finalize(q);if(!csv){rec_error(fd,404,"Export revision not found.");return;}
 }
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
 pid_t pid=fork();if(pid==0){execlp("python3","python3",script,input,output,starts,ends,preview?"--preview":NULL,(char*)NULL);_exit(127);}
 int status=0;pid_t waited=pid>0?waitpid(pid,&status,0):-1;sigaction(SIGCHLD,&old_chld,NULL);ok=pid>0&&waited==pid&&WIFEXITED(status)&&WEXITSTATUS(status)==0;}
 unlink(input);
 struct stat st;if(ok)ok=stat(output,&st)==0&&st.st_size>0&&st.st_size<=12000000;
 if(!ok){unlink(output);rec_error(fd,500,"Excel generation failed.");return;}
 int f=open(output,O_RDONLY);if(f<0){unlink(output);rec_error(fd,500,"Cannot open export.");return;}
 size_t len=(size_t)st.st_size;char*bytes=malloc(len);ok=bytes!=NULL;
 if(ok){size_t got=0;while(got<len){ssize_t n=read(f,bytes+got,len-got);if(n<=0){ok=0;break;}got+=(size_t)n;}}
 close(f);unlink(output);if(!ok){free(bytes);rec_error(fd,500,"Cannot read export.");return;}
 char filename[120],header[700];if(preview)snprintf(filename,sizeof filename,"Timesheet_%s_to_%s_PREVIEW.xlsx",starts,ends);
 else snprintf(filename,sizeof filename,"Timesheet_%s_to_%s_rev%d.xlsx",starts,ends,revision);
 int n=snprintf(header,sizeof header,"HTTP/1.1 200 OK\r\nContent-Type: application/vnd.openxmlformats-officedocument.spreadsheetml.sheet\r\nContent-Disposition: attachment; filename=\"%s\"\r\nContent-Length: %zu\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n",filename,len);
 long long deadline=now_ms()+60000;if(n>0&&(size_t)n<sizeof header&&op_send_all(fd,header,(size_t)n,deadline))op_send_all(fd,bytes,len,deadline);
 free(bytes);
}
static void rec_period_page(int fd){send_response(fd,200,"text/html; charset=utf-8",UI_HEAD("Pay periods","periods") CLOCK_RECORD_WARNING
 "<h1>Pay periods</h1><p id='uncoveredWarning' role='alert' hidden></p><p id='cyclebanner' role='alert' hidden></p><p id='cycleMismatch' role='alert' hidden></p>"
 "<details id='periodSettings'><summary>Attendance period schedule</summary><section><h2>Attendance period schedule</h2><form id='settings'><label>Frequency<select name='cycle' id='cycle'><option value='semi_monthly'>Twice monthly (1st to 15th, 16th to last day)</option><option value='biweekly'>Every two weeks</option><option value='weekly'>Weekly</option><option value='monthly'>Monthly</option></select></label><label id='anchorLabel'>First period start<input type='date' name='anchor' id='anchor' required></label><div class='actions'><button>Save pay schedule</button></div></form><p class='muted'>Current and future pay periods are created automatically. Closed and reviewed periods are preserved.</p></section></details>"
 "<section><h2>Attendance periods</h2><table class='periods-table'><thead><tr><th>Start</th><th>End</th><th>Status / report</th><th>Attention</th><th></th></tr></thead><tbody id='rows'></tbody></table><p id='status' role='status'></p></section>"
 "<section><h2>Saved attendance reports</h2><p>Close a period before exporting. Existing revisions are never changed, and revisions do not resend email.</p><label>Pay period<select id='exportPeriod'></select></label><div class='actions'><button type='button' id='exportCreate'>Create export / revision</button></div><ul id='exportVersions'></ul><p id='exportStatus' role='status'></p></section>"
 "<section><h2 id='detailTitle'>Select a pay period</h2><table><thead><tr><th>Employee</th><th>Shifts</th><th>Incomplete shifts</th><th>Review status</th><th>Recorded hours</th><th></th></tr></thead><tbody id='people'><tr><td colspan='7'>Select Review on a period above.</td></tr></tbody></table><p id='closeReason' role='status'></p><div class='actions'><button type='button' id='closePeriod' disabled>Close period</button><a href='/punch-corrections'>Punches and corrections</a><a href='/attendance-export'>Raw swipe export</a></div></section>"
 "<script>const anchor=document.getElementById('anchor'),anchorLabel=document.getElementById('anchorLabel'),closePeriod=document.getElementById('closePeriod'),cycle=document.getElementById('cycle'),detailTitle=document.getElementById('detailTitle'),exportCreate=document.getElementById('exportCreate'),exportPeriod=document.getElementById('exportPeriod'),exportStatus=document.getElementById('exportStatus'),exportVersions=document.getElementById('exportVersions'),people=document.getElementById('people'),rows=document.getElementById('rows'),settings=document.getElementById('settings');if(location.hash==='#periodSettings')document.getElementById('periodSettings').open=true;let selected=null,periods=[];const message=document.getElementById('status');const api=tc.api;function post(url,data){return api(url,{method:'POST',body:new URLSearchParams(data)})}async function refresh(){let res=await api('/api/v1/admin/periods');periods=res.periods;tc.banner(document.getElementById('uncoveredWarning'),'problem','Problem',[res.uncovered_punches?res.uncovered_punches+' punches are outside all attendance periods. Review punch records.':'',''].filter(Boolean).join(' '));let old=exportPeriod.value;exportPeriod.replaceChildren();for(let p of periods.filter(p=>p.status==='closed')){let o=document.createElement('option');o.value=p.id;o.textContent=p.starts+' to '+p.ends;exportPeriod.append(o)}if([...exportPeriod.options].some(o=>o.value===old))exportPeriod.value=old;await loadVersions();rows.replaceChildren();let today=tc.date(new Date()),due=0;for(let p of periods){let tr=document.createElement('tr'),ended=p.ends<today;tr.dataset.periodId=p.id;for(let value of [p.starts,p.ends,'','']){let td=document.createElement('td');td.textContent=value;tr.append(td)}if(ended&&p.has_activity&&p.manager_signoffs<p.review_total)tr.children[3].append(tc.flag('Review pending','muted'));if(p.status==='closed'){tr.children[2].append(tc.flag('Closed','muted'));if(p.changed_after_close){let f=tc.flag('Changed after close','problem');f.dataset.changedAfterClose='1';tr.children[2].append(f)}if(p.new_export_required)tr.children[2].append(tc.flag('New export required','problem'));if(p.export_stale)tr.children[2].append(tc.flag('Changed since revision '+p.latest_revision,'problem'));if(p.export_check_failed)tr.children[2].append(tc.flag('Cannot check latest export','problem'));if(p.queue_failure){tr.children[2].append(tc.flag('Report not queued','problem'));tr.children[3].append(tc.flag(p.queue_failure,'problem'));}}else if(ended&&p.has_activity){tr.children[2].append(tc.flag('Closing pending','problem'));tr.className='row-problem';due++}else if(ended){tr.children[2].textContent='Ended (empty)'}else tr.children[2].append(tc.flag('Open','ok'));let td=document.createElement('td'),actions=document.createElement('div');actions.className='actions period-actions';let reviewButton=document.createElement('button');reviewButton.type='button';reviewButton.className='secondary';reviewButton.textContent='Review';reviewButton.onclick=()=>selectPeriod(p);actions.append(reviewButton);if(p.status==='closed'){let send=document.createElement('button');send.type='button';send.className='secondary';send.textContent='Send report';send.onclick=async()=>{if(!await tc.ask({title:'Queue report for '+p.starts+' to '+p.ends+'?',ok:'Send report'}))return;try{await post('/api/v1/admin/periods/send-report',{period_id:p.id});tc.ok(message,'Report requested. See Email queue.')}catch(x){tc.err(message,x)}};actions.append(send);if(p.latest_revision){const download=document.createElement('a');download.textContent='Download revision '+p.latest_revision;download.href='/api/v1/admin/periods/export.xlsx?period_id='+p.id+'&revision='+p.latest_revision;actions.append(download)}}td.append(actions);tr.append(td);rows.append(tr)}tc.banner(document.getElementById('cyclebanner'),'problem','Problem',due?due+(due===1?' pay period has':' pay periods have')+' ended without being closed.':'');if(selected){let match=periods.find(p=>p.id===selected.id);if(match)await selectPeriod(match)}}async function loadVersions(){exportVersions.replaceChildren();if(!exportPeriod.value)return;try{let j=await api('/api/v1/admin/periods/exports?period_id='+encodeURIComponent(exportPeriod.value));for(let x of j.revisions){let li=document.createElement('li'),a=document.createElement('a');a.textContent='Revision '+x.revision+' ('+tc.datetime(x.created_at)+') - Download Excel';a.href='/api/v1/admin/periods/export.xlsx?period_id='+encodeURIComponent(exportPeriod.value)+'&revision='+x.revision;li.append(a);exportVersions.append(li)}}catch(x){tc.err(exportStatus,x)}}exportPeriod.onchange=loadVersions;exportCreate.onclick=async()=>{try{let x=await post('/api/v1/admin/periods/exports',{period_id:exportPeriod.value});tc.ok(exportStatus,'Revision '+x.revision+' preserved. No email sent.');await refresh()}catch(x){tc.err(exportStatus,x)}};async function selectPeriod(p){selected=p;if(p.status==='closed'){exportPeriod.value=String(p.id);await loadVersions();}detailTitle.textContent=p.starts+' to '+p.ends+(p.status==='closed'?' (closed)':'');closePeriod.disabled=p.status==='closed';let j=await api('/api/v1/admin/periods/people?period_id='+encodeURIComponent(p.id));people.replaceChildren();let bad=0,unapproved=0;for(let e of j.people){let tr=document.createElement('tr');let person=document.createElement('a');person.href='/punch-corrections?employee_id='+e.id;person.textContent=e.name;let cells=[person,e.punches,e.unpaired?tc.flag('Fix punches','problem'):e.long_shift?tc.flag('Long shift','check'):e.active_shifts?tc.flag('Clocked in, no OUT yet','muted'):'None',!e.review_required?tc.flag('Not required','muted'):e.manager_signed&&e.manager_current?tc.flag('Reviewed','ok'):(e.manager_signed?tc.flag('Changed since review','check'):tc.flag('Review pending','check')),(e.current_seconds/3600).toFixed(2)+' h'];for(let v of cells){let td=document.createElement('td');td.append(...[].concat(v));tr.append(td)}if(e.unpaired)tr.className='row-problem';else if(e.long_shift)tr.className='row-check';let td=document.createElement('td');if(e.review_required&&(!e.manager_signed||!e.manager_current)&&!e.unpaired&&!e.active_shifts ){let b=document.createElement('button');b.textContent='Mark reviewed';b.onclick=()=>review(p,e);td.append(b)}tr.append(td);people.append(tr);if(e.unpaired)bad++;if(e.review_required&&(!e.manager_signed||!e.manager_current))unapproved++}if(!j.people.length){let tr=document.createElement('tr'),td=document.createElement('td');td.colSpan=6;td.textContent='No recorded punches or leave in this period.';tr.append(td);people.append(tr)}let reason=document.getElementById('closeReason'),why=[];if(bad)why.push(bad+(bad===1?' employee has':' employees have')+' punch problems');if(unapproved)why.push(unapproved+' reviews pending');if(p.status==='open'){closePeriod.disabled=false;if(why.length){reason.replaceChildren(tc.flag('Exceptions to report','check'),' '+why.join(', ')+'. Closure does not mark these records reviewed.')}else reason.textContent='Ready to close attendance report.'}else reason.textContent=bad?'Punch exceptions remain recorded for manager review.':''}async function review(p,e){if(!await tc.ask({title:'Mark attendance reviewed for '+e.name+'?',ok:'Mark reviewed'}))return;try{await post('/api/v1/admin/periods',{action:'sign',starts:p.starts,ends:p.ends,employee_id:e.id,reason:'Attendance reviewed by manager.'});tc.ok(message,'Review recorded for '+e.name+'.');await refresh()}catch(x){tc.err(message,x)}}cycle.onchange=()=>{anchorLabel.hidden=(cycle.value==='semi_monthly'||cycle.value==='monthly')};settings.onsubmit=async ev=>{ev.preventDefault();if(!await tc.ask({title:'Change future attendance periods?',text:'Unused future open periods will be replaced. Reviewed and closed periods stay unchanged.',ok:'Change periods'}))return;try{await post('/api/v1/admin/periods/cycle',{...Object.fromEntries(new FormData(settings))});tc.ok(message,'Attendance schedule saved.');await refresh()}catch(x){tc.err(message,x)}};closePeriod.onclick=async()=>{let p=selected;if(!p||p.status==='closed')return;let reason=await tc.reason({title:'Close '+p.starts+' to '+p.ends+'?',text:'Unresolved attendance exceptions will remain flagged; later changes require a revised report.',label:'Reason for closing',value:'Attendance period closed by manager',min:5,ok:'Close period'});if(!reason)return;try{await post('/api/v1/admin/periods',{action:'close',starts:p.starts,ends:p.ends,employee_id:0,reason});tc.ok(message,'Period closed.');await refresh()}catch(x){tc.err(message,x)}};(async()=>{try{let c=await api('/api/v1/admin/periods/cycle');cycle.value=c.cycle;anchor.value=c.anchor;anchorLabel.hidden=(c.cycle==='semi_monthly'||c.cycle==='monthly');await refresh()}catch(x){tc.err(message,x)}})();</script>" UI_FOOT);}

