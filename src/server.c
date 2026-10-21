#define _GNU_SOURCE
#include <sys/wait.h>
#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <signal.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <crypt.h>
#include <time.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sendfile.h>
#include <sys/file.h>
#include <sys/utsname.h>
#include <sys/sysinfo.h>
#include <sys/timex.h>
#include <dirent.h>
#include "oled.h"
#include "version.h"
#include "wifi_signal.h"

static sqlite3 *db;
static const char *device_key;
/* Single-threaded request context: the card that approved the current edit. */
static char approval_card_uid[48];

static volatile sig_atomic_t stop_flag;
static void stop_handler(int signum){(void)signum;stop_flag=1;}
static int sql(const char *s){char *err=NULL;int r=sqlite3_exec(db,s,NULL,NULL,&err);if(r!=SQLITE_OK){fprintf(stderr,"sqlite: %s\n",err?err:"unknown");sqlite3_free(err);}return r==SQLITE_OK;}
static int seed_templates(void);
static void approval_uid_sql(sqlite3_context *c,int argc,sqlite3_value **argv){
 (void)argc;(void)argv;
 if(approval_card_uid[0])sqlite3_result_text(c,approval_card_uid,-1,SQLITE_TRANSIENT);else sqlite3_result_null(c);
}
static int rec_schema(void);
static int note_link_schema(void);
static int timeoff_schema(void);
static const char*note_link_base(char*,size_t);
static int welcome_enqueue(long employee_id);
static void welcome_resend(int fd,int who,const char *body);
/* Only additions needed by current RC3 settings; no historical schema conversion. */
static int current_settings_schema(void){
 const char *tables[]={"smtp_settings","employees","payroll_settings","review_send_settings"};
 const char *columns[]={"sender_name","vacation_days","send_time","send_time"};
 const char *definitions[]={"TEXT NOT NULL DEFAULT ''","REAL CHECK(vacation_days IS NULL OR (vacation_days BETWEEN 0 AND 366 AND vacation_days*2=CAST(vacation_days*2 AS INTEGER)))","TEXT NOT NULL DEFAULT '00:00'","TEXT NOT NULL DEFAULT '00:00'"};
 for(int i=0;i<4;i++){
  sqlite3_stmt *q=NULL;int found=0;char query[100];snprintf(query,sizeof query,"PRAGMA table_info(%s)",tables[i]);
  if(sqlite3_prepare_v2(db,query,-1,&q,NULL)!=SQLITE_OK)return 0;
  while(sqlite3_step(q)==SQLITE_ROW)if(!strcmp((const char*)sqlite3_column_text(q,1),columns[i]))found=1;
  sqlite3_finalize(q);
  if(!found){char *change=sqlite3_mprintf("ALTER TABLE %s ADD COLUMN %s %s",tables[i],columns[i],definitions[i]);int ok=change&&sql(change);sqlite3_free(change);if(!ok)return 0;}
 }
 return 1;
}
static int init_db(const char *path){
 if(sqlite3_open(path,&db)!=SQLITE_OK)return 0;
 sqlite3_busy_timeout(db,3000);
 if(sqlite3_create_function(db,"approval_uid",0,SQLITE_UTF8,NULL,approval_uid_sql,NULL,NULL)!=SQLITE_OK)return 0;
 return sql("PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL; PRAGMA foreign_keys=ON;"
 "CREATE TABLE IF NOT EXISTS employees(id INTEGER PRIMARY KEY,name TEXT NOT NULL,email TEXT,phone TEXT NOT NULL DEFAULT '',active INTEGER NOT NULL DEFAULT 1,late_email_enabled INTEGER NOT NULL DEFAULT 1,auto_review_enabled INTEGER NOT NULL DEFAULT 1,first_name TEXT NOT NULL DEFAULT '',last_name TEXT NOT NULL DEFAULT '',position TEXT NOT NULL DEFAULT '',schedule_exempt INTEGER NOT NULL DEFAULT 0 CHECK(schedule_exempt IN (0,1)),attendance_mode TEXT NOT NULL DEFAULT 'punch_required' CHECK(attendance_mode IN ('punch_required','vacation_only')),vacation_days REAL CHECK(vacation_days IS NULL OR (vacation_days BETWEEN 0 AND 366 AND vacation_days*2=CAST(vacation_days*2 AS INTEGER))),welcomed_at TEXT,created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
 "CREATE TABLE IF NOT EXISTS employee_attendance_modes(employee_id INTEGER NOT NULL REFERENCES employees(id),effective_at TEXT NOT NULL,mode TEXT NOT NULL CHECK(mode IN ('punch_required','vacation_only')),PRIMARY KEY(employee_id,effective_at));"
 "CREATE TRIGGER IF NOT EXISTS employee_mode_initial AFTER INSERT ON employees BEGIN INSERT INTO employee_attendance_modes(employee_id,effective_at,mode) VALUES(NEW.id,NEW.created_at,NEW.attendance_mode); END;"
 "CREATE TRIGGER IF NOT EXISTS employee_mode_changed AFTER UPDATE OF attendance_mode ON employees WHEN OLD.attendance_mode<>NEW.attendance_mode BEGIN INSERT INTO employee_attendance_modes(employee_id,effective_at,mode) VALUES(NEW.id,strftime('%Y-%m-%dT%H:%M:%fZ','now'),NEW.attendance_mode) ON CONFLICT(employee_id,effective_at) DO UPDATE SET mode=excluded.mode; END;"
 "CREATE TABLE IF NOT EXISTS cards(uid TEXT PRIMARY KEY COLLATE NOCASE,employee_id INTEGER NOT NULL REFERENCES employees(id),active INTEGER NOT NULL DEFAULT 1);"
 "CREATE TABLE IF NOT EXISTS card_ownership_history(id INTEGER PRIMARY KEY,uid TEXT NOT NULL COLLATE NOCASE,employee_id INTEGER NOT NULL REFERENCES employees(id),active INTEGER NOT NULL CHECK(active IN(0,1)),effective_at TEXT NOT NULL);"
 "CREATE INDEX IF NOT EXISTS card_ownership_lookup ON card_ownership_history(uid,effective_at,id);"
 "CREATE TRIGGER IF NOT EXISTS card_history_new AFTER INSERT ON cards BEGIN INSERT INTO card_ownership_history(uid,employee_id,active,effective_at) VALUES(NEW.uid,NEW.employee_id,NEW.active,strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;"
 "CREATE TRIGGER IF NOT EXISTS card_history_change AFTER UPDATE OF employee_id,active ON cards WHEN OLD.employee_id<>NEW.employee_id OR OLD.active<>NEW.active BEGIN INSERT INTO card_ownership_history(uid,employee_id,active,effective_at) VALUES(NEW.uid,NEW.employee_id,NEW.active,strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;"
 "CREATE TABLE IF NOT EXISTS employee_active_history(id INTEGER PRIMARY KEY,employee_id INTEGER NOT NULL REFERENCES employees(id),active INTEGER NOT NULL CHECK(active IN(0,1)),effective_at TEXT NOT NULL);"
 "CREATE INDEX IF NOT EXISTS employee_active_lookup ON employee_active_history(employee_id,effective_at,id);"
 "CREATE TRIGGER IF NOT EXISTS employee_active_new AFTER INSERT ON employees BEGIN INSERT INTO employee_active_history(employee_id,active,effective_at) VALUES(NEW.id,NEW.active,NEW.created_at); END;"
 "CREATE TRIGGER IF NOT EXISTS employee_active_change AFTER UPDATE OF active ON employees WHEN OLD.active<>NEW.active BEGIN INSERT INTO employee_active_history(employee_id,active,effective_at) VALUES(NEW.id,NEW.active,strftime('%Y-%m-%dT%H:%M:%fZ','now')); END;"
 "CREATE TABLE IF NOT EXISTS swipes(event_id TEXT PRIMARY KEY,clock_id TEXT NOT NULL,card_uid TEXT NOT NULL,employee_id INTEGER,swiped_at TEXT NOT NULL,received_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
 "CREATE INDEX IF NOT EXISTS swipes_employee_time ON swipes(employee_id,swiped_at);"
 "CREATE TABLE IF NOT EXISTS lite_install_identity(id INTEGER PRIMARY KEY CHECK(id=1),value TEXT NOT NULL CHECK(length(value)=32));"
 "INSERT OR IGNORE INTO lite_install_identity(id,value) VALUES(1,lower(hex(randomblob(16))));"
 "CREATE TABLE IF NOT EXISTS smtp_settings(id INTEGER PRIMARY KEY CHECK(id=1),url TEXT NOT NULL DEFAULT '',username TEXT NOT NULL DEFAULT '',password TEXT NOT NULL DEFAULT '',tls_mode TEXT NOT NULL DEFAULT 'starttls',sender TEXT NOT NULL DEFAULT '',sender_name TEXT NOT NULL DEFAULT '');"
 "CREATE TABLE IF NOT EXISTS email_templates(id TEXT PRIMARY KEY,subject TEXT NOT NULL,body TEXT NOT NULL,enabled INTEGER NOT NULL DEFAULT 1);"
 "CREATE TABLE IF NOT EXISTS email_retry_audit(id INTEGER PRIMARY KEY,email_id INTEGER NOT NULL,manager_id INTEGER NOT NULL,retried_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
 "CREATE TABLE IF NOT EXISTS template_restore_audit(id INTEGER PRIMARY KEY,template_id TEXT NOT NULL,manager_id INTEGER NOT NULL,approved_uid TEXT NOT NULL,restored_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
 "CREATE TABLE IF NOT EXISTS email_queue(id INTEGER PRIMARY KEY,recipient TEXT NOT NULL,subject TEXT NOT NULL,body TEXT NOT NULL,status TEXT NOT NULL DEFAULT 'pending',next_attempt TEXT,attempts INTEGER NOT NULL DEFAULT 0,event_id TEXT UNIQUE,last_error TEXT NOT NULL DEFAULT '',csv_attachment TEXT NOT NULL DEFAULT '',message_token TEXT NOT NULL DEFAULT '');"
 "CREATE TRIGGER IF NOT EXISTS lite_email_token_new AFTER INSERT ON email_queue WHEN NEW.message_token='' BEGIN UPDATE email_queue SET message_token=lower(hex(randomblob(16))) WHERE id=NEW.id; END;"
 "CREATE TABLE IF NOT EXISTS attendance_events(event_id TEXT PRIMARY KEY REFERENCES swipes(event_id),employee_id INTEGER NOT NULL REFERENCES employees(id),kind TEXT NOT NULL CHECK(kind IN ('IN','OUT')),late_minutes INTEGER NOT NULL DEFAULT 0,exception TEXT NOT NULL DEFAULT '',paired_in_event TEXT);"
 "CREATE INDEX IF NOT EXISTS attendance_events_employee ON attendance_events(employee_id);"
 "CREATE TABLE IF NOT EXISTS schedule_profiles(employee_id INTEGER PRIMARY KEY REFERENCES employees(id),pay_type TEXT NOT NULL CHECK(pay_type IN ('salary','hourly')),start_time TEXT NOT NULL DEFAULT '09:00',end_time TEXT NOT NULL DEFAULT '17:00',weekday_mask INTEGER NOT NULL DEFAULT 31,schedule_mode TEXT NOT NULL DEFAULT 'dated_shifts' CHECK(schedule_mode IN ('recurring_weekly','dated_shifts','none')));"
 "CREATE TABLE IF NOT EXISTS payroll_settings(id INTEGER PRIMARY KEY CHECK(id=1),anchor_payday TEXT NOT NULL DEFAULT '',lead_days INTEGER NOT NULL DEFAULT 3,recipient TEXT NOT NULL DEFAULT '',enabled INTEGER NOT NULL DEFAULT 0,enabled_since TEXT NOT NULL DEFAULT '',send_time TEXT NOT NULL DEFAULT '00:00');"
 "CREATE TABLE IF NOT EXISTS payroll_manual_requests(period_id INTEGER PRIMARY KEY,requested_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
 "CREATE TABLE IF NOT EXISTS lite_payroll_queue_failures(period_id INTEGER PRIMARY KEY,reason TEXT NOT NULL,failed_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
 "CREATE TABLE IF NOT EXISTS payroll_authorization(id INTEGER PRIMARY KEY CHECK(id=1),requested_by INTEGER NOT NULL,change_body TEXT NOT NULL,armed_at INTEGER NOT NULL);"
 "CREATE TABLE IF NOT EXISTS schedule_shifts(id INTEGER PRIMARY KEY,employee_id INTEGER NOT NULL REFERENCES employees(id),work_date TEXT NOT NULL,start_time TEXT NOT NULL,end_time TEXT NOT NULL,UNIQUE(employee_id,work_date,start_time));"
 "CREATE TABLE IF NOT EXISTS schedule_authorization(id INTEGER PRIMARY KEY CHECK(id=1),requested_by INTEGER NOT NULL,change_body TEXT NOT NULL,armed_at INTEGER NOT NULL);"
 "CREATE TABLE IF NOT EXISTS schedule_change_audit(id INTEGER PRIMARY KEY,manager_id INTEGER NOT NULL REFERENCES employees(id),change_body TEXT NOT NULL,recorded_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
 "CREATE TABLE IF NOT EXISTS employee_change_audit(id INTEGER PRIMARY KEY,manager_id INTEGER NOT NULL REFERENCES employees(id),employee_id INTEGER,change_body TEXT NOT NULL,recorded_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
 "CREATE TABLE IF NOT EXISTS user_accounts(id INTEGER PRIMARY KEY,employee_id INTEGER UNIQUE REFERENCES employees(id),password_hash TEXT,disabled INTEGER NOT NULL DEFAULT 0);"
 "CREATE TABLE IF NOT EXISTS login_sessions(token TEXT PRIMARY KEY,employee_id INTEGER NOT NULL REFERENCES employees(id),expires_at INTEGER NOT NULL);"
 "CREATE TABLE IF NOT EXISTS review_send_settings(id INTEGER PRIMARY KEY CHECK(id=1),enabled INTEGER NOT NULL DEFAULT 1,days_after INTEGER NOT NULL DEFAULT 1 CHECK(days_after BETWEEN 0 AND 7),send_time TEXT NOT NULL DEFAULT '00:00');"
 "INSERT OR IGNORE INTO review_send_settings(id) VALUES(1);"
 "CREATE TABLE IF NOT EXISTS review_links(employee_id INTEGER NOT NULL REFERENCES employees(id),period_id INTEGER NOT NULL REFERENCES pay_periods(id),token_hash TEXT NOT NULL,expires_at INTEGER NOT NULL,created_by INTEGER NOT NULL REFERENCES employees(id),created_at INTEGER NOT NULL,revoked INTEGER NOT NULL DEFAULT 0,PRIMARY KEY(employee_id,period_id));"
 "CREATE TABLE IF NOT EXISTS account_authorization(id INTEGER PRIMARY KEY CHECK(id=1),requested_by INTEGER NOT NULL,employee_id INTEGER NOT NULL,action TEXT NOT NULL,proposed_hash TEXT,armed_at INTEGER NOT NULL);"
 "CREATE TABLE IF NOT EXISTS account_change_audit(id INTEGER PRIMARY KEY,manager_id INTEGER NOT NULL,target_id INTEGER NOT NULL,action TEXT NOT NULL,at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
 "CREATE TABLE IF NOT EXISTS review_link_audit(id INTEGER PRIMARY KEY,manager_id INTEGER NOT NULL,employee_id INTEGER NOT NULL,period_id INTEGER NOT NULL,action TEXT NOT NULL,at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
 "CREATE TABLE IF NOT EXISTS oled_previews(clock_id TEXT PRIMARY KEY,frame TEXT NOT NULL,updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
 "CREATE TABLE IF NOT EXISTS swipe_notes(event_id TEXT PRIMARY KEY REFERENCES swipes(event_id),note TEXT NOT NULL DEFAULT '',updated_by INTEGER REFERENCES employees(id),updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
 "CREATE TABLE IF NOT EXISTS swipe_note_history(id INTEGER PRIMARY KEY,event_id TEXT NOT NULL,note TEXT NOT NULL,updated_by INTEGER,updated_at TEXT NOT NULL,approved_card_uid TEXT);"
 "CREATE TRIGGER IF NOT EXISTS swipe_note_history_insert AFTER INSERT ON swipe_notes BEGIN INSERT INTO swipe_note_history(event_id,note,updated_by,updated_at,approved_card_uid) VALUES(NEW.event_id,NEW.note,NEW.updated_by,NEW.updated_at,approval_uid()); END;"
 "CREATE TRIGGER IF NOT EXISTS swipe_note_history_update AFTER UPDATE ON swipe_notes BEGIN INSERT INTO swipe_note_history(event_id,note,updated_by,updated_at,approved_card_uid) VALUES(NEW.event_id,NEW.note,NEW.updated_by,NEW.updated_at,approval_uid()); END;"
 "CREATE TABLE IF NOT EXISTS lite_period_snapshots(period_id INTEGER NOT NULL REFERENCES pay_periods(id),revision INTEGER NOT NULL CHECK(revision>=1),starts TEXT NOT NULL,ends TEXT NOT NULL,csv TEXT NOT NULL,created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),PRIMARY KEY(period_id,revision));"
 "CREATE TRIGGER IF NOT EXISTS lite_snapshot_no_replace BEFORE INSERT ON lite_period_snapshots WHEN EXISTS(SELECT 1 FROM lite_period_snapshots WHERE period_id=NEW.period_id AND revision=NEW.revision) BEGIN SELECT RAISE(IGNORE); END;"
 "CREATE TRIGGER IF NOT EXISTS lite_snapshot_no_update BEFORE UPDATE ON lite_period_snapshots BEGIN SELECT RAISE(ABORT,'payroll snapshot immutable'); END;"
 "CREATE TRIGGER IF NOT EXISTS lite_snapshot_no_delete BEFORE DELETE ON lite_period_snapshots BEGIN SELECT RAISE(ABORT,'payroll snapshot immutable'); END;"
 "CREATE TABLE IF NOT EXISTS admin_export_audit(id INTEGER PRIMARY KEY,manager_id INTEGER NOT NULL,scope TEXT NOT NULL,format TEXT NOT NULL,from_date TEXT NOT NULL,to_date TEXT NOT NULL,created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
 "CREATE TABLE IF NOT EXISTS rejected_uploads(event_id TEXT PRIMARY KEY,clock_id TEXT NOT NULL,card_uid TEXT NOT NULL,swiped_at TEXT NOT NULL,http_status INTEGER NOT NULL,reason TEXT NOT NULL,rejected_at TEXT NOT NULL,received_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
 "CREATE TABLE IF NOT EXISTS unknown_cards(uid TEXT PRIMARY KEY COLLATE NOCASE,clock_id TEXT NOT NULL,first_seen TEXT NOT NULL,last_seen TEXT NOT NULL,seen_count INTEGER NOT NULL DEFAULT 1);"
 "CREATE TABLE IF NOT EXISTS employee_roles(employee_id INTEGER REFERENCES employees(id),role TEXT NOT NULL,PRIMARY KEY(employee_id,role));"
 "CREATE TABLE IF NOT EXISTS manager_cards(uid TEXT PRIMARY KEY COLLATE NOCASE,manager_id INTEGER NOT NULL REFERENCES employees(id),active INTEGER NOT NULL DEFAULT 1);"
 "CREATE TRIGGER IF NOT EXISTS guard_employee_card_insert BEFORE INSERT ON cards WHEN EXISTS(SELECT 1 FROM manager_cards WHERE uid=NEW.uid COLLATE NOCASE) BEGIN SELECT RAISE(ABORT,'UID reserved for manager card'); END;"
 "CREATE TRIGGER IF NOT EXISTS guard_employee_card_update BEFORE UPDATE OF uid ON cards WHEN EXISTS(SELECT 1 FROM manager_cards WHERE uid=NEW.uid COLLATE NOCASE) BEGIN SELECT RAISE(ABORT,'UID reserved for manager card'); END;"
 "CREATE TRIGGER IF NOT EXISTS guard_manager_card_insert BEFORE INSERT ON manager_cards WHEN EXISTS(SELECT 1 FROM cards WHERE uid=NEW.uid COLLATE NOCASE) BEGIN SELECT RAISE(ABORT,'UID reserved for employee card'); END;"
 "CREATE TRIGGER IF NOT EXISTS guard_manager_card_update BEFORE UPDATE OF uid ON manager_cards WHEN EXISTS(SELECT 1 FROM cards WHERE uid=NEW.uid COLLATE NOCASE) BEGIN SELECT RAISE(ABORT,'UID reserved for employee card'); END;"
 "CREATE TABLE IF NOT EXISTS manager_scan(id INTEGER PRIMARY KEY CHECK(id=1),armed_at INTEGER NOT NULL DEFAULT 0,uid TEXT NOT NULL DEFAULT '',scanned_at INTEGER NOT NULL DEFAULT 0,request_token TEXT NOT NULL DEFAULT '',scan_mode TEXT NOT NULL DEFAULT 'enrollment');"
 "CREATE TABLE IF NOT EXISTS first_manager_setup(id INTEGER PRIMARY KEY CHECK(id=1),name TEXT NOT NULL,email TEXT NOT NULL,position TEXT NOT NULL,password_hash TEXT NOT NULL,request_token TEXT NOT NULL,armed_at INTEGER NOT NULL);"
 "INSERT OR IGNORE INTO manager_scan(id) VALUES(1);"
 "CREATE TABLE IF NOT EXISTS manager_authorization(id INTEGER PRIMARY KEY CHECK(id=1),requested_by INTEGER NOT NULL,change_body TEXT NOT NULL,armed_at INTEGER NOT NULL);"
 "CREATE TABLE IF NOT EXISTS statutory_holidays(day TEXT PRIMARY KEY,name TEXT NOT NULL);"
 "CREATE TABLE IF NOT EXISTS holiday_settings(id INTEGER PRIMARY KEY CHECK(id=1),skip_alerts INTEGER NOT NULL DEFAULT 0);"
 "CREATE TABLE IF NOT EXISTS registered_clocks(clock_id TEXT PRIMARY KEY,device_key TEXT NOT NULL UNIQUE,active INTEGER NOT NULL DEFAULT 1 CHECK(active IN(0,1)));"
 "CREATE TABLE IF NOT EXISTS clock_names(clock_id TEXT PRIMARY KEY REFERENCES registered_clocks(clock_id),name TEXT NOT NULL CHECK(length(name) BETWEEN 1 AND 60));"
 "CREATE TABLE IF NOT EXISTS adoption_codes(code TEXT PRIMARY KEY,created_by INTEGER NOT NULL,expires_at INTEGER NOT NULL,used INTEGER NOT NULL DEFAULT 0);"
 "CREATE TABLE IF NOT EXISTS adoption_requests(id TEXT PRIMARY KEY,code TEXT NOT NULL,clock_id TEXT NOT NULL,status TEXT NOT NULL DEFAULT 'pending',device_key TEXT NOT NULL DEFAULT '',created_at INTEGER NOT NULL,UNIQUE(code,clock_id));"
 "CREATE TABLE IF NOT EXISTS clock_dim_settings(clock_id TEXT PRIMARY KEY REFERENCES registered_clocks(clock_id),enabled INTEGER NOT NULL DEFAULT 1 CHECK(enabled IN(0,1)),start_min INTEGER NOT NULL DEFAULT 1140 CHECK(start_min BETWEEN 0 AND 1439),end_min INTEGER NOT NULL DEFAULT 420 CHECK(end_min BETWEEN 0 AND 1439),normal_contrast INTEGER NOT NULL DEFAULT 127 CHECK(normal_contrast BETWEEN 1 AND 255),dim_contrast INTEGER NOT NULL DEFAULT 24 CHECK(dim_contrast BETWEEN 1 AND 255),font TEXT NOT NULL DEFAULT 'DejaVuSansMono.ttf');"
 "CREATE TABLE IF NOT EXISTS clock_late_settings(clock_id TEXT PRIMARY KEY,grace_minutes INTEGER NOT NULL DEFAULT 5 CHECK(grace_minutes BETWEEN 0 AND 60));"
 "CREATE TABLE IF NOT EXISTS clock_duplicate_settings(clock_id TEXT PRIMARY KEY,duplicate_seconds INTEGER NOT NULL DEFAULT 120 CHECK(duplicate_seconds BETWEEN 0 AND 300));"
 "INSERT OR IGNORE INTO holiday_settings(id,skip_alerts) VALUES(1,0);"
 "CREATE TABLE IF NOT EXISTS system_actions(id INTEGER PRIMARY KEY CHECK(id=1),requested_by INTEGER,action TEXT,armed_at INTEGER);"
 "CREATE TABLE IF NOT EXISTS backup_settings(id INTEGER PRIMARY KEY CHECK(id=1),email_enabled INTEGER NOT NULL DEFAULT 0,manager_id INTEGER NOT NULL DEFAULT 0);"
 "INSERT OR IGNORE INTO backup_settings(id,email_enabled,manager_id) VALUES(1,0,0);"
 "CREATE TABLE IF NOT EXISTS backup_download(id INTEGER PRIMARY KEY CHECK(id=1),requested_by INTEGER NOT NULL,filename TEXT NOT NULL,token TEXT NOT NULL DEFAULT '',approved_at INTEGER NOT NULL DEFAULT 0);"
 "CREATE TABLE IF NOT EXISTS backup_restore(id INTEGER PRIMARY KEY CHECK(id=1),requested_by INTEGER NOT NULL,filename TEXT NOT NULL);"
 "CREATE TABLE IF NOT EXISTS backup_restore_audit(id INTEGER PRIMARY KEY,manager_id INTEGER NOT NULL,filename TEXT NOT NULL,requested_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
 "CREATE TABLE IF NOT EXISTS backup_download_audit(id INTEGER PRIMARY KEY,manager_id INTEGER NOT NULL,filename TEXT NOT NULL,downloaded_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
 "CREATE TABLE IF NOT EXISTS card_replacement_audit(id INTEGER PRIMARY KEY,manager_id INTEGER NOT NULL REFERENCES employees(id),employee_id INTEGER NOT NULL REFERENCES employees(id),old_uid TEXT NOT NULL,new_uid TEXT NOT NULL,replaced_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
) && current_settings_schema() && seed_templates() && rec_schema() && note_link_schema() && timeoff_schema();
}
typedef struct {const char *id,*subject,*body;} email_template_default;
static const email_template_default template_defaults[]={
  {"swipe_employee","Timeclock: Swipe recorded","Hello {{employee_name}},\n\nYour card was read at {{swipe_time}} on {{swipe_date}} at {{clock_name}}."},
  {"late_arrival_employee","Timeclock: Late arrival on {{attendance_date}}","Hello {{employee_name}},\n\nScheduled start: {{scheduled_start}}\nRecorded IN: {{actual_start}}\nLate: {{late_minutes}} minutes\n\nTo add a reason, use this link within seven days:\n{{note_link}}\n\nContact your manager about any approved exception."},
  {"late_arrival_manager","Timeclock: {{employee_name}} arrived {{late_minutes}} minutes late","Employee: {{employee_name}}\nDate: {{attendance_date}}\nScheduled start: {{scheduled_start}}\nRecorded IN: {{actual_start}}\nLate: {{late_minutes}} minutes"},
  {"attendance_report","Timeclock attendance: {{period_start}} to {{period_end}}","{{attendance_summary}}"},
  {"welcome_employee","Welcome to the timeclock","Hello {{employee_name}},\n\nYou can request time off at {{timeoff_link}}.\n\nFor help with time off or your recorded timesheet, contact your manager."},
  {"override_employee","Timesheet approved without your confirmation","Your hours for {{period_start}} to {{period_end}} ({{hours}} h) were approved by {{manager_name}} without your confirmation.\nReason: {{reason}}\nContact your manager if anything is wrong."},
  {"missing_out_employee","Timeclock: No OUT punch for {{attendance_date}}","Hello {{employee_name}},\n\nNo OUT punch has been recorded after your {{scheduled_end}} finish on {{attendance_date}}.\n\nIf you left without tapping out, contact your manager to record your departure time. If you are still working, ignore this message."},
  {"auto_review_employee","Your timesheet for {{period_start}} to {{period_end}}","{{review_link}}\n\nHello {{employee_name}},\n\nYour timesheet for {{period_start}} to {{period_end}} is ready.\nPlease review and approve it using the link. If anything is wrong, tell your manager.\n{{missing_punch_note}}\nContact your manager if something looks wrong."},
  {"review_dispatch_payroll","Timeclock: Pay reviews issued for {{period_start}} to {{period_end}}","Pay review notices were queued for period {{period_start}} to {{period_end}}.\nEmployees emailed: {{sent_count}}\n\nThis confirms queuing, not delivery. Check the email queue for failures or employees without addresses."},
  {"timeoff_received_employee","Time-off request received: {{period_start}} to {{period_end}}","Hello {{employee_name}},\n\nYour {{request_type}} request for {{period_start}} to {{period_end}} ({{days_label}}, returning {{return_date}}) was sent to your manager.\n\nStatus: Pending."},
  {"timeoff_received_manager","Time-off request: {{employee_name}}","{{employee_name}} requested {{request_type}} from {{period_start}} to {{period_end}}.\nWorking days: {{days}}\nReturns: {{return_date}}\nNotes: {{note}}\n\nReview under Time off requests."},
  {"timeoff_decision_employee","Time-off request {{decision}}: {{period_start}} to {{period_end}}","Hello {{employee_name}},\n\nYour time-off request for {{period_start}} to {{period_end}} was {{decision}} by {{manager_name}}.\nManager note: {{note}}\n\nYou can view your history in Time off requests."},
  {"timeoff_cancelled_employee","Time-off approval cancelled: {{period_start}} to {{period_end}}","Hello {{employee_name}},\n\nYour previously approved time off from {{period_start}} to {{period_end}} has been cancelled by {{manager_name}}.\nManager note: {{note}}\n\nPlease contact your manager about your work schedule."},
  {"review_reminder_employee","Timesheet for {{period_start}} to {{period_end}}","Hello {{employee_name}},\n\nPlease review your recorded hours for {{period_start}} to {{period_end}}:\n{{review_link}}\n\nContact your manager if something needs correcting."},
  {"email_failure_payroll","Timeclock: Email delivery failed","Hello,\n\nThe timeclock could not deliver an email after repeated attempts.\nRecipient: {{recipient}}\nSubject: {{failed_subject}}\n\nCheck the Email queue and contact the recipient if needed."},
  {"missing_in_employee","Timeclock: No IN punch for {{attendance_date}}","Hello {{employee_name}},\n\nNo IN punch has been recorded for your {{scheduled_start}} start on {{attendance_date}}.\n\nIf you are working, contact your manager to record your start time. If you are away or your schedule changed, let your manager know. If you already tapped in, ignore this message."},
 };


static const email_template_default *find_template_default(const char *id){
 for(size_t i=0;i<sizeof(template_defaults)/sizeof(template_defaults[0]);i++)if(!strcmp(template_defaults[i].id,id))return &template_defaults[i];
 return NULL;
}
static int seed_templates(void){
 sqlite3_stmt*q=NULL;
 if(sqlite3_prepare_v2(db,"INSERT OR IGNORE INTO email_templates(id,subject,body,enabled) VALUES(?1,?2,?3,CASE WHEN ?1 IN ('late_arrival_employee','late_arrival_manager','attendance_report','welcome_employee','auto_review_employee','review_dispatch_payroll','timeoff_received_employee','timeoff_received_manager','timeoff_decision_employee','timeoff_cancelled_employee','review_reminder_employee','missing_in_employee','missing_out_employee','email_failure_payroll') THEN 1 ELSE 0 END)",-1,&q,NULL)!=SQLITE_OK)return 0;
 int ok=1;
 for(size_t i=0;i<sizeof(template_defaults)/sizeof(template_defaults[0]);i++){
  sqlite3_reset(q);sqlite3_clear_bindings(q);
  sqlite3_bind_text(q,1,template_defaults[i].id,-1,SQLITE_STATIC);
  sqlite3_bind_text(q,2,template_defaults[i].subject,-1,SQLITE_STATIC);
  sqlite3_bind_text(q,3,template_defaults[i].body,-1,SQLITE_STATIC);
  if(sqlite3_step(q)!=SQLITE_DONE){ok=0;break;}
 }
 sqlite3_finalize(q);
 return ok;
}
static int valid_id(const char *s,size_t max){size_t n=strlen(s);if(!n||n>max)return 0;for(;*s;s++)if(!isalnum((unsigned char)*s)&&!strchr("-_:.+",*s))return 0;return 1;}
static int json_string(const char *body,const char *key,char *out,size_t cap){
 char needle[80];snprintf(needle,sizeof needle,"\"%s\"",key);const char *p=strstr(body,needle);if(!p)return 0;p+=strlen(needle);while(isspace((unsigned char)*p))p++;if(*p++!=':')return 0;while(isspace((unsigned char)*p))p++;if(*p++!='"')return 0;
 size_t n=0;while(*p&&*p!='"'){if(*p=='\\'||(unsigned char)*p<32||n+1>=cap)return 0;out[n++]=*p++;}if(*p!='"')return 0;out[n]=0;return 1;
}
static const char *reason_phrase(int code){switch(code){case 200:return "OK";case 201:return "Created";case 202:return "Accepted";case 413:return "Payload Too Large";case 303:return "See Other";case 400:return "Bad Request";case 401:return "Unauthorized";case 403:return "Forbidden";case 404:return "Not Found";case 409:return "Conflict";case 422:return "Unprocessable Content";case 429:return "Too Many Requests";case 500:return "Internal Server Error";default:return "Error";}}
static int send_response(int fd,int code,const char *type,const char *body){char header[512];size_t n=strlen(body);int k=snprintf(header,sizeof header,"HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nX-Content-Type-Options: nosniff\r\n\r\n",code,reason_phrase(code),type,n);if(k<=0||(size_t)k>=sizeof header)return 0;return send(fd,header,(size_t)k,0)==k&&send(fd,body,n,0)==(ssize_t)n;}
#include "ui.inc"
static int clock_synced(void);
static long long tls_not_after(void);
static int tls_temporary(void);
static int form_value(const char*,const char*,char*,size_t);
static void diag_escape(const char*,char*,size_t);
static int random_hex(char*out,size_t bytes){
 unsigned char b[48];if(bytes>sizeof b)return 0;
 int f=open("/dev/urandom",O_RDONLY);if(f<0)return 0;
 size_t got=0;while(got<bytes){ssize_t n=read(f,b+got,bytes-got);if(n<=0){close(f);return 0;}got+=(size_t)n;}close(f);
 static const char hex[]="0123456789abcdef";
 for(size_t i=0;i<bytes;i++){out[2*i]=hex[b[i]>>4];out[2*i+1]=hex[b[i]&15];}out[2*bytes]=0;return 1;
}
static int session_user(const char *req){
 const char *head=strstr(req,"\r\n\r\n");if(!head)return 0;
 const char *line=req;char token[65]="";
 while(line<head){const char*end=strstr(line,"\r\n");if(!end||end>head)break;
 if((size_t)(end-line)>7&&!strncasecmp(line,"Cookie:",7)){
  const char*c=line+7;while(c<end){while(c<end&&(*c==' '||*c==';'))c++;
   if(end-c>=13&&!strncmp(c,"tc_session=",11)){c+=11;size_t k=0;while(c<end&&*c!=';'&&k<64)token[k++]=*c++;token[k]=0;break;}
   while(c<end&&*c!=';')c++;
  }
 }line=end+2;}
 if(strlen(token)!=64)return 0;
 sqlite3_stmt*q=NULL;int id=0;
 const char*sqltext="SELECT e.id FROM login_sessions s JOIN employees e ON e.id=s.employee_id JOIN user_accounts u ON u.employee_id=e.id JOIN employee_roles r ON r.employee_id=e.id AND r.role='manager' WHERE s.token=?1 AND s.expires_at>CAST(strftime('%s','now') AS INTEGER) AND e.active=1 AND u.disabled=0";
 if(sqlite3_prepare_v2(db,sqltext,-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,token,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW)id=sqlite3_column_int(q,0);}sqlite3_finalize(q);return id;
}
static void send_cookie_response(int fd,int code,const char*cookie,const char*location){
 char h[768];int n=snprintf(h,sizeof h,"HTTP/1.1 %d See Other\r\nLocation: %s\r\nSet-Cookie: tc_session=%s; HttpOnly; SameSite=Strict; Path=/; Max-Age=%d; Secure\r\nContent-Length: 0\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",code,location,cookie,*cookie?28800:0);
 if(n>0&&(size_t)n<sizeof h)(void)send(fd,h,(size_t)n,0);
}
/* 303 redirect that leaves the session cookie alone. location must be a fixed, trusted path. */
static void send_redirect(int fd,const char*location){
 char h[512];int n=snprintf(h,sizeof h,"HTTP/1.1 303 See Other\r\nLocation: %s\r\nContent-Length: 0\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",location);
 if(n>0&&(size_t)n<sizeof h)(void)send(fd,h,(size_t)n,0);
}
static int bootstrap_available(void);
/* Sign-in form. A failed attempt returns the same form with the reason in an error banner. */
static void login_page_msg(int fd,int code,const char*error){
 sqlite3_str*o=sqlite3_str_new(db);
 sqlite3_str_appendall(o,UI_HEAD_LOGIN("Sign in")"<form method='post' action='/login'><h1>Sign in</h1><p class='muted'>Manager account</p>");
 if(error&&*error){sqlite3_str_appendall(o,"<p class='banner banner-problem' role='alert'><span class='flag flag-problem'>Problem</span><span>");sqlite3_str_appendall(o,error);sqlite3_str_appendall(o,"</span></p>");}
 sqlite3_str_appendall(o,"<label>Email<input name='email' type='email' autocomplete='username' required></label><label>Password<input name='password' type='password' autocomplete='current-password' required></label><div class='actions'><button>Sign in</button></div></form>" UI_FOOT);
 char*page=sqlite3_str_finish(o);send_response(fd,code,"text/html; charset=utf-8",page?page:"");sqlite3_free(page);
}
static void login_page(int fd){
 if(bootstrap_available()){send_redirect(fd,"/setup");return;}
 login_page_msg(fd,200,NULL);
}
/* Login throttling. Each failed attempt costs a SHA-512 (100k rounds) computation on a
   single-threaded server, so unlimited guessing would also starve swipe ingestion.
   Per-IP: 5 failures per 5 minutes. Global: 20 failures per minute across all IPs. */
#define THROTTLE_SLOTS 64
static struct {uint32_t ip;time_t window_start;int failures;} throttle[THROTTLE_SLOTS];
static time_t global_window;static int global_failures;
static uint32_t current_peer;
static int login_blocked(uint32_t ip){
 time_t now=time(NULL);
 if(now-global_window>=60){global_window=now;global_failures=0;}
 if(global_failures>=20)return 1;
 for(int i=0;i<THROTTLE_SLOTS;i++)if(throttle[i].ip==ip&&throttle[i].failures){
  if(now-throttle[i].window_start>=300){throttle[i].failures=0;return 0;}
  return throttle[i].failures>=5;
 }
 return 0;
}
static void login_failed(uint32_t ip){
 time_t now=time(NULL);int slot=-1,oldest=0;
 global_failures++;
 for(int i=0;i<THROTTLE_SLOTS;i++){
  if(throttle[i].ip==ip&&throttle[i].failures){slot=i;break;}
  if(throttle[i].window_start<throttle[oldest].window_start)oldest=i;
 }
 if(slot<0||now-throttle[slot].window_start>=300){if(slot<0)slot=oldest;throttle[slot].ip=ip;throttle[slot].window_start=now;throttle[slot].failures=0;}
 throttle[slot].failures++;
}
static void login_succeeded(uint32_t ip){for(int i=0;i<THROTTLE_SLOTS;i++)if(throttle[i].ip==ip)throttle[i].failures=0;}
/* Enrollment is mandatory only when no usable manager RFID credential exists.
   No bootstrap bypass is exposed: a signed-in manager must scan a physical card. */
static int manager_card_setup_needed(void){
 sqlite3_stmt *q=NULL;int needed=1;
 const char *query="SELECT 1 FROM manager_cards mc JOIN employees e ON e.id=mc.manager_id JOIN employee_roles r ON r.employee_id=e.id AND r.role='manager' JOIN user_accounts ua ON ua.employee_id=e.id WHERE mc.active=1 AND e.active=1 AND ua.disabled=0 LIMIT 1";
 if(sqlite3_prepare_v2(db,query,-1,&q,NULL)==SQLITE_OK){int rc=sqlite3_step(q);needed=rc!=SQLITE_ROW;}
 sqlite3_finalize(q);return needed;
}
static void login_post_role(int fd,const char*body){
 if(login_blocked(current_peer)){login_page_msg(fd,429,"Too many failed attempts. Wait 5 minutes and try again.");return;}
 char email[220]="",password[256]="";
 if(!form_value(body,"email",email,sizeof email)||!form_value(body,"password",password,sizeof password)||strlen(password)<1){login_page_msg(fd,401,"Email or password is incorrect.");return;}
 sqlite3_stmt*q=NULL;int employee=0;char hash[256]="";
 const char*query="SELECT e.id,u.password_hash FROM employees e JOIN user_accounts u ON u.employee_id=e.id JOIN employee_roles r ON r.employee_id=e.id AND r.role='manager' WHERE e.email=?1 COLLATE NOCASE AND e.active=1 AND u.disabled=0";
 if(sqlite3_prepare_v2(db,query,-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,email,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW){employee=sqlite3_column_int(q,0);const unsigned char*p=sqlite3_column_text(q,1);if(p)snprintf(hash,sizeof hash,"%s",p);}}sqlite3_finalize(q);
 const char*computed=*hash?crypt(password,hash):NULL;
 if(!employee||!computed||strcmp(computed,hash)){login_failed(current_peer);login_page_msg(fd,401,"Email or password is incorrect.");return;}
 login_succeeded(current_peer);
 sql("DELETE FROM login_sessions WHERE expires_at<=CAST(strftime('%s','now') AS INTEGER)");
 char token[65];if(!random_hex(token,32)){login_page_msg(fd,500,"Sign-in failed on the clock. Try again.");return;}
 if(sqlite3_prepare_v2(db,"INSERT INTO login_sessions(token,employee_id,expires_at) VALUES(?1,?2,CAST(strftime('%s','now') AS INTEGER)+28800)",-1,&q,NULL)!=SQLITE_OK){login_page_msg(fd,500,"Sign-in failed on the clock. Try again.");return;}
 sqlite3_bind_text(q,1,token,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,2,employee);int ok=sqlite3_step(q)==SQLITE_DONE;sqlite3_finalize(q);
 if(!ok){login_page_msg(fd,500,"Sign-in failed on the clock. Try again.");return;}send_cookie_response(fd,303,token,manager_card_setup_needed()?"/manager-cards":"/");
}

static int form_value(const char *body,const char *name,char *dest,size_t cap){
 size_t keylen=strlen(name);
 for(const char *p=body;*p;){
  const char *eq=strchr(p,'=');if(!eq)return 0;
  const char *amp=strchr(eq+1,'&');if(!amp)amp=p+strlen(p);
  if((size_t)(eq-p)==keylen&&!strncmp(p,name,keylen)){
   size_t n=0;
   for(const char *v=eq+1;v<amp;v++){
    unsigned char ch=(unsigned char)*v;
    if(ch=='%'&&v+2<amp&&isxdigit((unsigned char)v[1])&&isxdigit((unsigned char)v[2])){
     char h[3]={v[1],v[2],0};ch=(unsigned char)strtoul(h,NULL,16);v+=2;
    }else if(ch=='+')ch=' ';
    if(ch<32||ch==127||n+1>=cap)return 0;
    dest[n++]=(char)ch;
   }dest[n]=0;return 1;
  }
  p=*amp?amp+1:amp;
 }return 0;
}
static int form_has(const char *body,const char *name){
 size_t n=strlen(name);for(const char *p=body;p&&*p;p=strchr(p,'&')){if(*p=='&')p++;if(!strncmp(p,name,n)&&p[n]=='=')return 1;}return 0;
}
/* Management templates are stored only. Sending is not implemented in this release. */
static int form_value_multiline(const char *body,const char *name,char *dest,size_t cap){
 size_t keylen=strlen(name);
 for(const char *p=body;*p;){
  const char *eq=strchr(p,'=');if(!eq)return 0;
  const char *amp=strchr(eq+1,'&');if(!amp)amp=p+strlen(p);
  if((size_t)(eq-p)==keylen&&!strncmp(p,name,keylen)){
   size_t n=0;
   for(const char *v=eq+1;v<amp;v++){
    unsigned char ch=(unsigned char)*v;
    if(ch=='%'&&v+2<amp&&isxdigit((unsigned char)v[1])&&isxdigit((unsigned char)v[2])){
     char h[3]={v[1],v[2],0};ch=(unsigned char)strtoul(h,NULL,16);v+=2;
    }else if(ch=='+')ch=' ';
    if(ch==0||ch==127||(ch<32&&ch!='\n'&&ch!='\r'&&ch!='\t')||n+1>=cap)return 0;
    if(ch=='\r')continue;
    dest[n++]=(char)ch;
   }dest[n]=0;return 1;
  }p=*amp?amp+1:amp;
 }return 0;
}
static void str_json(sqlite3_str *o,const char *s){
 sqlite3_str_appendchar(o,1,'"');
 for(const unsigned char*p=(const unsigned char*)(s?s:"");*p;p++){
  if(*p=='"')sqlite3_str_appendall(o,"\\\"");else if(*p=='\\')sqlite3_str_appendall(o,"\\\\");
  else if(*p=='\n')sqlite3_str_appendall(o,"\\n");else if(*p=='\r')sqlite3_str_appendall(o,"\\r");else if(*p=='\t')sqlite3_str_appendall(o,"\\t");
  else if(*p<32||*p==127)sqlite3_str_appendf(o,"\\u%04x",*p);
  else sqlite3_str_appendchar(o,1,(char)*p);
 }
 sqlite3_str_appendchar(o,1,'"');
}
/* Finish a dynamically built body and send it; 500 on allocation failure. */
static void send_str(int fd,const char *type,sqlite3_str *o){
 int err=sqlite3_str_errcode(o);char *body=sqlite3_str_finish(o);
 /* sqlite3_str_finish returns NULL for an empty string, which is a valid empty body. */
 if(err!=SQLITE_OK)send_response(fd,500,"application/json","{\"error\":\"out of memory\"}");
 else send_response(fd,200,type,body?body:"");
 sqlite3_free(body);
}

/* SMTP configuration is server-side only. Manager GET never returns stored password. */

/* NTP requests are applied by a root-owned systemd helper, never by this server. */
static int ntp_host_valid(const char *s){
 size_t n=strlen(s);if(n<1||n>200||s[0]=='.'||s[n-1]=='.'||s[0]=='-'||s[n-1]=='-')return 0;
 for(size_t i=0;i<n;i++)if(!(isalnum((unsigned char)s[i])||s[i]=='.'||s[i]=='-'||s[i]==':'))return 0;
 return 1;
}
static void ntp_get(int fd){
 char host[220]="",status[250]="";FILE*f=fopen("/var/lib/timeclock/ntp-server.txt","r");
 if(f){if(fgets(host,sizeof host,f))host[strcspn(host,"\r\n")]=0;fclose(f);}
 f=fopen("/var/lib/timeclock/ntp-status.txt","r");if(f){if(fgets(status,sizeof status,f))status[strcspn(status,"\r\n")]=0;fclose(f);}
 /* Both files are written outside this process; str_json escapes whatever they contain. */
 sqlite3_str *j=sqlite3_str_new(db);sqlite3_str_appendall(j,"{\"server\":");str_json(j,host);
 sqlite3_str_appendall(j,",\"status\":");str_json(j,status);
 sqlite3_str_appendf(j,",\"synchronized\":%s}",clock_synced()?"true":"false");send_str(fd,"application/json",j);
}
static void ntp_save(int fd,const char *body){
 char host[220];if(!form_value(body,"server",host,sizeof host)||!ntp_host_valid(host)){
  send_response(fd,400,"application/json","{\"error\":\"Enter one valid NTP hostname or IP address\"}");return;
 }
 const char *tmp="/var/lib/timeclock/ntp-server.tmp",*dst="/var/lib/timeclock/ntp.request";
 int f=open(tmp,O_CREAT|O_TRUNC|O_WRONLY|O_NOFOLLOW,0600);
 if(f<0){send_response(fd,500,"application/json","{\"error\":\"Could not apply the NTP server. Try again.\"}");return;}
 int ok=write(f,host,strlen(host))==(ssize_t)strlen(host)&&write(f,"\n",1)==1&&fsync(f)==0;
 if(close(f)!=0)ok=0;
 if(ok)ok=rename(tmp,dst)==0;else unlink(tmp);
 if(!ok){send_response(fd,500,"application/json","{\"error\":\"Could not apply the NTP server. Try again.\"}");return;}
 send_response(fd,202,"application/json","{\"status\":\"Requested. Applying now.\"}");
}
/* Timezone is separate from NTP. The privileged helper validates and applies it. */
static int timezone_valid(const char *s){
 size_t n=strlen(s);if(n<3||n>95||s[0]=='/'||strstr(s,"..")||!strchr(s,'/'))return 0;
 for(size_t i=0;i<n;i++)if(!(isalnum((unsigned char)s[i])||s[i]=='/'||s[i]=='_'||s[i]=='-'||s[i]=='+'))return 0;
 char path[160];snprintf(path,sizeof path,"/usr/share/zoneinfo/%s",s);
 struct stat st;return lstat(path,&st)==0&&(S_ISREG(st.st_mode)||S_ISLNK(st.st_mode));
}
static void timezone_get(int fd){
 char tz[110]="",status[250]="";
 char link[512];ssize_t n=readlink("/etc/localtime",link,sizeof link-1);
 if(n>0){link[n]=0;const char *prefix="/usr/share/zoneinfo/";char *z=strstr(link,prefix);
  if(z&&z[0]=='/'&&z[strlen(prefix)])snprintf(tz,sizeof tz,"%s",z+strlen(prefix));}
 FILE*f=fopen("/var/lib/timeclock/timezone-status.txt","r");if(f){if(fgets(status,sizeof status,f))status[strcspn(status,"\r\n")]=0;fclose(f);}
 sqlite3_str*j=sqlite3_str_new(db);sqlite3_str_appendall(j,"{\"timezone\":");str_json(j,tz);
 sqlite3_str_appendall(j,",\"status\":");str_json(j,status);sqlite3_str_appendall(j,"}");send_str(fd,"application/json",j);
}
static void ntp_page(int fd){
 send_response(fd,200,"text/html; charset=utf-8",UI_HEAD("Time sync","ntp")
 "<h1>Time sync</h1><p id='syncbanner' role='alert' hidden></p><section><p>The clock refuses punches until its time is synchronized.</p>"
 "<form id='ntp'><label>NTP server hostname or IP<input id='server' maxlength='200' placeholder='time.example.com' required></label><div class='actions'><button>Apply NTP server</button></div>"
 "<p id='state' role='status'>Loading...</p></form><hr><h2>Local timezone</h2><p class='muted'>Used for shift times and daylight saving. Requires a manager card. The clock restarts briefly after approval.</p><form id='tzform'><label>Timezone<select id='timezone' required><option value='America/Edmonton'>Calgary / Edmonton (Mountain Time)</option><option value='America/Vancouver'>Vancouver (Pacific Time)</option><option value='America/Regina'>Regina (Central Time, no daylight saving)</option><option value='America/Winnipeg'>Winnipeg (Central Time)</option><option value='America/Toronto'>Toronto / Ottawa (Eastern Time)</option><option value='America/Halifax'>Halifax (Atlantic Time)</option><option value='America/St_Johns'>St. John’s (Newfoundland Time)</option><option value='America/Whitehorse'>Whitehorse (Yukon Time)</option><option value='America/New_York'>New York (Eastern Time)</option><option value='America/Chicago'>Chicago (Central Time)</option><option value='America/Denver'>Denver (Mountain Time)</option><option value='America/Phoenix'>Phoenix (Mountain Time, no daylight saving)</option><option value='America/Los_Angeles'>Los Angeles (Pacific Time)</option><option value='Europe/London'>London (UK Time)</option><option value='Etc/UTC'>UTC</option></select></label><div class='actions'><button>Apply timezone</button></div><p id='tzstate' role='status'>Loading...</p></form></section>"
 "<script>async function load(){try{let r=await tc.request('/api/v1/admin/ntp');let j=await tc.json(r);if(r.ok){if(document.activeElement!==server)server.value=j.server||'';state.textContent=j.status||'Not configured';tc.banner(syncbanner,'problem','Problem',j.synchronized?'':'Clock time is not synchronized. Punches are refused until it is.');}else tc.err(state,j.error);}catch(e){tc.err(state,e)}}"
 "ntp.onsubmit=async e=>{e.preventDefault();state.textContent='Requesting change...';try{let r=await tc.request('/api/v1/admin/ntp',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({server:server.value})});let j=await tc.json(r);if(r.ok)state.textContent=j.status;else tc.err(state,j.error||'Request failed');}catch(e){tc.err(state,e)}};async function loadTz(){try{let r=await tc.request('/api/v1/admin/timezone');let j=await tc.json(r);if(r.ok){if(document.activeElement!==timezone){let zone=j.timezone||'';if(zone&&!Array.from(timezone.options).some(o=>o.value===zone))timezone.add(new Option(zone,zone));timezone.value=zone;}tzstate.textContent=j.status||'';}else tc.err(tzstate,j.error);}catch(e){tc.err(tzstate,e)}}tzform.onsubmit=async e=>{e.preventDefault();tzstate.textContent=tc.CARD;try{let r=await tc.request('/api/v1/admin/system/arm',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({action:'timezone',timezone:timezone.value})});let j=await tc.json(r);if(!r.ok)throw Error(j.error||'Approval failed');let tok=j.token,done=false;async function poll(){if(done)return;try{let r=await tc.request('/api/v1/admin/system/status',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({token:tok})});let x=await tc.json(r);if(!r.ok)throw Error(x.error||'Approval failed');if(x.status==='waiting'){setTimeout(poll,700);return;}done=true;if(x.status==='expired')tc.err(tzstate,tc.TIMEOUT);else tc.ok(tzstate,x.status||'Requested');}catch(e){done=true;tc.err(tzstate,e)}}setTimeout(poll,700)}catch(e){tc.err(tzstate,e)}};load();loadTz();setInterval(()=>{load();loadTz()},5000)</script>"UI_FOOT);
}

static void smtp_config_get(int fd){
 sqlite3_stmt*q=NULL;sqlite3_str*j=sqlite3_str_new(db);sqlite3_str_appendall(j,"{");
 const char *a[]={"url","username","tls_mode","sender","sender_name"};
 int ok=sqlite3_prepare_v2(db,"SELECT url,username,tls_mode,sender,sender_name,length(password)>0 FROM smtp_settings WHERE id=1",-1,&q,NULL)==SQLITE_OK;
 if(ok&&sqlite3_step(q)==SQLITE_ROW){
  for(int i=0;i<5;i++){if(i)sqlite3_str_appendall(j,",");sqlite3_str_appendf(j,"\"%s\":",a[i]);str_json(j,(const char*)sqlite3_column_text(q,i));}
  sqlite3_str_appendf(j,",\"has_password\":%s",sqlite3_column_int(q,5)?"true":"false");
 }else sqlite3_str_appendall(j,"\"url\":\"\",\"username\":\"\",\"tls_mode\":\"starttls\",\"sender\":\"\",\"sender_name\":\"\",\"has_password\":false");
 sqlite3_finalize(q);
 sqlite3_str_appendall(j,"}");send_str(fd,"application/json",j);
}
static int smtp_clean(const char*s,size_t max){
 if(strlen(s)>max)return 0;
 for(;*s;s++)if((unsigned char)*s<32||(unsigned char)*s==127)return 0;
 return 1;
}
static void smtp_config_save(int fd,const char*b){
 char url[260]="",user[260]="",password[260]="",tls[20]="",sender[260]="",sender_name[260]="",clear[8]="";
 if(!form_value(b,"url",url,sizeof url)||!form_value(b,"username",user,sizeof user)||
    !form_value(b,"password",password,sizeof password)||!form_value(b,"tls_mode",tls,sizeof tls)||
    (form_has(b,"sender_name")&&!form_value(b,"sender_name",sender_name,sizeof sender_name))||!form_value(b,"sender",sender,sizeof sender)||!form_value(b,"clear_password",clear,sizeof clear)){
  send_response(fd,400,"application/json","{\"error\":\"Fill in all required fields.\"}");return;
 }
 int scheme=!strncmp(url,"smtp://",7)?1:!strncmp(url,"smtps://",8)?2:0;
 if(!smtp_clean(url,250)||!smtp_clean(user,200)||!smtp_clean(password,240)||!smtp_clean(sender,200)||!smtp_clean(sender_name,200)||
    (url[0]&&!scheme)||(sender[0]&&(!strchr(sender,'@')||strchr(sender,' ')))||
    (strcmp(tls,"none")&&strcmp(tls,"starttls")&&strcmp(tls,"implicit"))||
    (scheme==2&&strcmp(tls,"implicit"))||(scheme==1&&!strcmp(tls,"implicit"))||
    (!strcmp(tls,"none")&&user[0])||strchr(url,'@')||strchr(url,'?')||strchr(url,'#')){
   send_response(fd,400,"application/json","{\"error\":\"Check the server name, port and sender address.\"}");return;
 }
 sqlite3_stmt*q=NULL;const char*query=
 "INSERT INTO smtp_settings(id,url,username,password,tls_mode,sender,sender_name) VALUES(1,?1,?2,?3,?4,?5,?7) "
 "ON CONFLICT(id) DO UPDATE SET url=excluded.url,username=excluded.username,"
 "password=CASE WHEN ?6='1' THEN '' WHEN ?3<>'' THEN ?3 ELSE smtp_settings.password END,"
 "tls_mode=excluded.tls_mode,sender=excluded.sender,sender_name=excluded.sender_name";
 int ok=sqlite3_prepare_v2(db,query,-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,url,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,user,-1,SQLITE_TRANSIENT);
 sqlite3_bind_text(q,3,password,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,4,tls,-1,SQLITE_TRANSIENT);
 sqlite3_bind_text(q,5,sender,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,6,clear,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,7,sender_name,-1,SQLITE_TRANSIENT);
 ok=sqlite3_step(q)==SQLITE_DONE;}
 sqlite3_finalize(q);send_response(fd,ok?200:500,"application/json",ok?"{\"status\":\"saved\"}":"{\"error\":\"save failed\"}");
}
static void smtp_test_enqueue(int fd,const char *body){
 char recipient[260]="";
 if(!form_value(body,"recipient",recipient,sizeof recipient)||!smtp_clean(recipient,250)||!strchr(recipient,'@')||strchr(recipient,' ')){
  send_response(fd,400,"application/json","{\"error\":\"Enter a valid email address.\"}");return;
 }
 sqlite3_stmt*q=NULL;
 int ok=sqlite3_prepare_v2(db,"INSERT INTO email_queue(recipient,subject,body,status,next_attempt) VALUES(?1,'Timeclock: Test email','Test message from mk-timeclock. Outgoing email works.','pending',strftime('%Y-%m-%dT%H:%M:%fZ','now'))",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,recipient,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}
 sqlite3_finalize(q);send_response(fd,ok?200:500,"application/json",ok?"{\"status\":\"queued\"}":"{\"error\":\"queue failed\"}");
}
static void smtp_config_page(int fd){
 send_response(fd,200,"text/html; charset=utf-8",
 UI_HEAD("Outgoing email","smtp")"<h1>Outgoing email</h1><p>Mail server for notices, timesheet links, payroll reports and backups.</p><p id='smtpbanner' role='alert' hidden></p><section><form id='settings'>"
 "<h2>Outgoing server</h2>"
 "<label>SMTP server<input id='host' placeholder='smtp.example.com' required></label>"
 "<label>Port<input id='port' type='number' min='1' max='65535' value='587' required></label>"
 "<label>Connection security<select id='tls_mode'><option value='starttls'>STARTTLS (port 587)</option><option value='implicit'>SSL/TLS (port 465)</option><option value='none'>None (trusted network or VPN)</option></select></label>"
 "<h2>Account</h2><label>Username (blank if the server needs no sign-in)<input id='username' autocomplete='username'></label>"
 "<label>Password (blank keeps saved password)<input id='password' type='password' autocomplete='new-password'></label>"
 "<p class='muted' id='stored'></p><label><input id='clear_password' type='checkbox'> Remove saved password</label>"
 "<h2>Sender</h2><label>Sender name (optional)<input id='sender_name' maxlength='200' placeholder='iCube Development Timeclock'></label><label>From email address<input id='sender' type='email' autocomplete='email' placeholder='timeclock@example.com' required></label>"
 "<div class='actions'><button>Save email settings</button></div><p id='status' role='status'></p></form></section>"
 "<section><h2>Send a test email</h2><p class='muted'>Check <a href='/email-queue'>Email queue</a> for the result.</p>"
 "<form id='test'><label>Send to<input id='recipient' type='email' required></label><div class='actions'><button>Send test email</button></div><p id='result' role='status'></p></form></section>"
 "<script>const api=tc.api;"
 "const byId=id=>document.getElementById(id);let loading=false;"
 "function defaultPort(mode){return mode==='implicit'?'465':mode==='none'?'25':'587'}"
 "function newUrl(){const host=byId('host').value.trim(),port=Number(byId('port').value),mode=byId('tls_mode').value;"
 "if(!host||/[\\s\\/?#@]/.test(host)||host.includes(':')||!Number.isInteger(port)||port<1||port>65535)throw Error('Enter a valid server name or IPv4 address, and a port from 1 to 65535.');"
 "return (mode==='implicit'?'smtps://':'smtp://')+host+':'+port;}"
 "async function load(){let d=await api('/api/v1/admin/smtp');byId('tls_mode').value=d.tls_mode||'starttls';"
 "let host='',port=defaultPort(byId('tls_mode').value);if(d.url){try{let u=new URL(d.url);host=u.hostname;port=u.port||((u.protocol==='smtps:')?'465':'25');}catch(e){tc.err(byId('status'),'The saved server address is not valid. Enter it again.');}}"
 "byId('host').value=host;byId('port').value=port;byId('sender').value=d.sender||'';byId('sender_name').value=d.sender_name||'';byId('username').value=d.username||'';"
 "byId('stored').textContent=d.has_password?'Password saved. Leave blank to keep it.':'No password saved.';"
 "tc.banner(byId('smtpbanner'),'problem','Problem',d.url&&d.sender?'':'No mail server set. No email is sent.');}"
 "byId('tls_mode').onchange=()=>{const p=byId('port');if(['25','465','587'].includes(p.value))p.value=defaultPort(byId('tls_mode').value);};"
 "byId('settings').onsubmit=async e=>{e.preventDefault();const status=byId('status');try{let p=new URLSearchParams();"
 "p.set('url',newUrl());for(let f of ['tls_mode','sender','sender_name','username','password'])p.set(f,byId(f).value);p.set('clear_password',byId('clear_password').checked?'1':'0');"
 "status.textContent='Saving...';await api('/api/v1/admin/smtp',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p});"
 "byId('password').value='';byId('clear_password').checked=false;tc.ok(status,'Saved. New messages use these settings.');await load();}catch(e){tc.err(status,e);}};"
 "byId('test').onsubmit=async e=>{e.preventDefault();try{await api('/api/v1/admin/smtp-test',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({recipient:byId('recipient').value})});"
 "tc.ok(byId('result'),'Test queued. Check Email queue for the result.');}catch(e){tc.err(byId('result'),e);}};"
 "load().catch(e=>tc.err(byId('status'),e));</script>"UI_FOOT);
}

static int tls_stage_write(const char *name,const char *data){
 char path[180];snprintf(path,sizeof path,"/var/lib/timeclock/tls-stage/%s",name);
 int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0600);
 if(fd<0)return 0;
 size_t len=strlen(data);ssize_t n=write(fd,data,len);int ok=n==(ssize_t)len&&fsync(fd)==0;close(fd);return ok;
}
static void tls_upload(int fd,const char *body){
 char cert[9000],chain[9000],key[9000],hostname[260];
 if(!form_value_multiline(body,"cert",cert,sizeof cert)||!form_value_multiline(body,"chain",chain,sizeof chain)||!form_value_multiline(body,"key",key,sizeof key)||!form_value(body,"hostname",hostname,sizeof hostname)){
  send_response(fd,400,"application/json","{\"error\":\"Choose the certificate and key files (each under 9 KB).\"}");return;
 }
 if(strncmp(cert,"-----BEGIN CERTIFICATE-----",27)||strncmp(key,"-----BEGIN ",11)||(!strstr(key,"PRIVATE KEY-----"))||!valid_id(hostname,250)||strchr(hostname,':')){
  send_response(fd,400,"application/json","{\"error\":\"The certificate, key or hostname is not valid.\"}");return;
 }
 if(chain[0]&&strncmp(chain,"-----BEGIN CERTIFICATE-----",27)){
  send_response(fd,400,"application/json","{\"error\":\"The intermediate chain file is not valid.\"}");return;
 }
 if(mkdir("/var/lib/timeclock/tls-stage",0700)&&errno!=EEXIST){send_response(fd,500,"application/json","{\"error\":\"Could not start the check. Try again.\"}");return;}
 int ok=tls_stage_write("leaf.pem",cert)&&tls_stage_write("chain.pem",chain)&&tls_stage_write("key.pem",key)&&tls_stage_write("hostname",hostname);
 memset(key,0,sizeof key);
 if(ok)ok=tls_stage_write("request","apply\n");
 send_response(fd,ok?202:500,"application/json",ok?"{\"status\":\"staged\",\"message\":\"Checking certificate\"}":"{\"error\":\"Could not start the check. Try again.\"}");
}
static void tls_status(int fd){
 FILE*f=fopen("/var/lib/timeclock/tls-stage/status","r");char status[360]="Not configured";
 if(f){if(!fgets(status,sizeof status,f))status[0]=0;fclose(f);}
 size_t n=strlen(status);while(n&&(status[n-1]=='\n'||status[n-1]=='\r'))status[--n]=0;
 sqlite3_str*o=sqlite3_str_new(db);sqlite3_str_appendall(o,"{\"enabled\":");sqlite3_str_appendall(o,access("/etc/timeclock/tls/active",F_OK)==0?"true":"false");sqlite3_str_appendall(o,",\"status\":");str_json(o,status);sqlite3_str_appendf(o,",\"expires\":%lld,\"temporary\":%s}",tls_not_after(),tls_temporary()?"true":"false");send_str(fd,"application/json",o);
}
static void tls_page(int fd){send_response(fd,200,"text/html; charset=utf-8",
 UI_HEAD("HTTPS certificate","tls")"<h1>HTTPS certificate</h1><p>The clock checks that the key matches, the hostname is covered and the chain is trusted before it switches over. A failed check keeps the current certificate.</p><p id='tlsbanner' role='alert' hidden></p>"
 "<section><h2>Current certificate</h2><p id='tlsstate' role='status'>Loading...</p></section>"
 "<section><h2>Replace certificate</h2><form id='tlsform'><label>Certificate hostname<input id='hostname' required placeholder='clock.example.com'></label><label>Certificate (.pem)<input type='file' id='cert' accept='.pem,.crt,.cer' required></label><label>Intermediate chain (.pem, leave empty if signed directly by a root)<input type='file' id='chain' accept='.pem,.crt,.cer'></label><label>Private key (.pem)<input type='file' id='key' accept='.pem,.key' required></label><div class='actions'><button>Check and install</button></div><p id='tlsresult' role='status'></p></form></section>"
 "<section><h2>Employee email links</h2><p id='linkeffective' role='status'></p></section>"
 "<script>const out=document.getElementById('tlsresult'),state=document.getElementById('tlsstate'),banner=document.getElementById('tlsbanner');let poll=null;"
 "async function refresh(){try{let r=await tc.request('/api/v1/admin/tls/status',{cache:'no-store'});let j=await tc.json(r);if(!r.ok)throw Error(j.error||'Status unavailable');"
 "state.replaceChildren();let days=j.expires?Math.floor((j.expires*1000-Date.now())/86400000):null;"
 "if(!j.enabled){tc.banner(banner,'problem','Problem','HTTPS is not active. Install a certificate below.');}"
 "else if(j.temporary){tc.banner(banner,'problem','Problem','Temporary self-signed certificate. Browsers show a warning and employee links may not open. Install a trusted certificate below.');}"
 "else if(days!==null&&days<7){tc.banner(banner,'problem','Problem','Certificate expires in '+Math.max(days,0)+(days===1?' day':' days')+'. Replace it now.');}"
 "else if(days!==null&&days<30){tc.banner(banner,'check','Check','Certificate expires in '+days+' days.');}else tc.banner(banner,'','','');"
 "if(j.enabled)state.append(tc.flag('Active','ok'),' ');else state.append('None installed.');if(j.expires)state.append('Valid until '+new Date(j.expires*1000).toISOString().slice(0,10)+'. ');"
 "let last=String(j.status||'');if(/^Failed/.test(last))tc.err(out,'Last upload: '+last);else if(/^Validating/.test(last))out.textContent=last+'...';"
 "clearTimeout(poll);if(/^Validating/.test(last))poll=setTimeout(refresh,3000);}catch(e){tc.err(state,e)}}"
 "document.getElementById('tlsform').onsubmit=async e=>{e.preventDefault();let p=new URLSearchParams({hostname:document.getElementById('hostname').value});for(let id of ['cert','chain','key']){let file=document.getElementById(id).files[0];p.set(id,file?await file.text():'')}if(p.toString().length>24000){tc.err(out,'The files are larger than 24 KB. Check that each file holds only one certificate or key.');return}if(!confirm('Replace the HTTPS certificate?'))return;try{let r=await tc.request('/api/v1/admin/tls/upload',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p});let j=await tc.json(r);if(!r.ok)throw Error(j.error||'Upload failed');out.textContent='Checking certificate...';setTimeout(refresh,2000)}catch(e){tc.err(out,e)}};refresh();"
 "async function loadLink(){let field=document.getElementById('linkeffective');try{let r=await tc.request('/api/v1/admin/note-link',{cache:'no-store'});let j=await tc.json(r);if(!r.ok)throw Error(j.error||'Unavailable');field.replaceChildren();"
 "if(!j.effective){field.append(tc.flag('Problem','problem'),' No hostname. Employee links cannot be sent.');return}"
 "field.append('Late-arrival and timesheet links use '+j.effective+' ');if(j.source!=='certificate')field.append(tc.flag('Check','check'),' Not the certificate hostname. Employees may see a certificate warning.')}catch(e){tc.err(field,e)}}loadLink();</script>"UI_FOOT);
}
static void email_retry(int fd,int who,const char *body){
 char id[30]="";if(!form_value(body,"id",id,sizeof id)){send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
 for(const char *c=id;*c;c++)if(*c<'0'||*c>'9'){send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
 if(!sql("BEGIN IMMEDIATE")){send_response(fd,503,"application/json","{\"error\":\"The clock is busy. Try again.\"}");return;}
 sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(db,"UPDATE email_queue SET status='pending',next_attempt=strftime('%Y-%m-%dT%H:%M:%fZ','now'),attempts=0,last_error='' WHERE id=?1 AND status='failed'",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int64(q,1,atoll(id));ok=sqlite3_step(q)==SQLITE_DONE&&sqlite3_changes(db)==1;}
 sqlite3_finalize(q);q=NULL;
 if(ok&&sqlite3_prepare_v2(db,"INSERT INTO email_retry_audit(email_id,manager_id) VALUES(?1,?2)",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int64(q,1,atoll(id));sqlite3_bind_int(q,2,who);ok=sqlite3_step(q)==SQLITE_DONE;}else ok=0;sqlite3_finalize(q);
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");
 send_response(fd,ok?200:409,"application/json",ok?"{\"status\":\"queued\"}":"{\"error\":\"Could not retry. Reload the page; the email may already be queued.\"}");
}
/* Clock acknowledgements are durable: the agent deletes its local copy on HTTP 200.
   synchronous is connection-local and must be changed outside a transaction. */
static int clock_durability(int full){
 return sql(full ? "PRAGMA synchronous=FULL" : "PRAGMA synchronous=NORMAL");
}
static void rejected_upload_receive(int fd,const char *body){
 char event[45]="",clock[55]="",uid[32]="",swiped[48]="",rejected[48]="",status[8]="";
 if(!form_value(body,"event_id",event,sizeof event)||!form_value(body,"clock_id",clock,sizeof clock)||!form_value(body,"card_uid",uid,sizeof uid)||!form_value(body,"swiped_at",swiped,sizeof swiped)||!form_value(body,"rejected_at",rejected,sizeof rejected)||!form_value(body,"http_status",status,sizeof status)||strlen(event)!=32||!valid_id(clock,48)||!valid_id(uid,24)||strlen(swiped)<19||strlen(rejected)<19){send_response(fd,400,"application/json","{\"error\":\"invalid rejected event\"}");return;}
 for(int i=0;i<32;i++)if(!((event[i]>='a'&&event[i]<='f')||(event[i]>='0'&&event[i]<='9'))){send_response(fd,400,"application/json","{\"error\":\"invalid event id\"}");return;}
 for(const char*c=status;*c;c++)if(*c<'0'||*c>'9'){send_response(fd,400,"application/json","{\"error\":\"invalid status\"}");return;}
 int code=atoi(status);if(code!=400&&code!=409&&code!=422){send_response(fd,400,"application/json","{\"error\":\"invalid status\"}");return;}
 if(!clock_durability(1)){send_response(fd,503,"application/json","{\"error\":\"durability unavailable\"}");return;}
 sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(db,"INSERT INTO rejected_uploads(event_id,clock_id,card_uid,swiped_at,http_status,reason,rejected_at) VALUES(?1,?2,?3,?4,?5,'server rejected event',?6) ON CONFLICT(event_id) DO NOTHING",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,event,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,clock,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,uid,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,4,swiped,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,5,code);sqlite3_bind_text(q,6,rejected,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;} sqlite3_finalize(q);
 if(!clock_durability(0))fprintf(stderr,"cannot restore normal synchronous mode after rejected event\n");
 send_response(fd,ok?200:500,"application/json",ok?"{\"status\":\"recorded\"}":"{\"error\":\"The clock could not complete this. Try again.\"}");
}
static void rejected_upload_list(int fd){
 sqlite3_stmt*q=NULL;if(sqlite3_prepare_v2(db,"SELECT event_id,clock_id,card_uid,swiped_at,http_status,reason,rejected_at FROM rejected_uploads ORDER BY received_at DESC LIMIT 200",-1,&q,NULL)!=SQLITE_OK){send_response(fd,500,"application/json","{\"error\":\"The clock could not complete this. Try again.\"}");return;}
 sqlite3_str*o=sqlite3_str_new(db);sqlite3_str_appendall(o,"{\"rejected_uploads\":[");int count=0;
 while(sqlite3_step(q)==SQLITE_ROW){if(count++)sqlite3_str_appendchar(o,1,',');sqlite3_str_appendall(o,"{");const char*names[]={"event_id","clock_id","card_uid","swiped_at","reason","rejected_at"};int cols[]={0,1,2,3,5,6};for(int i=0;i<6;i++){if(i)sqlite3_str_appendchar(o,1,',');str_json(o,names[i]);sqlite3_str_appendchar(o,1,':');str_json(o,(const char*)sqlite3_column_text(q,cols[i]));}sqlite3_str_appendf(o,",\"http_status\":%d}",sqlite3_column_int(q,4));}
 sqlite3_finalize(q);sqlite3_str_appendall(o,"]}");send_str(fd,"application/json",o);
}
static void template_list(int fd){
 sqlite3_stmt*q=NULL;
 if(sqlite3_prepare_v2(db,"SELECT id,subject,body,enabled FROM email_templates ORDER BY id",-1,&q,NULL)!=SQLITE_OK){send_response(fd,500,"application/json","{\"error\":\"The clock could not complete this. Try again.\"}");return;}
 sqlite3_str *o=sqlite3_str_new(db);sqlite3_str_appendall(o,"{\"templates\":[");int first=1;
 while(sqlite3_step(q)==SQLITE_ROW){
  if(!first)sqlite3_str_appendchar(o,1,',');
  first=0;
  sqlite3_str_appendall(o,"{\"id\":");str_json(o,(const char*)sqlite3_column_text(q,0));
  sqlite3_str_appendall(o,",\"subject\":");str_json(o,(const char*)sqlite3_column_text(q,1));
  sqlite3_str_appendall(o,",\"body\":");str_json(o,(const char*)sqlite3_column_text(q,2));
  sqlite3_str_appendf(o,",\"enabled\":%d,\"implemented\":%s}",sqlite3_column_int(q,3),(!strcmp((const char*)sqlite3_column_text(q,0),"swipe_employee")||!strcmp((const char*)sqlite3_column_text(q,0),"late_arrival_employee")||!strcmp((const char*)sqlite3_column_text(q,0),"late_arrival_manager")||!strcmp((const char*)sqlite3_column_text(q,0),"attendance_report")||!strcmp((const char*)sqlite3_column_text(q,0),"missing_in_employee")||!strcmp((const char*)sqlite3_column_text(q,0),"missing_out_employee")||!strcmp((const char*)sqlite3_column_text(q,0),"welcome_employee")||!strcmp((const char*)sqlite3_column_text(q,0),"override_employee")||!strcmp((const char*)sqlite3_column_text(q,0),"auto_review_employee")||!strcmp((const char*)sqlite3_column_text(q,0),"review_dispatch_payroll")||!strcmp((const char*)sqlite3_column_text(q,0),"timeoff_received_employee")||!strcmp((const char*)sqlite3_column_text(q,0),"timeoff_received_manager")||!strcmp((const char*)sqlite3_column_text(q,0),"timeoff_decision_employee")||!strcmp((const char*)sqlite3_column_text(q,0),"timeoff_cancelled_employee")||!strcmp((const char*)sqlite3_column_text(q,0),"review_reminder_employee")||!strcmp((const char*)sqlite3_column_text(q,0),"email_failure_payroll"))?"true":"false");
 }
 sqlite3_finalize(q);sqlite3_str_appendall(o,"]}");send_str(fd,"application/json",o);
}
static void template_default_get(int fd,const char *body){
 char id[80];
 if(!form_value(body,"id",id,sizeof id)){send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
 const email_template_default *t=find_template_default(id);
 if(!t){send_response(fd,404,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
 sqlite3_str *o=sqlite3_str_new(db);
 sqlite3_str_appendall(o,"{\"id\":");str_json(o,t->id);
 sqlite3_str_appendall(o,",\"subject\":");str_json(o,t->subject);
 sqlite3_str_appendall(o,",\"body\":");str_json(o,t->body);
 sqlite3_str_appendall(o,",\"enabled\":true}");send_str(fd,"application/json",o);
}
static int authorization_pending(void);
static int approval_token_new(char out[33]);
static int approval_token_matches(const char *body);
static void template_restore_arm(int fd,int who,const char *body){
 char id[80];if(!form_value(body,"id",id,sizeof id)||!find_template_default(id)){send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
 if(!sql("BEGIN IMMEDIATE")){send_response(fd,503,"application/json","{\"error\":\"The clock is busy. Try again.\"}");return;}
 if(authorization_pending()){sql("ROLLBACK");send_response(fd,409,"application/json","{\"error\":\"The card reader is waiting for another approval. Try again in 30 seconds.\"}");return;}
 sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(db,"INSERT INTO manager_authorization(id,requested_by,change_body,armed_at) VALUES(1,?1,?2,CAST(strftime('%s','now') AS INTEGER)) ON CONFLICT(id) DO UPDATE SET requested_by=excluded.requested_by,change_body=excluded.change_body,armed_at=excluded.armed_at",-1,&q,NULL)==SQLITE_OK;
 char op[120];snprintf(op,sizeof op,"TEMPLATE_RESTORE:%s",id);
 if(ok){sqlite3_bind_int(q,1,who);sqlite3_bind_text(q,2,op,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 char token[33]="";if(ok)ok=sql("UPDATE manager_scan SET armed_at=CAST(strftime('%s','now') AS INTEGER),uid='',scanned_at=0,scan_mode='approval' WHERE id=1");
 if(ok)ok=approval_token_new(token);
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");
 char response[140];snprintf(response,sizeof response,"{\"status\":\"waiting\",\"token\":\"%s\"}",token);send_response(fd,ok?200:500,"application/json",ok?response:"{\"error\":\"The clock could not complete this. Try again.\"}");
}
static void template_restore_status(int fd,int who){
 sqlite3_stmt*q=NULL;char op[130]="",uid[32]="";sqlite3_int64 armed=0,scanned=0;int owner=0;
 if(sqlite3_prepare_v2(db,"SELECT a.requested_by,a.change_body,a.armed_at,s.uid,s.scanned_at FROM manager_authorization a JOIN manager_scan s ON s.id=1 WHERE a.id=1",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW){
 owner=sqlite3_column_int(q,0);const char*v=(const char*)sqlite3_column_text(q,1);if(v)snprintf(op,sizeof op,"%s",v);armed=sqlite3_column_int64(q,2);const char*u=(const char*)sqlite3_column_text(q,3);if(u)snprintf(uid,sizeof uid,"%s",u);scanned=sqlite3_column_int64(q,4);
 }sqlite3_finalize(q);
 if(owner!=who||strncmp(op,"TEMPLATE_RESTORE:",17)||!armed){send_response(fd,409,"application/json","{\"error\":\"This approval is no longer valid. Start again.\"}");return;}
 if((sqlite3_int64)time(NULL)-armed>30){sql("DELETE FROM manager_authorization WHERE id=1");send_response(fd,200,"application/json","{\"status\":\"expired\"}");return;}
 if(!uid[0]||scanned<armed){send_response(fd,200,"application/json","{\"status\":\"waiting\"}");return;}
 int authorized=0;
 if(sqlite3_prepare_v2(db,"SELECT 1 FROM manager_cards mc JOIN employees e ON e.id=mc.manager_id JOIN user_accounts ua ON ua.employee_id=e.id JOIN employee_roles er ON er.employee_id=e.id AND er.role='manager' WHERE mc.uid=?1 COLLATE NOCASE AND mc.active=1 AND e.active=1 AND ua.disabled=0",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,uid,-1,SQLITE_TRANSIENT);authorized=sqlite3_step(q)==SQLITE_ROW;}sqlite3_finalize(q);
 if(!authorized){sql("UPDATE manager_scan SET uid='',scanned_at=0 WHERE id=1");send_response(fd,200,"application/json","{\"status\":\"waiting\"}");return;}
 const email_template_default*t=find_template_default(op+17);int ok=t!=NULL;
 if(!sql("BEGIN IMMEDIATE")){send_response(fd,503,"application/json","{\"error\":\"The clock is busy. Try again.\"}");return;}
 if(ok&&sqlite3_prepare_v2(db,"UPDATE email_templates SET subject=?1,body=?2 WHERE id=?3",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,t->subject,-1,SQLITE_STATIC);sqlite3_bind_text(q,2,t->body,-1,SQLITE_STATIC);sqlite3_bind_text(q,3,t->id,-1,SQLITE_STATIC);ok=sqlite3_step(q)==SQLITE_DONE&&sqlite3_changes(db)==1;}else ok=0;sqlite3_finalize(q);q=NULL;
 if(ok&&sqlite3_prepare_v2(db,"INSERT INTO template_restore_audit(template_id,manager_id,approved_uid) VALUES(?1,?2,?3)",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,t->id,-1,SQLITE_STATIC);sqlite3_bind_int(q,2,who);sqlite3_bind_text(q,3,uid,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}else ok=0;sqlite3_finalize(q);
 if(ok)ok=sql("DELETE FROM manager_authorization WHERE id=1")&&sql("UPDATE manager_scan SET armed_at=0,uid='',scanned_at=0,request_token='' WHERE id=1");
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");send_response(fd,ok?200:500,"application/json",ok?"{\"status\":\"restored\"}":"{\"error\":\"Default not restored. Try again.\"}");
}
static void template_save(int fd,const char *body){
 char id[80],subject[260],content[2400],enabled[4];
 if(!form_value(body,"id",id,sizeof id)||!form_value(body,"subject",subject,sizeof subject)||!form_value_multiline(body,"body",content,sizeof content)||!form_value(body,"enabled",enabled,sizeof enabled)||
    !valid_id(id,64)||!subject[0]||!content[0]||(strcmp(enabled,"0")&&strcmp(enabled,"1"))){
  send_response(fd,400,"application/json","{\"error\":\"Template not saved. Try again.\"}");return;
 }
 for(const unsigned char*p=(const unsigned char*)subject;*p;p++)if(*p<32||*p==127){send_response(fd,400,"application/json","{\"error\":\"Enter a subject of up to 240 characters.\"}");return;}
 sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(db,"UPDATE email_templates SET subject=?1,body=?2,enabled=?3 WHERE id=?4 AND id IN ('swipe_employee','late_arrival_employee','late_arrival_manager','attendance_report','missing_in_employee','missing_out_employee','welcome_employee','auto_review_employee','review_dispatch_payroll','timeoff_received_employee','timeoff_received_manager','timeoff_decision_employee','timeoff_cancelled_employee','review_reminder_employee','email_failure_payroll')",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,subject,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,content,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,3,enabled[0]=='1');sqlite3_bind_text(q,4,id,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE&&sqlite3_changes(db)==1;}
 sqlite3_finalize(q);send_response(fd,ok?200:400,"application/json",ok?"{\"status\":\"saved\"}":"{\"error\":\"Template not saved. Try again.\"}");
}
static void template_page(int fd){
 static const char html[]=
 UI_HEAD("Email templates","templates")"<h1>Email templates</h1><p>No IN and No OUT notices are sent after the scheduled start or finish plus the late grace.</p><section><label>Template<select id='picker'></select></label><form id='editor'><label>Subject<input id='subject' maxlength='240' required></label><label>Message body<textarea id='body' maxlength='2300' required></textarea></label><label><input type='checkbox' id='enabled'> Send this email</label><div class='actions'><button type='submit'>Save template</button><button type='button' id='previewDefault'>Show default</button><button type='button' id='restoreDefault'>Restore default</button></div><p id='status' role='status'></p></form></section>"
 "<section id='defaultPreview' hidden><h2>Default text</h2><label>Subject<input id='defaultSubject' readonly></label><label>Message body<textarea id='defaultBody' readonly></textarea></label></section><section><h2>Variables</h2><p id='variables' class='muted'></p></section>"
 "<script>let rows=[];let picker=document.getElementById('picker'),status=document.getElementById('status');"
 "const api=tc.api;"
 "const variables={swipe_employee:'{{employee_name}}, {{clock_name}}, {{clock_id}}, {{swipe_time}}, {{swipe_date}}',late_arrival_employee:'{{employee_name}}, {{scheduled_start}}, {{actual_start}}, {{late_minutes}}, {{attendance_date}}, {{note_link}} (the employee adds a reason here)',late_arrival_manager:'{{employee_name}}, {{scheduled_start}}, {{actual_start}}, {{late_minutes}}, {{attendance_date}}',missing_in_employee:'{{employee_name}}, {{scheduled_start}}, {{attendance_date}}',missing_out_employee:'{{employee_name}}, {{scheduled_end}}, {{attendance_date}}',attendance_report:'{{payday}}, {{period_start}}, {{period_end}}, {{attendance_summary}}',welcome_employee:'{{employee_name}}',override_employee:'{{period_start}}, {{period_end}}, {{hours}}, {{manager_name}}, {{reason}}',auto_review_employee:'{{employee_name}}, {{period_start}}, {{period_end}}, {{review_link}}, {{review_deadline}}, {{missing_punch_note}}',review_dispatch_payroll:'{{period_start}}, {{period_end}}, {{sent_count}}',timeoff_received_employee:'{{employee_name}}, {{request_type}}, {{period_start}}, {{period_end}}, {{days}}, {{days_label}}, {{return_date}}',timeoff_received_manager:'{{employee_name}}, {{request_type}}, {{period_start}}, {{period_end}}, {{days}}, {{return_date}}, {{note}}',timeoff_decision_employee:'{{employee_name}}, {{period_start}}, {{period_end}}, {{decision}}, {{manager_name}}, {{note}}',timeoff_cancelled_employee:'{{employee_name}}, {{period_start}}, {{period_end}}, {{manager_name}}, {{note}}',review_reminder_employee:'{{employee_name}}, {{period_start}}, {{period_end}}, {{review_link}}',email_failure_payroll:'{{recipient}}, {{failed_subject}}'};function show(){let t=rows.find(x=>x.id===picker.value);if(!t)return;document.getElementById('variables').textContent=variables[t.id]||'';subject.value=t.subject;document.getElementById('body').value=t.body;enabled.checked=!!t.enabled;enabled.disabled=!t.implemented;document.querySelector('#editor button[type=submit]').disabled=!t.implemented;document.getElementById('status').textContent='';}"
 "const labels={swipe_employee:'Swipe receipt (employee)',late_arrival_employee:'Late arrival (employee)',late_arrival_manager:'Late arrival (manager)',missing_in_employee:'No IN punch (employee)',missing_out_employee:'No OUT punch (employee)',attendance_report:'Payroll report',welcome_employee:'Employee welcome',timeoff_received_employee:'Time off request received (employee)',timeoff_received_manager:'Time off request received (manager)',timeoff_decision_employee:'Time off approved or denied',timeoff_cancelled_employee:'Time off approval cancelled',review_reminder_employee:'Timesheet reminder',auto_review_employee:'Employee timesheet link',review_dispatch_payroll:'Timesheet links queued (payroll)',email_failure_payroll:'Email delivery failure (payroll)'};"
 "async function load(keep){try{let j=await api('/api/v1/admin/templates');rows=j.templates;picker.replaceChildren();for(let t of rows.filter(x=>x.implemented&&x.id!=='override_employee')){let o=document.createElement('option');o.value=t.id;o.textContent=(labels[t.id]||t.id.replaceAll('_',' '))+(!t.enabled?' (off)':'');picker.append(o);}if(keep)picker.value=keep;show();}catch(e){tc.err(document.getElementById('status'),e);}}"
 "picker.onchange=()=>{document.getElementById('defaultPreview').hidden=true;show();};"
 "document.getElementById('previewDefault').onclick=async()=>{let id=picker.value;try{let j=await api('/api/v1/admin/templates/default?id='+encodeURIComponent(id));if(picker.value!==id)return;document.getElementById('defaultSubject').value=j.subject;document.getElementById('defaultBody').value=j.body;document.getElementById('defaultPreview').hidden=false;status.textContent='Default shown below. Your template is unchanged.';}catch(e){tc.err(status,e);}};"
 "document.getElementById('restoreDefault').onclick=async()=>{let id=picker.value;if(!confirm('Restore the default subject and body? Your changes to this template will be lost.'))return;try{let armed=await api('/api/v1/admin/templates/restore/arm',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({id}).toString()});status.textContent=tc.CARD;let result;for(let i=0;i<45;i++){await new Promise(r=>setTimeout(r,700));result=await api('/api/v1/admin/templates/restore/status',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({token:armed.token}).toString()});if(result.status==='restored')break;if(result.status==='expired')throw Error(tc.TIMEOUT);}if(!result||result.status!=='restored')throw Error(tc.TIMEOUT);await load(id);document.getElementById('defaultPreview').hidden=true;tc.ok(status,'Default restored.');}catch(e){tc.err(status,e);}};"
 "editor.onsubmit=async e=>{e.preventDefault();let id=picker.value,p=new URLSearchParams({id,subject:subject.value,body:document.getElementById('body').value,enabled:enabled.checked?'1':'0'});try{await api('/api/v1/admin/templates',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()});await load(id);tc.ok(document.getElementById('status'),'Template saved.');}catch(e){tc.err(document.getElementById('status'),e);}};load();</script>"UI_FOOT;
 send_response(fd,200,"text/html; charset=utf-8",html);
}
static void trim_spaces(char *s){size_t n=strlen(s),a=0;while(a<n&&s[a]==' ')a++;while(n>a&&s[n-1]==' ')n--;memmove(s,s+a,n-a);s[n-a]=0;}
/* First name, last name and position are required on every employee save. Checked when the
   change is armed (so a missing field never uses a manager card scan) and again when applied.
   Returns NULL when valid, otherwise a JSON error body. Limits are bytes (UTF-8). */
static const char *employee_identity_error(const char *body,char *first,size_t fc,char *last,size_t lc,char *position,size_t pc){
 if(!form_value(body,"first_name",first,fc)||!form_value(body,"last_name",last,lc)||!form_value(body,"position",position,pc))
  return "{\"error\":\"First name, last name and position are required. No card scan was requested.\"}";
 trim_spaces(first);trim_spaces(last);trim_spaces(position);
 if(!first[0])return "{\"error\":\"First name is required. No card scan was requested.\"}";
 if(!last[0])return "{\"error\":\"Last name is required. No card scan was requested.\"}";
 if(!position[0])return "{\"error\":\"Position is required. No card scan was requested.\"}";
 return NULL;
}
static void management_list(int fd){
 sqlite3_stmt*q=NULL;
 const char *query="SELECT e.id,e.name,coalesce(e.email,''),e.active,coalesce((SELECT group_concat(uid,',') FROM cards WHERE employee_id=e.id AND active=1),''),coalesce((SELECT group_concat(uid,',') FROM manager_cards WHERE manager_id=e.id AND active=1),''),EXISTS(SELECT 1 FROM employee_roles WHERE employee_id=e.id AND role='manager'),e.late_email_enabled,e.first_name,e.last_name,e.position,(trim(e.first_name)='' OR trim(e.last_name)='' OR trim(e.position)=''),e.phone,EXISTS(SELECT 1 FROM schedule_profiles sp WHERE sp.employee_id=e.id),EXISTS(SELECT 1 FROM schedule_profiles sp WHERE sp.employee_id=e.id AND sp.schedule_mode='none'),e.auto_review_enabled,e.attendance_mode,e.vacation_days FROM employees e ORDER BY e.last_name COLLATE NOCASE,e.first_name COLLATE NOCASE,e.id";
 if(sqlite3_prepare_v2(db,query,-1,&q,NULL)!=SQLITE_OK){send_response(fd,500,"application/json","{\"error\":\"The clock could not complete this. Try again.\"}");return;}
 sqlite3_str *o=sqlite3_str_new(db);sqlite3_str_appendall(o,"{\"employees\":[");int first=1;
 while(sqlite3_step(q)==SQLITE_ROW){
  if(!first)sqlite3_str_appendchar(o,1,',');
  first=0;
  sqlite3_str_appendf(o,"{\"id\":%d,\"name\":",sqlite3_column_int(q,0));str_json(o,(const char*)sqlite3_column_text(q,1));
  sqlite3_str_appendall(o,",\"email\":");str_json(o,(const char*)sqlite3_column_text(q,2));
  sqlite3_str_appendf(o,",\"active\":%d,\"cards\":",sqlite3_column_int(q,3));str_json(o,(const char*)sqlite3_column_text(q,4));
  sqlite3_str_appendall(o,",\"manager_cards\":");str_json(o,(const char*)sqlite3_column_text(q,5));
  sqlite3_str_appendf(o,",\"manager\":%d,\"late_email_enabled\":%d,\"first_name\":",sqlite3_column_int(q,6),sqlite3_column_int(q,7));str_json(o,(const char*)sqlite3_column_text(q,8));
  sqlite3_str_appendall(o,",\"last_name\":");str_json(o,(const char*)sqlite3_column_text(q,9));
  sqlite3_str_appendall(o,",\"position\":");str_json(o,(const char*)sqlite3_column_text(q,10));
  sqlite3_str_appendf(o,",\"incomplete\":%s,\"phone\":",sqlite3_column_int(q,11)?"true":"false");str_json(o,(const char*)sqlite3_column_text(q,12));sqlite3_str_appendf(o,",\"has_schedule\":%s,\"schedule_exempt\":%s,\"auto_review_enabled\":%s,\"attendance_mode\":\"%s\"",sqlite3_column_int(q,13)?"true":"false",sqlite3_column_int(q,14)?"true":"false",sqlite3_column_int(q,15)?"true":"false",sqlite3_column_text(q,16));
  if(sqlite3_column_type(q,17)==SQLITE_NULL)sqlite3_str_appendall(o,",\"vacation_days\":null}");else sqlite3_str_appendf(o,",\"vacation_days\":%g}",sqlite3_column_double(q,17));
 }
 sqlite3_finalize(q);sqlite3_str_appendall(o,"]}");send_str(fd,"application/json",o);
}
static int op_admin_change(int fd,const char *body,int who);
/* Every employee welcome message uses the same durable email_queue and SMTP mailer. */
static int welcome_enqueue_ex(long employee_id,int manual){
 sqlite3_stmt*q=NULL;char email[260]="",first[160]="",display[200]="";
 int ok=sqlite3_prepare_v2(db,"SELECT coalesce(email,''),coalesce(first_name,''),name FROM employees WHERE id=?1 AND active=1 AND (?2=1 OR (welcomed_at IS NULL AND created_at IS NOT NULL AND julianday('now')-julianday(created_at) BETWEEN 0 AND 7))",-1,&q,NULL)==SQLITE_OK;
 int eligible=0;
 if(ok){sqlite3_bind_int64(q,1,employee_id);sqlite3_bind_int(q,2,manual);if(sqlite3_step(q)==SQLITE_ROW){eligible=1;snprintf(email,sizeof email,"%s",(const char*)sqlite3_column_text(q,0));snprintf(first,sizeof first,"%s",(const char*)sqlite3_column_text(q,1));snprintf(display,sizeof display,"%s",(const char*)sqlite3_column_text(q,2));}}sqlite3_finalize(q);
 if(!ok)return 0;
 if(!eligible)return 1;
 if(!strchr(email,'@')||strpbrk(email,"\r\n ")||!email[0])return 1;
 char subject[220]="Welcome to the timeclock";
 char message[2400];
 sqlite3_stmt *tpl=NULL;int template_enabled=0;char template_body[2300]="Hello {{employee_name}},\n\nYou can request time off at {{timeoff_link}}.\n\nFor help with time off or your recorded timesheet, contact your manager.";
 if(sqlite3_prepare_v2(db,"SELECT subject,body FROM email_templates WHERE id='welcome_employee' AND enabled=1",-1,&tpl,NULL)==SQLITE_OK && sqlite3_step(tpl)==SQLITE_ROW){template_enabled=1;snprintf(subject,sizeof subject,"%s",(const char*)sqlite3_column_text(tpl,0));snprintf(template_body,sizeof template_body,"%s",(const char*)sqlite3_column_text(tpl,1));}sqlite3_finalize(tpl);
 if(!template_enabled)return manual?0:1;
 sqlite3_str *msg=sqlite3_str_new(db);const char *cursor=template_body,*token="{{employee_name}}";size_t token_len=strlen(token);const char *hit;
 while((hit=strstr(cursor,token))){sqlite3_str_append(msg,cursor,(int)(hit-cursor));sqlite3_str_appendall(msg,first[0]?first:display);cursor=hit+token_len;}
 sqlite3_str_appendall(msg,cursor);char *rendered=sqlite3_str_finish(msg);if(!rendered)return 0;
 char *portal=strstr(rendered,"{{timeoff_link}}");
 if(portal){char base[270]="";note_link_base(base,sizeof base);if(!base[0]){const char *phrase="To request time off, visit {{timeoff_link}}.";char *at=strstr(rendered,phrase);if(at){sqlite3_str *clean=sqlite3_str_new(db);sqlite3_str_append(clean,rendered,(int)(at-rendered));sqlite3_str_appendall(clean,"Ask your manager to request time off.");sqlite3_str_appendall(clean,at+strlen(phrase));char *tmp=sqlite3_str_finish(clean);sqlite3_free(rendered);rendered=tmp;if(!rendered)return 0;portal=NULL;}}if(portal){sqlite3_str *expanded=sqlite3_str_new(db);sqlite3_str_append(expanded,rendered,(int)(portal-rendered));if(base[0]){sqlite3_str_appendall(expanded,base);sqlite3_str_appendall(expanded,"/time-off");}else sqlite3_str_appendall(expanded,"your manager");sqlite3_str_appendall(expanded,portal+strlen("{{timeoff_link}}"));char *tmp=sqlite3_str_finish(expanded);sqlite3_free(rendered);rendered=tmp;if(!rendered)return 0;}}
 snprintf(message,sizeof message,"%s",rendered);sqlite3_free(rendered);
 char key[120];if(manual){char token[33];if(!random_hex(token,16))return 0;snprintf(key,sizeof key,"welcome:%ld:resend:%s",employee_id,token);}else snprintf(key,sizeof key,"welcome:%ld",employee_id);
 q=NULL;ok=sqlite3_prepare_v2(db,"INSERT OR IGNORE INTO email_queue(recipient,subject,body,status,next_attempt,event_id) VALUES(?1,?2,?3,'pending',strftime('%Y-%m-%dT%H:%M:%fZ','now'),?4)",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,email,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,subject,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,message,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,4,key,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 if(ok&&!manual){q=NULL;ok=sqlite3_prepare_v2(db,"UPDATE employees SET welcomed_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=?1 AND welcomed_at IS NULL",-1,&q,NULL)==SQLITE_OK;
  if(ok){sqlite3_bind_int64(q,1,employee_id);ok=sqlite3_step(q)==SQLITE_DONE&&sqlite3_changes(db)==1;}sqlite3_finalize(q);}
 return ok;
}
static int welcome_enqueue(long employee_id){return welcome_enqueue_ex(employee_id,0);}
/* Manager-only resend; reuses the normal SMTP queue and records the requester. */
static void welcome_resend(int fd,int who,const char *body){
 if(who<=0){send_response(fd,401,"application/json","{\"error\":\"Sign in again.\"}");return;}
 char value[24]="";if(!form_value(body,"id",value,sizeof value)){send_response(fd,400,"application/json","{\"error\":\"Select an employee.\"}");return;}
 char *end=NULL;long id=strtol(value,&end,10);
 if(id<=0||!end||*end){send_response(fd,400,"application/json","{\"error\":\"Invalid employee.\"}");return;}
 sqlite3_stmt*q=NULL;int eligible=0,pending=0;
 if(sqlite3_prepare_v2(db,"SELECT 1 FROM employees WHERE id=?1 AND active=1 AND email LIKE '%@%' AND instr(email,' ')=0",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int64(q,1,id);eligible=sqlite3_step(q)==SQLITE_ROW;}sqlite3_finalize(q);
 if(!eligible){send_response(fd,409,"application/json","{\"error\":\"Employee must be active and have an email address.\"}");return;}
 if(sqlite3_prepare_v2(db,"SELECT 1 FROM email_queue WHERE (event_id=?1 OR event_id LIKE ?1 || ':%') AND status='pending' LIMIT 1",-1,&q,NULL)==SQLITE_OK){char pattern[80];snprintf(pattern,sizeof pattern,"welcome:%ld",id);sqlite3_bind_text(q,1,pattern,-1,SQLITE_TRANSIENT);pending=sqlite3_step(q)==SQLITE_ROW;}sqlite3_finalize(q);
 if(pending){send_response(fd,200,"application/json","{\"status\":\"already_pending\"}");return;}
 if(!sql("BEGIN IMMEDIATE")){send_response(fd,500,"application/json","{\"error\":\"Could not queue message.\"}");return;}
 int ok=1;
 q=NULL;if(sqlite3_prepare_v2(db,"SELECT 1 FROM email_queue WHERE (event_id=?1 OR event_id LIKE ?1 || ':%') AND status='pending' LIMIT 1",-1,&q,NULL)==SQLITE_OK){char pattern[80];snprintf(pattern,sizeof pattern,"welcome:%ld",id);sqlite3_bind_text(q,1,pattern,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW)ok=0;}sqlite3_finalize(q);
 if(!ok){sql("ROLLBACK");send_response(fd,200,"application/json","{\"status\":\"already_pending\"}");return;}
 ok=welcome_enqueue_ex(id,1);
 if(ok){q=NULL;ok=sqlite3_prepare_v2(db,"INSERT INTO employee_change_audit(manager_id,employee_id,change_body) VALUES(?1,?2,'Resent welcome email')",-1,&q,NULL)==SQLITE_OK;if(ok){sqlite3_bind_int(q,1,who);sqlite3_bind_int64(q,2,id);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);}
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");
 if(ok)send_response(fd,200,"application/json","{\"status\":\"queued\"}");else send_response(fd,500,"application/json","{\"error\":\"Welcome message could not be queued. Check the email template.\"}");
}
static void management_save(int fd,const char *body,int who,int direct){
 if(op_admin_change(fd,body,who))return;
 char id_s[20]="",first[130]="",last[130]="",position[170]="",name[264]="",email[220]="",active[8]="",manager[8]="",uid[80]="",late_email[8]="",auto_review[8]="1",phone[48]="",attendance_mode[24]="punch_required",vacation[24]="";
 int vacation_present=form_has(body,"vacation_days");double vacation_days=0;
 if(vacation_present&&!form_value(body,"vacation_days",vacation,sizeof vacation)){send_response(fd,400,"application/json","{\"error\":\"Check the vacation allowance.\"}");return;}
 if(vacation_present&&*vacation){
  char *end=NULL;vacation_days=strtod(vacation,&end);
  if(!end||*end||strspn(vacation,"0123456789.")!=strlen(vacation)||!(vacation_days>=0&&vacation_days<=366)||vacation_days*2!=(int)(vacation_days*2)){
   send_response(fd,400,"application/json","{\"error\":\"Vacation allowance needs whole or half days from 0 to 366, or leave it blank.\"}");return;
  }
 }
 const char *identity=employee_identity_error(body,first,sizeof first,last,sizeof last,position,sizeof position);
 if(identity){send_response(fd,400,"application/json",identity);return;}
 snprintf(name,sizeof name,"%s %s",first,last);
 if(!form_value(body,"id",id_s,sizeof id_s)||!form_value(body,"email",email,sizeof email)||!form_value(body,"phone",phone,sizeof phone)||!form_value(body,"active",active,sizeof active)||!form_value(body,"manager",manager,sizeof manager)||!form_value(body,"uid",uid,sizeof uid)||!form_value(body,"late_email_enabled",late_email,sizeof late_email)){
  send_response(fd,400,"application/json","{\"error\":\"Check the entries and try again.\"}");return;
 }
 if(form_value(body,"attendance_mode",attendance_mode,sizeof attendance_mode) && strcmp(attendance_mode,"punch_required") && strcmp(attendance_mode,"vacation_only")){send_response(fd,400,"application/json","{\"error\":\"Invalid attendance mode.\"}");return;}
 if(form_value(body,"auto_review_enabled",auto_review,sizeof auto_review) && strcmp(auto_review,"0") && strcmp(auto_review,"1")){send_response(fd,400,"application/json","{\"error\":\"Check the review email setting.\"}");return;}
 if(strlen(email)>200||strchr(email,'\n')){
  send_response(fd,400,"application/json","{\"error\":\"Employee not found. Reload the page.\"}");return;
 }
 /* Allow international phone formats; store as text to preserve + and extensions. */
 int digits=0;
 for(size_t i=0;phone[i];i++){
  unsigned char c=(unsigned char)phone[i];
  if(isdigit(c))digits++;
  else if(!(c=='+'||c=='-'||c=='('||c==')'||c==' '||c=='.'||c=='x'||c=='X')){
   send_response(fd,400,"application/json","{\"error\":\"Enter a valid phone number.\"}");return;
  }
 }
 if(phone[0]&&(digits<7||digits>15)){
  send_response(fd,400,"application/json","{\"error\":\"Phone numbers need 7 to 15 digits.\"}");return;
 }
 char *tail=NULL;long id=strtol(id_s,&tail,10);
 if(!*id_s||*tail||id<0||id>2147483647||(strcmp(active,"0")&&strcmp(active,"1"))||(strcmp(manager,"0")&&strcmp(manager,"1"))||(strcmp(late_email,"0")&&strcmp(late_email,"1"))){
  send_response(fd,400,"application/json","{\"error\":\"Check the entries and try again.\"}");return;
 }
 for(size_t i=0;uid[i];i++){if(!isxdigit((unsigned char)uid[i])){send_response(fd,400,"application/json","{\"error\":\"Enter a card UID of 8 to 20 hexadecimal characters.\"}");return;}uid[i]=(char)toupper((unsigned char)uid[i]);}
 if(uid[0]&&strlen(uid)!=8&&strlen(uid)!=14&&strlen(uid)!=20){send_response(fd,400,"application/json","{\"error\":\"Enter a card UID of 8 to 20 hexadecimal characters.\"}");return;}
 /* Validate card ownership before modifying any employee or role. */
 if(uid[0]){
  sqlite3_stmt *v=NULL;int rc=SQLITE_ERROR;
  if(sqlite3_prepare_v2(db,"SELECT manager_id FROM manager_cards WHERE uid=?1 COLLATE NOCASE",-1,&v,NULL)==SQLITE_OK){sqlite3_bind_text(v,1,uid,-1,SQLITE_TRANSIENT);rc=sqlite3_step(v);}sqlite3_finalize(v);
  if(rc==SQLITE_ROW){send_response(fd,409,"application/json","{\"error\":\"That card is a manager card. Use a different card.\"}");return;}
  if(rc!=SQLITE_DONE){send_response(fd,500,"application/json","{\"error\":\"Could not check the card. Try again.\"}");return;}
  v=NULL;rc=SQLITE_ERROR;long owner=0;
  if(sqlite3_prepare_v2(db,"SELECT employee_id FROM cards WHERE uid=?1 COLLATE NOCASE",-1,&v,NULL)==SQLITE_OK){sqlite3_bind_text(v,1,uid,-1,SQLITE_TRANSIENT);rc=sqlite3_step(v);if(rc==SQLITE_ROW)owner=sqlite3_column_int64(v,0);}sqlite3_finalize(v);
  if(rc==SQLITE_ROW&&owner!=id){send_response(fd,409,"application/json","{\"error\":\"That card belongs to another employee. Nothing was changed.\"}");return;}
  if(rc!=SQLITE_ROW&&rc!=SQLITE_DONE){send_response(fd,500,"application/json","{\"error\":\"Could not check the card. Try again.\"}");return;}
 }
 if(!sql("BEGIN IMMEDIATE")){send_response(fd,500,"application/json","{\"error\":\"The clock is busy. Try again.\"}");return;}

 int ok=1;sqlite3_stmt *q=NULL;
 if(id==0){ok=sqlite3_prepare_v2(db,"INSERT INTO employees(name,email,active,late_email_enabled,first_name,last_name,position,phone,created_at) VALUES(?1,?2,?3,?4,?6,?7,?8,?9,strftime('%Y-%m-%dT%H:%M:%fZ','now'))",-1,&q,NULL)==SQLITE_OK;}
 else {ok=sqlite3_prepare_v2(db,"UPDATE employees SET name=?1,email=?2,active=?3,late_email_enabled=?4,first_name=?6,last_name=?7,position=?8,phone=?9 WHERE id=?5",-1,&q,NULL)==SQLITE_OK;}
 if(ok){sqlite3_bind_text(q,1,name,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,email,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,3,active[0]=='1');sqlite3_bind_int(q,4,late_email[0]=='1');if(id)sqlite3_bind_int(q,5,(int)id);sqlite3_bind_text(q,6,first,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,7,last,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,8,position,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,9,phone,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE&&(id==0||sqlite3_changes(db)==1);}
 sqlite3_finalize(q);if(id==0&&ok)id=sqlite3_last_insert_rowid(db);
 if(ok){sqlite3_stmt*mode=NULL;ok=sqlite3_prepare_v2(db,"UPDATE employees SET attendance_mode=?1 WHERE id=?2",-1,&mode,NULL)==SQLITE_OK;if(ok){sqlite3_bind_text(mode,1,attendance_mode,-1,SQLITE_TRANSIENT);sqlite3_bind_int64(mode,2,id);ok=sqlite3_step(mode)==SQLITE_DONE;}sqlite3_finalize(mode);}
 if(ok&&vacation_present){sqlite3_stmt *allowance=NULL;ok=sqlite3_prepare_v2(db,"UPDATE employees SET vacation_days=?1 WHERE id=?2",-1,&allowance,NULL)==SQLITE_OK;if(ok){if(*vacation)sqlite3_bind_double(allowance,1,vacation_days);else sqlite3_bind_null(allowance,1);sqlite3_bind_int64(allowance,2,id);ok=sqlite3_step(allowance)==SQLITE_DONE;}sqlite3_finalize(allowance);}
 if(ok){sqlite3_stmt*pref=NULL;ok=sqlite3_prepare_v2(db,"UPDATE employees SET auto_review_enabled=?1 WHERE id=?2",-1,&pref,NULL)==SQLITE_OK;if(ok){sqlite3_bind_int(pref,1,auto_review[0]=='1');sqlite3_bind_int64(pref,2,id);ok=sqlite3_step(pref)==SQLITE_DONE;}sqlite3_finalize(pref);}
 if(ok){ok=sqlite3_prepare_v2(db,"DELETE FROM employee_roles WHERE employee_id=?1 AND role='manager'",-1,&q,NULL)==SQLITE_OK;if(ok){sqlite3_bind_int64(q,1,id);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);}
 if(ok&&manager[0]=='1'){ok=sqlite3_prepare_v2(db,"INSERT INTO employee_roles(employee_id,role) VALUES(?1,'manager')",-1,&q,NULL)==SQLITE_OK;if(ok){sqlite3_bind_int64(q,1,id);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);}
 /* Refuse manager cards and cards assigned to a different employee. */
 if(ok&&uid[0]){
  ok=sqlite3_prepare_v2(db,"SELECT 1 FROM manager_cards WHERE uid=?1 COLLATE NOCASE",-1,&q,NULL)==SQLITE_OK;
  if(ok){sqlite3_bind_text(q,1,uid,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)!=SQLITE_ROW;}sqlite3_finalize(q);q=NULL;
 }
 if(ok&&uid[0]){
  ok=sqlite3_prepare_v2(db,"SELECT employee_id FROM cards WHERE uid=?1 COLLATE NOCASE",-1,&q,NULL)==SQLITE_OK;
  if(ok){sqlite3_bind_text(q,1,uid,-1,SQLITE_TRANSIENT);int rc=sqlite3_step(q);if(rc==SQLITE_ROW)ok=sqlite3_column_int64(q,0)==id;else ok=rc==SQLITE_DONE;}sqlite3_finalize(q);q=NULL;
 }
 /* Additive card registration: preserve every other card assigned to this employee. */
 if(ok&&uid[0]){
  ok=sqlite3_prepare_v2(db,"INSERT INTO cards(uid,employee_id,active) VALUES(?1,?2,1) ON CONFLICT(uid) DO UPDATE SET employee_id=excluded.employee_id,active=1",-1,&q,NULL)==SQLITE_OK;
  if(ok){sqlite3_bind_text(q,1,uid,-1,SQLITE_TRANSIENT);sqlite3_bind_int64(q,2,id);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
  /* The card is now assigned; drop it from the unassigned-sightings list. */
  if(ok){ok=sqlite3_prepare_v2(db,"DELETE FROM unknown_cards WHERE uid=?1",-1,&q,NULL)==SQLITE_OK;if(ok){sqlite3_bind_text(q,1,uid,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);}
 }
 if(ok){
  /* All administrative edits must preserve at least one usable manager card. */
  int count=0;
  const char *guard="SELECT COUNT(*) FROM manager_cards mc JOIN employees e ON e.id=mc.manager_id JOIN employee_roles er ON er.employee_id=e.id AND er.role='manager' JOIN user_accounts ua ON ua.employee_id=e.id WHERE mc.active=1 AND e.active=1 AND ua.disabled=0";
  ok=sqlite3_prepare_v2(db,guard,-1,&q,NULL)==SQLITE_OK;
  if(ok){if(sqlite3_step(q)==SQLITE_ROW)count=sqlite3_column_int(q,0);else ok=0;}
  sqlite3_finalize(q);q=NULL;
  if(ok&&count<1)ok=0;
 }
 if(ok&&direct){
  sqlite3_stmt *audit=NULL;
  ok=sqlite3_prepare_v2(db,"INSERT INTO employee_change_audit(manager_id,employee_id,change_body) VALUES(?1,?2,?3)",-1,&audit,NULL)==SQLITE_OK;
  if(ok){sqlite3_bind_int(audit,1,who);sqlite3_bind_int64(audit,2,id);sqlite3_bind_text(audit,3,body,-1,SQLITE_TRANSIENT);ok=sqlite3_step(audit)==SQLITE_DONE;}
  sqlite3_finalize(audit);
 }
 if(ok)ok=welcome_enqueue(id);
 if(ok){if(!sql("COMMIT")){sql("ROLLBACK");ok=0;}}else sql("ROLLBACK");
 if(ok){char reply[64];snprintf(reply,sizeof reply,"{\"status\":\"saved\",\"id\":%ld}",id);send_response(fd,200,"application/json",reply);}
 else send_response(fd,409,"application/json","{\"error\":\"Not saved. The card may already be assigned.\"}");
}
/* A one-use authorization request carries the proposed employee change in SQLite.
   A known, active manager card must be scanned after the request is armed. */
/* Exclusive scanner lease: tested inside BEGIN IMMEDIATE for every workflow. */
/* Unique per-operation browser token, not an alternative to physical manager authorization. */
static int approval_token_new(char out[33]){
 if(!random_hex(out,16))return 0;
 sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(db,"UPDATE manager_scan SET request_token=?1 WHERE id=1",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,out,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);return ok;
}
static int approval_token_matches(const char *body){
 char token[64]="";if(!body||!form_value(body,"token",token,sizeof token)||strlen(token)!=32)return 0;
 sqlite3_stmt*q=NULL;int ok=0;
 if(sqlite3_prepare_v2(db,"SELECT 1 FROM manager_scan WHERE request_token=?1 AND armed_at>0",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,token,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_ROW;}sqlite3_finalize(q);return ok;
}
static int authorization_pending(void){
 sqlite3_stmt *q=NULL;int busy=1;
 const char *query="SELECT (SELECT count(*) FROM manager_authorization WHERE armed_at>=CAST(strftime('%s','now') AS INTEGER)-30)+(SELECT count(*) FROM schedule_authorization WHERE armed_at>=CAST(strftime('%s','now') AS INTEGER)-30)+(SELECT count(*) FROM payroll_authorization WHERE armed_at>=CAST(strftime('%s','now') AS INTEGER)-30)+(SELECT count(*) FROM system_actions WHERE armed_at>=CAST(strftime('%s','now') AS INTEGER)-30)+(SELECT count(*) FROM account_authorization WHERE armed_at>=CAST(strftime('%s','now') AS INTEGER)-30)";
 if(sqlite3_prepare_v2(db,query,-1,&q,NULL)==SQLITE_OK && sqlite3_step(q)==SQLITE_ROW)busy=sqlite3_column_int(q,0)>0;
 sqlite3_finalize(q);return busy;
}
/* Routine employee enrollment and edits: authenticated manager only.
   Role and activation changes are still handled by card-authorized workflow. */
static void employee_save_routine(int fd,int who,const char *body){
 if(who<=0){send_response(fd,401,"application/json","{\"error\":\"Sign in again.\"}");return;}
 if(!body||strlen(body)>3500){send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
 char forbidden_op[40]="";if(form_value(body,"op",forbidden_op,sizeof forbidden_op)){send_response(fd,403,"application/json","{\"error\":\"This change requires a manager card.\"}");return;}
 char idtext[24]="",newrole[8]="",newactive[8]="";
 if(!form_value(body,"id",idtext,sizeof idtext)||!form_value(body,"manager",newrole,sizeof newrole)||!form_value(body,"active",newactive,sizeof newactive)){
  send_response(fd,400,"application/json","{\"error\":\"Fill in all required fields.\"}");return;
 }
 char *end=NULL;long id=strtol(idtext,&end,10);
 if(!idtext[0]||*end||id<0||id>2147483647||(strcmp(newrole,"0")&&strcmp(newrole,"1"))||(strcmp(newactive,"0")&&strcmp(newactive,"1"))){
  send_response(fd,400,"application/json","{\"error\":\"Check the entries and try again.\"}");return;
 }
 int role=0,active=1;
 if(id){sqlite3_stmt*q=NULL;int found=0;
  if(sqlite3_prepare_v2(db,"SELECT e.active,EXISTS(SELECT 1 FROM employee_roles r WHERE r.employee_id=e.id AND r.role='manager') FROM employees e WHERE e.id=?1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,(int)id);if(sqlite3_step(q)==SQLITE_ROW){found=1;active=sqlite3_column_int(q,0);role=sqlite3_column_int(q,1);}}sqlite3_finalize(q);
  if(!found){send_response(fd,404,"application/json","{\"error\":\"Employee not found. Reload the page.\"}");return;}
 }
 if((newrole[0]=='1')!=role||(newactive[0]=='1')!=active){send_response(fd,403,"application/json","{\"error\":\"This change requires a manager card.\"}");return;}
 management_save(fd,body,who,1);
}
static void employee_change_arm(int fd,int who,const char *body){
 if(!body||strlen(body)>3500){send_response(fd,400,"application/json","{\"error\":\"Check the entries and try again.\"}");return;}
 {char op[32],f[130],l[130],p[170];
  /* card_status is the only non-employee operation (manager swipe notes were removed in v0.9.48). */
  if(form_value(body,"op",op,sizeof op)&&strcmp(op,"card_status")){send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
  if(!form_value(body,"op",op,sizeof op)){const char *identity=employee_identity_error(body,f,sizeof f,l,sizeof l,p,sizeof p);if(identity){send_response(fd,400,"application/json",identity);return;}}}
 if(!sql("BEGIN IMMEDIATE")){send_response(fd,503,"application/json","{\"error\":\"The clock is busy. Try again.\"}");return;}
 if(authorization_pending()){sql("ROLLBACK");send_response(fd,409,"application/json","{\"error\":\"The card reader is waiting for another approval. Try again in 30 seconds.\"}");return;}
 sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(db,"INSERT INTO manager_authorization(id,requested_by,change_body,armed_at) VALUES(1,?1,?2,CAST(strftime('%s','now') AS INTEGER)) ON CONFLICT(id) DO UPDATE SET requested_by=excluded.requested_by,change_body=excluded.change_body,armed_at=excluded.armed_at",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,who);sqlite3_bind_text(q,2,body,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 char token[33]="";if(ok)ok=sql("UPDATE manager_scan SET armed_at=CAST(strftime('%s','now') AS INTEGER),uid='',scanned_at=0,scan_mode='approval' WHERE id=1");
 if(ok)ok=approval_token_new(token);
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");
 char result[160];snprintf(result,sizeof result,"{\"status\":\"awaiting manager card\",\"token\":\"%s\"}",token);
 send_response(fd,ok?200:500,"application/json",ok?result:"{\"error\":\"The clock could not complete this. Try again.\"}");
}
static void employee_change_status(int fd,int who){
 sqlite3_stmt*q=NULL;char body[4096]="";long long armed=0,scanned=0;char uid[40]="";int owner=0;
 const char *query="SELECT a.requested_by,a.change_body,a.armed_at,s.uid,s.scanned_at FROM manager_authorization a JOIN manager_scan s ON s.id=1 WHERE a.id=1";
 if(sqlite3_prepare_v2(db,query,-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW){
 owner=sqlite3_column_int(q,0);const char*b=(const char*)sqlite3_column_text(q,1);if(b)snprintf(body,sizeof body,"%s",b);
 armed=sqlite3_column_int64(q,2);const char*u=(const char*)sqlite3_column_text(q,3);if(u)snprintf(uid,sizeof uid,"%s",u);scanned=sqlite3_column_int64(q,4);
 }sqlite3_finalize(q);
 if(owner!=who||!armed){send_response(fd,409,"application/json","{\"error\":\"This approval is no longer valid. Start again.\"}");return;}
 if((long long)time(NULL)-armed>30){sql("DELETE FROM manager_authorization WHERE id=1");send_response(fd,200,"application/json","{\"status\":\"expired\"}");return;}
 if(!uid[0]||scanned<armed){send_response(fd,200,"application/json","{\"status\":\"waiting\"}");return;}
 int authorized=0;
 const char *check="SELECT 1 FROM manager_cards mc JOIN employees e ON e.id=mc.manager_id JOIN employee_roles r ON r.employee_id=e.id AND r.role='manager' JOIN user_accounts ua ON ua.employee_id=e.id WHERE mc.uid=?1 COLLATE NOCASE AND mc.active=1 AND e.active=1 AND ua.disabled=0";
 if(sqlite3_prepare_v2(db,check,-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,uid,-1,SQLITE_TRANSIENT);authorized=sqlite3_step(q)==SQLITE_ROW;}sqlite3_finalize(q);
 if(!authorized){sql("UPDATE manager_scan SET uid='',scanned_at=0 WHERE id=1");send_response(fd,200,"application/json","{\"status\":\"waiting\",\"message\":\"registered manager card required\"}");return;}
 /* Consume scan before performing the write, preventing re-use of this scan. */
 if(!sql("DELETE FROM manager_authorization WHERE id=1")||!sql("UPDATE manager_scan SET armed_at=0,uid='',scanned_at=0 WHERE id=1")){
 send_response(fd,500,"application/json","{\"error\":\"The clock could not complete this. Try again.\"}");return;}
 snprintf(approval_card_uid,sizeof approval_card_uid,"%s",uid);
 management_save(fd,body,who,0);
 approval_card_uid[0]=0;
}
static void management_page(int fd){
 static const char page[]=
 UI_HEAD("Employees","employees")"<h1>Employees</h1><p id='empbanner' role='alert' hidden></p>"
 "<section><div class='section-head'><h2>Employee records</h2><div class='actions'><button type='button' id='addEmployee'>Add employee</button></div></div><p class='muted'>Select a row to edit.</p><p id='status' role='status'></p><table><thead><tr><th>Name</th><th>Position</th><th>Email</th><th>Phone</th><th>Attendance cards</th><th>Manager cards</th><th>Manager</th><th>Status</th></tr></thead><tbody id='rows'></tbody></table></section>"
 "<dialog id='employeeDialog' aria-labelledby='heading'><div class='section-head'><h2 id='heading'>Add employee</h2><div class='actions'><button type='button' id='closeEmployee' aria-label='Close'>Close</button></div></div><p id='dlgbanner' role='alert' hidden></p><form id='form'><input type='hidden' id='id' value='0'><div class='grid'><label>First name<input id='first_name' maxlength='60' autocomplete='off' required></label><label>Last name<input id='last_name' maxlength='60' autocomplete='off' required></label><label>Position<input id='position' list='positions' maxlength='80' autocomplete='off' required></label><datalist id='positions'></datalist><label>Email<input id='email' type='email' maxlength='200'></label><label>Phone (optional)<input id='phone' type='tel' maxlength='40' autocomplete='tel' placeholder='+1 403 555 0100'></label><label>Vacation allowance (days)<input id='vacation_days' type='number' min='0' max='366' step='0.5' placeholder='Unspecified' aria-describedby='vacation-hint'></label><label>Attendance mode<select id='attendance_mode'><option value='punch_required'>Punch required</option><option value='vacation_only'>Vacation only</option></select></label><label>Add attendance card<input id='uid' placeholder='Scan or enter a card UID' maxlength='20' autocomplete='off'></label></div><p id='vacation-hint' class='muted'>Annual vacation allowance is per calendar year and for reference; requests may exceed this amount.</p><div class='actions'><button type='button' id='scan'>Scan card (30 seconds)</button><span id='scanstatus' class='muted' role='status'></span></div><p><label><input id='active' type='checkbox' checked> Active</label> <label><input id='manager' type='checkbox'> Manager</label> <label><input id='late_email_enabled' type='checkbox' checked> Late-arrival emails (employee and manager)</label> <label><input id='auto_review_enabled' type='checkbox' checked> Automatically email timesheet links</label></p><div class='actions'><button type='button' id='resendWelcome' hidden>Resend welcome email</button><button type='button' id='sendVacation' hidden>Send vacation link</button></div><div class='actions'><button type='submit'>Save employee</button><button type='button' id='cancelEmployee'>Cancel</button></div><p id='message' role='status'></p></form><p class='muted'>Changing Manager or Active requires a manager card. Create sign-in accounts under <a href='/accounts'>Manager accounts</a>.</p><div id='assigned_cards' class='muted'>No cards yet.</div></dialog>"
 "<script>let records=[]; const msg=document.getElementById('message');"
 "const call=tc.api;"
 "function issues(e){let x=[];if(!e.active)return x;if(e.incomplete)x.push('Incomplete');if((e.late_email_enabled||e.auto_review_enabled)&&!e.email)x.push('No email');if(e.attendance_mode!=='vacation_only'&&e.cards&&!e.has_schedule&&!e.schedule_exempt)x.push('No schedule');return x}"
 "const why={'Incomplete':'first name, last name or position is missing','No email':'late-arrival emails are on but no address is set','No schedule':'late and missing-punch notices are not sent'};"
 "async function load(){try{let j=await call('/api/v1/admin/employees');records=j.employees;let b=document.getElementById('rows');b.replaceChildren();let flagged=0,pos=new Set();for(let e of records){if(e.position)pos.add(e.position);let tr=document.createElement('tr');let nm=e.last_name&&e.first_name?e.last_name+', '+e.first_name:(e.first_name||e.last_name||e.name);for(let v of [nm,e.position,e.email,e.phone||'',e.cards.replaceAll(',',', '),e.manager_cards.replaceAll(',',', '),e.manager?'Yes':'No']){let td=document.createElement('td');td.textContent=v;tr.append(td)}let st=document.createElement('td'),list=issues(e);if(!e.active)st.append(tc.flag('Inactive','muted'));else st.append('Active ');if(e.active&&e.schedule_exempt)st.append(tc.flag('No fixed schedule','muted'));for(let i of list){let f=tc.flag(i,'check');f.title=why[i];st.append(' ',f)}if(list.length)flagged++;tr.append(st);tr.className='employee-selectable'+(list.length?' row-check':'');tr.tabIndex=0;tr.setAttribute('role','button');tr.setAttribute('aria-label','Edit employee '+nm);tr.onclick=()=>edit(e.id);tr.onkeydown=ev=>{if(ev.key==='Enter'||ev.key===' '){ev.preventDefault();edit(e.id)}};b.append(tr)}document.getElementById('positions').replaceChildren(...[...pos].sort().map(p=>{let o=document.createElement('option');o.value=p;return o}));tc.banner(document.getElementById('empbanner'),'check','Check',flagged?flagged+(flagged===1?' employee record needs':' employee records need')+' attention. Flagged rows are marked below.':'');}catch(e){tc.err(document.getElementById('status'),e);}}"
 "function edit(id){resetScan();let e=records.find(v=>v.id===id);if(!e)return;for(let k of ['id','first_name','last_name','position','email','phone'])document.getElementById(k).value=e[k]||'';document.getElementById('vacation_days').value=(e.vacation_days==null?'':e.vacation_days);document.getElementById('uid').value='';document.getElementById('active').checked=!!e.active;document.getElementById('manager').checked=!!e.manager;document.getElementById('late_email_enabled').checked=!!e.late_email_enabled;document.getElementById('auto_review_enabled').checked=!!e.auto_review_enabled;document.getElementById('attendance_mode').value=e.attendance_mode||'punch_required';document.getElementById('heading').textContent='Edit '+((e.first_name+' '+e.last_name).trim()||e.name);renderCards(e);let resend=document.getElementById('resendWelcome');resend.hidden=!(e.active&&e.email);resend.dataset.id=String(e.id);let vacation=document.getElementById('sendVacation');vacation.hidden=!(e.active&&e.email);vacation.dataset.id=String(e.id);let list=issues(e);tc.banner(document.getElementById('dlgbanner'),'check','Check',list.map(i=>i+': '+why[i]).join('. '));msg.textContent='';if(!document.getElementById('employeeDialog').open)document.getElementById('employeeDialog').showModal();document.getElementById('scanstatus').textContent=e.manager_cards?'Manager card: '+e.manager_cards.replaceAll(',',', ')+' (see Manager cards)':'';}"
 "async function revokeCard(uid){let id=Number(document.getElementById('id').value);if(!id||!confirm('Revoke card '+uid+'? Past punches are kept.'))return;try{await call('/api/v1/admin/employees/cards/revoke',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({id:String(id),uid}).toString()});await load();edit(id);tc.ok(msg,'Card revoked. Each clock applies this at its next sync.');}catch(e){tc.err(msg,e);}}"
 "function renderCards(e){let box=document.getElementById('assigned_cards');box.replaceChildren();let cards=e.cards?e.cards.split(',').filter(Boolean):[];if(!cards.length){box.textContent='No attendance cards.';return;}for(let uid of cards){let item=document.createElement('div');item.style.marginBottom='8px';let code=document.createElement('code');code.textContent=uid;let btn=document.createElement('button');btn.type='button';btn.textContent='Revoke';btn.style.marginLeft='12px';btn.onclick=()=>revokeCard(uid);item.append(code,btn);box.append(item);}}"
 "let scanTimer=null,scanBusy=false;function resetScan(){if(scanTimer){clearInterval(scanTimer);scanTimer=null;}scanBusy=false;document.getElementById('scan').disabled=false;document.getElementById('scan').textContent='Scan card (30 seconds)';document.getElementById('scanstatus').textContent='';}"
 "document.getElementById('scan').onclick=async()=>{resetScan();document.getElementById('scan').disabled=true;document.getElementById('uid').value='';const state=document.getElementById('scanstatus');state.textContent='Starting reader...';try{await call('/api/v1/admin/manager-scan/arm',{method:'POST'});scanBusy=true;state.textContent='Tap the employee card on the clock.';let attempts=0;scanTimer=setInterval(async()=>{if(!scanBusy)return;try{const j=await call('/api/v1/admin/manager-scan/status');if(j.status==='scanned'&&j.uid){document.getElementById('uid').value=j.uid;document.getElementById('scan').textContent=j.uid;state.textContent='Save to assign this card.';clearInterval(scanTimer);scanTimer=null;scanBusy=false;document.getElementById('scan').disabled=false;}else if(j.status==='expired'||++attempts>48){resetScan();tc.err(state,tc.TIMEOUT);}}catch(e){resetScan();tc.err(state,e);}},700);}catch(e){resetScan();tc.err(state,e);}};"
 "function addEmployee(){document.getElementById('resendWelcome').hidden=true;document.getElementById('sendVacation').hidden=true;resetScan();document.getElementById('form').reset();document.getElementById('id').value=0;document.getElementById('heading').textContent='Add employee';document.getElementById('assigned_cards').textContent='No cards yet.';msg.textContent='';tc.banner(document.getElementById('dlgbanner'),'','','');document.getElementById('employeeDialog').showModal();}document.getElementById('addEmployee').onclick=addEmployee;document.getElementById('closeEmployee').onclick=()=>document.getElementById('employeeDialog').close();document.getElementById('cancelEmployee').onclick=()=>document.getElementById('employeeDialog').close();document.getElementById('employeeDialog').addEventListener('close',resetScan);"
 "document.getElementById('resendWelcome').onclick=async()=>{const b=document.getElementById('resendWelcome');if(!b.dataset.id)return;b.disabled=true;try{const r=await call('/api/v1/admin/employees/welcome/resend',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({id:b.dataset.id}).toString()});tc.ok(msg,r.status==='already_pending'?'Welcome email already queued.':'Welcome email queued. Check Email queue for delivery status.');}catch(e){tc.err(msg,e);}finally{b.disabled=false;}};"
 "document.getElementById('sendVacation').onclick=async()=>{const b=document.getElementById('sendVacation');if(!b.dataset.id)return;b.disabled=true;try{const r=await call('/api/v1/admin/employees/timeoff/send',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({id:b.dataset.id}).toString()});tc.ok(msg,r.status==='already_recent'?'A link was recently sent. Try again in 15 minutes.':'Private vacation link queued. Check Email queue.');}catch(e){tc.err(msg,e);}finally{b.disabled=false;}};"
 "document.getElementById('form').onsubmit=async e=>{e.preventDefault();let p=new URLSearchParams();for(let k of ['id','first_name','last_name','position','email','phone','uid','attendance_mode','vacation_days'])p.set(k,document.getElementById(k).value.trim());for(let k of ['active','manager','late_email_enabled','auto_review_enabled'])p.set(k,document.getElementById(k).checked?'1':'0');try{const current=records.find(x=>String(x.id)===p.get('id'));const privileged=current?(!!current.manager!==document.getElementById('manager').checked||!!current.active!==document.getElementById('active').checked):document.getElementById('manager').checked;if(!privileged){await call('/api/v1/admin/employees/save',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()});await load();document.getElementById('employeeDialog').close();tc.ok(document.getElementById('status'),'Employee saved.');return;}const approval=await call('/api/v1/admin/employee-change/arm',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p.toString()});msg.textContent=tc.CARD;const timer=setInterval(async()=>{try{const r=await call('/api/v1/admin/employee-change/status',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({token:approval.token})});if(r.status==='waiting')return;clearInterval(timer);if(r.status==='expired')throw Error(tc.TIMEOUT);await load();document.getElementById('employeeDialog').close();tc.ok(document.getElementById('status'),'Employee saved.');}catch(err){clearInterval(timer);tc.err(msg,err);}},700);}catch(err){tc.err(msg,err);}};load();</script>"UI_FOOT;
 send_response(fd,200,"text/html; charset=utf-8",page);
}

static void employee_card_revoke(int fd,int who,const char *body){
 if(!who){send_response(fd,401,"application/json","{\"error\":\"Sign in again.\"}");return;}
 char idstr[24],uid[65];
 if(!form_value(body,"id",idstr,sizeof idstr)||!form_value(body,"uid",uid,sizeof uid)){
  send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;
 }
 char *end=NULL;long id=strtol(idstr,&end,10);
 if(!*idstr||*end||id<=0||id>2147483647||!uid[0]||strlen(uid)>20){
  send_response(fd,400,"application/json","{\"error\":\"Enter a card UID of 8 to 20 hexadecimal characters.\"}");return;
 }
 for(size_t i=0;uid[i];i++)if(!isxdigit((unsigned char)uid[i])){
  send_response(fd,400,"application/json","{\"error\":\"Enter a card UID of 8 to 20 hexadecimal characters.\"}");return;
 }
 if(!sql("BEGIN IMMEDIATE")){send_response(fd,500,"application/json","{\"error\":\"The clock is busy. Try again.\"}");return;}
 sqlite3_stmt *q=NULL;
 int ok=sqlite3_prepare_v2(db,"UPDATE cards SET active=0 WHERE employee_id=?1 AND uid=?2 COLLATE NOCASE AND active=1",-1,&q,NULL)==SQLITE_OK;
 int changed=0;
 if(ok){sqlite3_bind_int(q,1,(int)id);sqlite3_bind_text(q,2,uid,-1,SQLITE_TRANSIENT);
  ok=sqlite3_step(q)==SQLITE_DONE;if(ok)changed=sqlite3_changes(db);}
 sqlite3_finalize(q);q=NULL;
 if(ok&&changed){
  ok=sqlite3_prepare_v2(db,"INSERT INTO employee_change_audit(manager_id,employee_id,change_body) VALUES(?1,?2,?3)",-1,&q,NULL)==SQLITE_OK;
  if(ok){char change[120];snprintf(change,sizeof change,"attendance card revoked: %s",uid);
   sqlite3_bind_int(q,1,who);sqlite3_bind_int(q,2,(int)id);sqlite3_bind_text(q,3,change,-1,SQLITE_TRANSIENT);
   ok=sqlite3_step(q)==SQLITE_DONE;}
  sqlite3_finalize(q);
 }
 if(ok&&changed)ok=sql("COMMIT");
 if(!ok||!changed){sql("ROLLBACK");send_response(fd,ok?404:500,"application/json",ok?"{\"error\":\"Card not found, or already revoked.\"}":"{\"error\":\"The clock could not complete this. Try again.\"}");return;}
 send_response(fd,200,"application/json","{\"ok\":true}");
}
/* Append text with HTML escaping. */
static void html_text(sqlite3_str*o,const char*s){
 for(;s&&*s;s++){switch(*s){case '&':sqlite3_str_appendall(o,"&amp;");break;case '<':sqlite3_str_appendall(o,"&lt;");break;case '>':sqlite3_str_appendall(o,"&gt;");break;case '\'':sqlite3_str_appendall(o,"&#39;");break;case '"':sqlite3_str_appendall(o,"&quot;");break;default:sqlite3_str_appendchar(o,1,*s);}}
}
static void html_flag(sqlite3_str*o,const char*level,const char*text){sqlite3_str_appendf(o,"<span class='flag flag-%s'>",level);html_text(o,text);sqlite3_str_appendall(o,"</span>");}
static void recent_page(int fd){
 sqlite3_str*o=sqlite3_str_new(db);
 sqlite3_str_appendall(o,UI_HEAD("Overview","dashboard")"<h1>Overview</h1><section class='today-preview'><div><canvas id='oled' width='128' height='64' aria-label='Current clock display'></canvas></div><div class='today-preview-meta'><label>Clock <select id='clockId'></select></label><span id='previewStatus' class='muted'>Loading...</span></div></section>"
  "<section><h2>Today's swipes</h2><p><a href='/punch-corrections'>All punches and corrections</a></p><table><thead><tr><th>Employee</th><th>Punch</th><th>Time (local)</th><th>Clock</th><th>Card</th></tr></thead><tbody>");
 sqlite3_stmt*q=NULL;
 const char*query="SELECT e.name,s.card_uid,coalesce(n.name,s.clock_id),s.swiped_at,coalesce(a.kind,''),CASE WHEN coalesce(a.late_minutes,0)>coalesce((SELECT grace_minutes FROM clock_late_settings WHERE clock_id=s.clock_id),5) THEN a.late_minutes ELSE 0 END,coalesce(a.exception,'') FROM swipes s LEFT JOIN employees e ON e.id=s.employee_id LEFT JOIN attendance_events a ON a.event_id=s.event_id LEFT JOIN clock_names n ON n.clock_id=s.clock_id WHERE date(s.swiped_at,'localtime')=date('now','localtime') ORDER BY s.received_at DESC LIMIT 50";
 if(sqlite3_prepare_v2(db,query,-1,&q,NULL)==SQLITE_OK){while(sqlite3_step(q)==SQLITE_ROW){
  const char*name=(const char*)sqlite3_column_text(q,0),*exc=(const char*)sqlite3_column_text(q,6);int late=sqlite3_column_int(q,5);
  int problem=!name,check=late>0||(exc&&*exc);
  sqlite3_str_appendf(o,"<tr%s><td>",problem?" class='row-problem'":check?" class='row-check'":"");
  if(name)html_text(o,name);else html_flag(o,"problem","Unknown card");
  sqlite3_str_appendall(o,"</td><td>");html_text(o,(const char*)sqlite3_column_text(q,4));
  if(late>0){char t[40];snprintf(t,sizeof t,"Late %d min",late);sqlite3_str_appendchar(o,1,' ');html_flag(o,"check",t);}
  if(exc&&!strcmp(exc,"duplicate_tap")){sqlite3_str_appendchar(o,1,' ');html_flag(o,"check","Repeat tap, not counted");}
  else if(exc&&!strcmp(exc,"out_of_order")){sqlite3_str_appendchar(o,1,' ');html_flag(o,"check","Out of order, not counted");}
  else if(exc&&*exc){sqlite3_str_appendchar(o,1,' ');html_flag(o,"check",exc);}
  sqlite3_str_appendall(o,"</td><td class='overview-time'>");html_text(o,(const char*)sqlite3_column_text(q,3));
  sqlite3_str_appendall(o,"</td><td>");html_text(o,(const char*)sqlite3_column_text(q,2));
  sqlite3_str_appendall(o,"</td><td><code>");html_text(o,(const char*)sqlite3_column_text(q,1));sqlite3_str_appendall(o,"</code></td></tr>");
 }}
 sqlite3_finalize(q);q=NULL;
 sqlite3_str_appendall(o,"</tbody></table></section><section><h2>Unknown cards</h2><p class='muted'>Cards tapped that belong to no one. Assign them under <a href='/employees'>Employees</a>.</p><table><thead><tr><th>Card</th><th>Clock</th><th>Last tapped (local)</th><th>Taps</th></tr></thead><tbody>");
 int unknown=0;
 if(sqlite3_prepare_v2(db,"SELECT u.uid,coalesce(n.name,u.clock_id),u.last_seen,u.seen_count,u.last_seen>=strftime('%Y-%m-%dT%H:%M:%fZ','now','-7 days') FROM unknown_cards u LEFT JOIN clock_names n ON n.clock_id=u.clock_id ORDER BY u.last_seen DESC LIMIT 30",-1,&q,NULL)==SQLITE_OK){while(sqlite3_step(q)==SQLITE_ROW){unknown++;
  int recent=sqlite3_column_int(q,4);
  sqlite3_str_appendf(o,"<tr%s><td><code>",recent?" class='row-check'":"");html_text(o,(const char*)sqlite3_column_text(q,0));
  sqlite3_str_appendall(o,"</code></td><td>");html_text(o,(const char*)sqlite3_column_text(q,1));
  sqlite3_str_appendall(o,"</td><td class='overview-time'>");html_text(o,(const char*)sqlite3_column_text(q,2));
  sqlite3_str_appendf(o,"</td><td>%d</td></tr>",sqlite3_column_int(q,3));}}
 sqlite3_finalize(q);
 if(!unknown)sqlite3_str_appendall(o,"<tr><td colspan='4'>None</td></tr>");
 sqlite3_str_appendall(o,"</tbody></table></section>"
 "<script>const sel=document.getElementById('clockId'),ps=document.getElementById('previewStatus');"
 "async function clocks(){try{let r=await tc.request('/api/v1/admin/adoption/list',{cache:'no-store'});let j=await tc.json(r);if(!r.ok)throw Error(j.error||'Clock list unavailable');sel.replaceChildren();for(let c of j.clocks.filter(c=>c.active)){let o=document.createElement('option');o.value=c.clock_id;o.textContent=c.name&&c.name!==c.clock_id?c.name+' ('+c.clock_id+')':c.clock_id;sel.append(o)}if(!sel.options.length){ps.replaceChildren(tc.flag('Problem','problem'),' No clocks registered.');return}refreshPreview()}catch(e){tc.err(ps,e)}}"
 "async function refresh(){let id=sel.value;if(!id)return;try{let r=await tc.request('/api/v1/preview?clock_id='+encodeURIComponent(id),{cache:'no-store'});if(!r.ok){document.getElementById('oled').parentElement.hidden=true;ps.replaceChildren(tc.flag('Offline','problem'));hasPreview=false;return}document.getElementById('oled').parentElement.hidden=false;let o=await tc.json(r);let c=document.getElementById('oled').getContext('2d'),d=c.createImageData(128,64);for(let y=0;y<64;y++)for(let x=0;x<128;x++){let z=(y>>3)*128+x,b=parseInt(o.frame.substr(z*2,2),16),on=(b>>(y&7))&1,i=(y*128+x)*4;d.data[i]=d.data[i+1]=d.data[i+2]=on?255:0;d.data[i+3]=255;}c.putImageData(d,0,0);"
 "let when=new Date(o.updated_at),age=(Date.now()-when.getTime())/1000;ps.replaceChildren();if(age>=180)ps.append(tc.flag('Offline','problem'),' ');ps.append('Updated '+tc.time(when)+(age>=180?' ('+tc.ago(age)+' ago)':''));}catch(e){tc.err(ps,e);}}"
 "document.querySelectorAll('.overview-time').forEach(el=>{el.textContent=tc.datetime(el.textContent.trim())});"
 "let previewBusy=false,hasPreview=true;async function refreshPreview(){if(document.hidden||previewBusy||!hasPreview)return;previewBusy=true;try{await refresh()}finally{previewBusy=false}}sel.onchange=()=>{hasPreview=true;refreshPreview()};clocks();setInterval(refreshPreview,4000);document.addEventListener('visibilitychange',()=>{if(!document.hidden)refreshPreview()});</script>" UI_FOOT);
 send_str(fd,"text/html; charset=utf-8",o);
}
static void manager_cards_get(int fd){
 sqlite3_stmt*q=NULL;sqlite3_str*o=sqlite3_str_new(db);
 sqlite3_str_appendall(o,"{\"cards\":[");int first=1;
 if(sqlite3_prepare_v2(db,"SELECT m.uid,e.name FROM manager_cards m JOIN employees e ON e.id=m.manager_id WHERE m.active=1 ORDER BY m.uid",-1,&q,NULL)==SQLITE_OK){
  while(sqlite3_step(q)==SQLITE_ROW){if(!first)sqlite3_str_appendall(o,",");first=0;
   sqlite3_str_appendall(o,"{\"uid\":");str_json(o,(const char*)sqlite3_column_text(q,0));sqlite3_str_appendall(o,",\"manager\":");str_json(o,(const char*)sqlite3_column_text(q,1));sqlite3_str_appendall(o,"}");
  }
 }sqlite3_finalize(q);sqlite3_str_appendall(o,"]}");send_str(fd,"application/json",o);
}
static void manager_scan_arm(int fd){
 if(!sql("BEGIN IMMEDIATE")){send_response(fd,503,"application/json","{\"error\":\"The clock is busy. Try again.\"}");return;}
 if(authorization_pending()){sql("ROLLBACK");send_response(fd,409,"application/json","{\"error\":\"The card reader is waiting for another approval. Try again in 30 seconds.\"}");return;}
 int ok=sql("UPDATE manager_scan SET armed_at=CAST(strftime('%s','now') AS INTEGER),uid='',scanned_at=0,scan_mode='enrollment' WHERE id=1");
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");
 send_response(fd,ok?200:500,"application/json",ok?"{\"status\":\"armed\"}":"{\"error\":\"storage failure\"}");
}
static void manager_scan_status(int fd){
 sqlite3_stmt*q=NULL;char out[160]="{\"status\":\"idle\"}";
 if(sqlite3_prepare_v2(db,"SELECT armed_at,uid,scanned_at FROM manager_scan WHERE id=1",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW){
  long long armed=sqlite3_column_int64(q,0),scanned=sqlite3_column_int64(q,2);
  const char*uid=(const char*)sqlite3_column_text(q,1);long long now=(long long)time(NULL);
  if(armed>0&&now-armed<=30){if(scanned>=armed&&uid&&*uid)snprintf(out,sizeof out,"{\"status\":\"scanned\",\"uid\":\"%s\"}",uid);else snprintf(out,sizeof out,"{\"status\":\"waiting\"}");}
  else if(armed>0)snprintf(out,sizeof out,"{\"status\":\"expired\"}");
 }sqlite3_finalize(q);send_response(fd,200,"application/json",out);
}
static void manager_card_assign(int fd,int who,const char*body){
 char uid[40];if(!form_value(body,"uid",uid,sizeof uid)){send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
 size_t n=strlen(uid);if(n!=8&&n!=14&&n!=20){send_response(fd,422,"application/json","{\"error\":\"Enter a card UID of 8 to 20 hexadecimal characters.\"}");return;}
 for(size_t i=0;i<n;i++){if(!isxdigit((unsigned char)uid[i])){send_response(fd,422,"application/json","{\"error\":\"Enter a card UID of 8 to 20 hexadecimal characters.\"}");return;}uid[i]=(char)toupper((unsigned char)uid[i]);}
 sqlite3_stmt*q=NULL;int scanned=0,ok=0;
 if(sqlite3_prepare_v2(db,"SELECT 1 FROM manager_scan WHERE id=1 AND uid=?1 COLLATE NOCASE AND scanned_at>=armed_at AND armed_at>0 AND CAST(strftime('%s','now') AS INTEGER)-armed_at<=30",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,uid,-1,SQLITE_TRANSIENT);scanned=sqlite3_step(q)==SQLITE_ROW;}sqlite3_finalize(q);
 if(!scanned){send_response(fd,409,"application/json","{\"error\":\"Time ran out. Try again.\"}");return;}
 /* A manager card must never be an employee attendance card. Refuse conflicting assignments. */
 if(sqlite3_prepare_v2(db,"SELECT 1 FROM cards WHERE uid=?1 COLLATE NOCASE",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,uid,-1,SQLITE_TRANSIENT);int conflict=sqlite3_step(q)==SQLITE_ROW;sqlite3_finalize(q);if(conflict){send_response(fd,409,"application/json","{\"error\":\"That card is already an attendance card. Use a different card.\"}");return;}}else{send_response(fd,500,"application/json","{\"error\":\"Could not check the card. Try again.\"}");return;}
 /* A manager UID is bound to its original owner. Never silently transfer it. */
 q=NULL;
 if(sqlite3_prepare_v2(db,"SELECT manager_id FROM manager_cards WHERE uid=?1 COLLATE NOCASE",-1,&q,NULL)!=SQLITE_OK){send_response(fd,500,"application/json","{\"error\":\"Could not check the card. Try again.\"}");return;}
 sqlite3_bind_text(q,1,uid,-1,SQLITE_TRANSIENT);int mrc=sqlite3_step(q);int existing=mrc==SQLITE_ROW?sqlite3_column_int(q,0):0;sqlite3_finalize(q);
 if(mrc!=SQLITE_ROW&&mrc!=SQLITE_DONE){send_response(fd,500,"application/json","{\"error\":\"Could not check the card. Try again.\"}");return;}
 if(mrc==SQLITE_ROW&&existing!=who){send_response(fd,409,"application/json","{\"error\":\"That card belongs to another manager. Nothing was changed.\"}");return;}
 ok=sql("BEGIN IMMEDIATE");
 if(ok){ok=sqlite3_prepare_v2(db,"INSERT INTO manager_cards(uid,manager_id,active) VALUES(?1,?2,1) ON CONFLICT(uid) DO UPDATE SET manager_id=excluded.manager_id,active=1",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,uid,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,2,who);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);}
 if(ok)ok=sql("UPDATE manager_scan SET armed_at=0,uid='',scanned_at=0 WHERE id=1");
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");
 send_response(fd,ok?200:500,"application/json",ok?"{\"status\":\"registered\"}":"{\"error\":\"The clock could not complete this. Try again.\"}");
}
static void manager_cards_page(int fd){
 send_response(fd,200,"text/html; charset=utf-8",
 UI_HEAD("Manager cards","manager-cards")"<h1>Manager cards</h1><p id='mcbanner' role='alert' hidden></p><section><h2>Register a card to your account</h2><p>A manager card approves protected changes at the clock. It never records attendance.</p><div class='actions'><button id='arm'>Scan a card (30 seconds)</button></div><p id='status' role='status'></p><div class='actions'><button id='save' disabled>Register this card</button></div></section><section><h2>Registered cards</h2><div id='cards'>Loading</div></section><script>const arm=document.getElementById('arm'),status=document.getElementById('status'),save=document.getElementById('save'),cards=document.getElementById('cards');let seen='',scanActive=false;const api=tc.api;async function refresh(){let j=await api('/api/v1/admin/manager-cards');cards.replaceChildren();for(let c of j.cards){let p=document.createElement('p');p.textContent=c.uid+' ('+c.manager+')';cards.append(p)}if(!j.cards.length){cards.replaceChildren(tc.flag('Problem','problem'),' None registered.');}tc.banner(document.getElementById('mcbanner'),'problem','Problem',j.cards.length?'':'No manager card is registered. Protected changes cannot be approved until you register one.')}arm.onclick=async()=>{try{await api('/api/v1/admin/manager-scan/arm',{method:'POST'});status.textContent='Tap the card on the clock.';scanActive=true;seen='';arm.textContent='Scan a card (30 seconds)';save.disabled=true}catch(e){tc.err(status,e)}};save.onclick=async()=>{try{await api('/api/v1/admin/manager-cards',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({uid:seen})});tc.ok(status,'Manager card registered.');save.disabled=true;await refresh();window.location.assign('/')}catch(e){tc.err(status,e)}};async function poll(){if(!scanActive||document.hidden)return;try{let j=await api('/api/v1/admin/manager-scan/status');if(j.status==='scanned'&&j.uid!==seen){seen=j.uid;scanActive=false;arm.textContent=seen;status.textContent='Card read. Select Register this card.';save.disabled=false}else if(j.status==='expired'&& !seen){scanActive=false;tc.err(status,tc.TIMEOUT)}}catch(e){tc.err(status,e)}}refresh().catch(e=>tc.err(status,e));setInterval(poll,700)</script>"UI_FOOT);
}
#include "schedule.inc"
#include "send_schedule.h"
#include "payroll.inc"
static void send_cards(int fd){
 sqlite3_stmt*s=NULL;
 if(sqlite3_prepare_v2(db,"SELECT upper(c.uid), CASE WHEN trim(e.first_name)<>'' THEN trim(e.first_name) ELSE substr(trim(e.name),1,instr(trim(e.name)||' ',' ')-1) END, e.attendance_mode FROM cards c JOIN employees e ON e.id=c.employee_id WHERE c.active=1 AND e.active=1 AND NOT EXISTS(SELECT 1 FROM manager_cards m WHERE m.uid=c.uid AND m.active=1) ORDER BY c.uid",-1,&s,NULL)!=SQLITE_OK){send_response(fd,500,"text/plain","database");return;}
 sqlite3_str *o=sqlite3_str_new(db);
 while(sqlite3_step(s)==SQLITE_ROW){
  const char*uid=(const char*)sqlite3_column_text(s,0);const char *name=(const char*)sqlite3_column_text(s,1);
  char clean[65];size_t j=0;
  if(name)for(size_t i=0;name[i]&&j<sizeof(clean)-1;i++){unsigned char ch=(unsigned char)name[i];if(ch=='\t'||ch=='\n'||ch=='\r')break;if(ch>=32&&ch<127)clean[j++]=(char)ch;}
  clean[j]=0;
  if(uid)sqlite3_str_appendf(o,"%s\t%s\t%s\n",uid,clean,sqlite3_column_text(s,2));
 }
 sqlite3_finalize(s);
 s=NULL;
 if(sqlite3_prepare_v2(db,"SELECT upper(uid) FROM manager_cards WHERE active=1 ORDER BY uid",-1,&s,NULL)==SQLITE_OK){
  while(sqlite3_step(s)==SQLITE_ROW){const char*uid=(const char*)sqlite3_column_text(s,0);if(uid)sqlite3_str_appendf(o,"%s\t__MANAGER__\n",uid);}
 }sqlite3_finalize(s);
 send_str(fd,"text/plain; charset=ascii",o);
}
static void preview_write(int fd,const char *body){
 char clock[60],frame[OLED_BYTES*2+1];
 if(!json_string(body,"clock_id",clock,sizeof clock)||!valid_id(clock,48)||
    !json_string(body,"frame",frame,sizeof frame)||strlen(frame)!=OLED_BYTES*2){
  send_response(fd,400,"application/json","{\"error\":\"invalid preview\"}");return;
 }
 for(size_t i=0;frame[i];i++)if(!isxdigit((unsigned char)frame[i])){
  send_response(fd,400,"application/json","{\"error\":\"invalid frame\"}");return;
 }
 sqlite3_stmt*q=NULL;
 int ok=sqlite3_prepare_v2(db,"INSERT INTO oled_previews(clock_id,frame) VALUES(?1,?2) ON CONFLICT(clock_id) DO UPDATE SET frame=excluded.frame,updated_at=strftime('%Y-%m-%dT%H:%M:%fZ','now')",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,clock,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,frame,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}
 sqlite3_finalize(q);send_response(fd,ok?200:500,"application/json",ok?"{\"status\":\"ok\"}":"{\"error\":\"storage failure\"}");
}
static void preview_read(int fd,const char *path){
 const char*id=strstr(path,"?clock_id=");if(!id||!valid_id(id+10,48)){
  send_response(fd,400,"application/json","{\"error\":\"clock_id required\"}");return;
 }
 sqlite3_stmt*q=NULL;char body[2400];int found=0;
 if(sqlite3_prepare_v2(db,"SELECT frame,updated_at FROM oled_previews WHERE clock_id=?1",-1,&q,NULL)==SQLITE_OK){
  sqlite3_bind_text(q,1,id+10,-1,SQLITE_TRANSIENT);
  if(sqlite3_step(q)==SQLITE_ROW){
   snprintf(body,sizeof body,"{\"frame\":\"%s\",\"updated_at\":\"%s\"}",sqlite3_column_text(q,0),sqlite3_column_text(q,1));found=1;
  }
 }sqlite3_finalize(q);
 send_response(fd,found?200:404,"application/json",found?body:"{\"error\":\"no preview\"}");
}
/* Render stored swipe templates at enqueue time so later edits do not alter pending mail. */
/* {{swipe_time}} and {{swipe_date}} are rendered in the BPI's local time zone
   (/etc/localtime); the stored swipe stays in UTC. Falls back to the raw stamp. */
static void local_stamp(const char *iso,char *timebuf,size_t tcap,char *datebuf,size_t dcap){
 snprintf(timebuf,tcap,"%s",iso);snprintf(datebuf,dcap,"%s",iso);
 sqlite3_stmt *q=NULL;long long epoch=-1;
 if(sqlite3_prepare_v2(db,"SELECT CAST(strftime('%s',?1) AS INTEGER)",-1,&q,NULL)==SQLITE_OK){
  sqlite3_bind_text(q,1,iso,-1,SQLITE_TRANSIENT);
  if(sqlite3_step(q)==SQLITE_ROW&&sqlite3_column_type(q,0)!=SQLITE_NULL)epoch=sqlite3_column_int64(q,0);
 }sqlite3_finalize(q);
 if(epoch<0)return;
 time_t t=(time_t)epoch;struct tm tm;if(!localtime_r(&t,&tm))return;
 strftime(timebuf,tcap,"%I:%M %p %Z",&tm);
 strftime(datebuf,dcap,"%A, %B %d, %Y",&tm);
}
static char *replace_tokens(const char *input,const char *name,const char *clock,const char *clock_name,const char *swipe_time,const char *swipe_date){
 const char *keys[]={"{{employee_name}}","{{clock_id}}","{{clock_name}}","{{swipe_time}}","{{swipe_date}}","{{sync_status}}"};
 const char *vals[]={name,clock,clock_name,swipe_time,swipe_date,"Recorded"};
 size_t cap=32768,n=0;char *out=malloc(cap);if(!out)return NULL;
 for(const char *p=input;*p;){int matched=0;
  for(size_t i=0;i<6;i++){size_t k=strlen(keys[i]);if(!strncmp(p,keys[i],k)){
    size_t v=strlen(vals[i]);if(n+v>=cap){free(out);return NULL;}memcpy(out+n,vals[i],v);n+=v;p+=k;matched=1;break;}}
  if(!matched){if(n+1>=cap){free(out);return NULL;}out[n++]=*p++;}}
 out[n]=0;return out;
}
static int enqueue_swipe_email(int employee_id,const char *event,const char *clock,const char *stamp){
 sqlite3_stmt *q=NULL;const char *name="",*email="";char *name_copy=NULL,*email_copy=NULL;
 if(sqlite3_prepare_v2(db,"SELECT name,COALESCE(email,'') FROM employees WHERE id=?1 AND active=1",-1,&q,NULL)!=SQLITE_OK)return 0;
 sqlite3_bind_int(q,1,employee_id);
 if(sqlite3_step(q)==SQLITE_ROW){name=(const char*)sqlite3_column_text(q,0);email=(const char*)sqlite3_column_text(q,1);name_copy=strdup(name?name:"");email_copy=strdup(email?email:"");}
 sqlite3_finalize(q);
 if(!name_copy||!email_copy){free(name_copy);free(email_copy);return 1;}
 if(!strchr(email_copy,'@')||strchr(email_copy,'\r')||strchr(email_copy,'\n')){free(name_copy);free(email_copy);return 1;}
 const char *subject=NULL,*body=NULL;char *sub_copy=NULL,*body_copy=NULL;
 if(sqlite3_prepare_v2(db,"SELECT subject,body FROM email_templates WHERE id='swipe_employee' AND enabled=1",-1,&q,NULL)!=SQLITE_OK){free(name_copy);free(email_copy);return 0;}
 if(sqlite3_step(q)==SQLITE_ROW){subject=(const char*)sqlite3_column_text(q,0);body=(const char*)sqlite3_column_text(q,1);sub_copy=strdup(subject?subject:"");body_copy=strdup(body?body:"");}
 sqlite3_finalize(q);
 if(!sub_copy||!body_copy){free(name_copy);free(email_copy);free(sub_copy);free(body_copy);return 1;}
 char when[64],day[64];local_stamp(stamp,when,sizeof when,day,sizeof day);
 char clock_name[64];snprintf(clock_name,sizeof clock_name,"%s",clock);
 if(sqlite3_prepare_v2(db,"SELECT name FROM clock_names WHERE clock_id=?1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,clock,-1,SQLITE_TRANSIENT);
  if(sqlite3_step(q)==SQLITE_ROW&&sqlite3_column_text(q,0))snprintf(clock_name,sizeof clock_name,"%s",(const char*)sqlite3_column_text(q,0));}
 sqlite3_finalize(q);q=NULL;
 char *rendered_subject=replace_tokens(sub_copy,name_copy,clock,clock_name,when,day);
 char *rendered_body=replace_tokens(body_copy,name_copy,clock,clock_name,when,day);
 int ok=rendered_subject&&rendered_body;
 if(ok&&sqlite3_prepare_v2(db,"INSERT OR IGNORE INTO email_queue(recipient,subject,body,status,next_attempt,event_id) VALUES(?1,?2,?3,'pending',strftime('%Y-%m-%dT%H:%M:%fZ','now'),?4)",-1,&q,NULL)==SQLITE_OK){
  sqlite3_bind_text(q,1,email_copy,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,rendered_subject,-1,SQLITE_TRANSIENT);
  sqlite3_bind_text(q,3,rendered_body,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,4,event,-1,SQLITE_TRANSIENT);
  ok=sqlite3_step(q)==SQLITE_DONE;
 }else ok=0;
 sqlite3_finalize(q);free(name_copy);free(email_copy);free(sub_copy);free(body_copy);free(rendered_subject);free(rendered_body);return ok;
}
#include "email_render.h"
static int note_link_create(const char*event,int employee,char*url,size_t cap);
#include "attendance.inc"
static void clock_grace_get(int fd){
 sqlite3_stmt*q=NULL;sqlite3_str*j=sqlite3_str_new(db);sqlite3_str_appendall(j,"{\"clocks\":[");int n=0;
 if(sqlite3_prepare_v2(db,"SELECT clock_id,grace_minutes FROM clock_late_settings ORDER BY clock_id",-1,&q,NULL)==SQLITE_OK){
  while(sqlite3_step(q)==SQLITE_ROW){const char*id=(const char*)sqlite3_column_text(q,0);if(n++)sqlite3_str_appendchar(j,1,',');sqlite3_str_appendall(j,"{\"clock_id\":");str_json(j,id);sqlite3_str_appendf(j,",\"grace_minutes\":%d}",sqlite3_column_int(q,1));}}
 sqlite3_finalize(q);sqlite3_str_appendall(j,"]}");char*out=sqlite3_str_finish(j);if(out){send_response(fd,200,"application/json",out);sqlite3_free(out);}else send_response(fd,500,"application/json","{\"error\":\"The clock could not complete this. Try again.\"}");
}
static void clock_grace_save(int fd,const char*body){
 char id[60],value[12];if(!form_value(body,"clock_id",id,sizeof id)||!valid_id(id,48)||!form_value(body,"grace_minutes",value,sizeof value)){
  send_response(fd,400,"application/json","{\"error\":\"Choose a clock and enter whole minutes.\"}");return;}
 char*end=NULL;long minutes=strtol(value,&end,10);if(end==value||*end||minutes<0||minutes>60){send_response(fd,400,"application/json","{\"error\":\"Late grace must be 0 to 60 minutes.\"}");return;}
 sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(db,"INSERT INTO clock_late_settings(clock_id,grace_minutes) VALUES(?1,?2) ON CONFLICT(clock_id) DO UPDATE SET grace_minutes=excluded.grace_minutes",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,id,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,2,(int)minutes);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 send_response(fd,ok?200:500,"application/json",ok?"{\"status\":\"saved\"}":"{\"error\":\"The clock could not complete this. Try again.\"}");
}
static void clock_duplicate_get(int fd){
 sqlite3_stmt*q=NULL;sqlite3_str*j=sqlite3_str_new(db);sqlite3_str_appendall(j,"{\"clocks\":[");int n=0;
 if(sqlite3_prepare_v2(db,"SELECT clock_id,duplicate_seconds FROM clock_duplicate_settings ORDER BY clock_id",-1,&q,NULL)==SQLITE_OK){while(sqlite3_step(q)==SQLITE_ROW){if(n++)sqlite3_str_appendchar(j,1,',');sqlite3_str_appendall(j,"{\"clock_id\":");str_json(j,(const char*)sqlite3_column_text(q,0));sqlite3_str_appendf(j,",\"duplicate_seconds\":%d}",sqlite3_column_int(q,1));}}
 sqlite3_finalize(q);sqlite3_str_appendall(j,"]}");char*r=sqlite3_str_finish(j);if(r){send_response(fd,200,"application/json",r);sqlite3_free(r);}else send_response(fd,500,"application/json","{\"error\":\"The clock could not complete this. Try again.\"}");
}
static void clock_duplicate_save(int fd,const char*body){
 char id[60],value[16];if(!form_value(body,"clock_id",id,sizeof id)||!valid_id(id,48)||!form_value(body,"duplicate_seconds",value,sizeof value)){send_response(fd,400,"application/json","{\"error\":\"Choose a clock and enter whole seconds.\"}");return;}
 char*end=NULL;long sec=strtol(value,&end,10);if(end==value||*end||sec<0||sec>300){send_response(fd,400,"application/json","{\"error\":\"Repeat window must be 0 to 300 seconds.\"}");return;}
 sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(db,"INSERT INTO clock_duplicate_settings(clock_id,duplicate_seconds) VALUES(?1,?2) ON CONFLICT(clock_id) DO UPDATE SET duplicate_seconds=excluded.duplicate_seconds",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,id,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,2,(int)sec);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);send_response(fd,ok?200:500,"application/json",ok?"{\"status\":\"saved\"}":"{\"error\":\"The clock could not complete this. Try again.\"}");
}
static void clock_grace_page(int fd){
 send_response(fd,200,"text/html; charset=utf-8",UI_HEAD("Late grace and repeat taps","clock-grace")
 "<h1>Late grace and repeat taps</h1><p>Settings apply per clock. Changes do not affect punches already recorded.</p>"
 "<section><h2>Late grace</h2><p class='muted'>Minutes after the scheduled start before an arrival counts as late and a notice is sent. With 5 minutes, 08:05:00 is on time and 08:05:01 is late. The late minutes recorded are always exact.</p><form id='graceForm'><label>Clock<select name='clock_id' class='clockpick' required></select></label><label>Late after (minutes, 0 to 60)<input name='grace_minutes' type='number' min='0' max='60' step='1' value='5' required></label><div class='actions'><button>Save late grace</button></div></form><p id='result' role='status'></p></section>"
 "<section><h2>Repeat taps</h2><p class='muted'>A second tap within this many seconds is logged but not counted as a punch.</p><form id='dupForm'><label>Clock<select name='clock_id' class='clockpick' required></select></label><label>Repeat window (seconds, 0 to 300)<input name='duplicate_seconds' type='number' min='0' max='300' value='120' required></label><div class='actions'><button>Save repeat window</button></div></form><p id='dupStatus' role='status'></p></section>"
 "<section><h2>Current settings</h2><table><thead><tr><th>Clock</th><th>Late after</th><th>Repeat window</th></tr></thead><tbody id='rows'></tbody></table></section>"
 "<script>const f=document.getElementById('graceForm'),df=document.getElementById('dupForm'),rows=document.getElementById('rows'),status=document.getElementById('result'),ds=document.getElementById('dupStatus');"
 "const get=tc.api;"
 "async function load(){let [c,g,d]=await Promise.all([get('/api/v1/admin/adoption/list'),get('/api/v1/admin/clock-grace'),get('/api/v1/admin/clock-duplicate')]);"
 "let grace=new Map(g.clocks.map(x=>[x.clock_id,x.grace_minutes])),dup=new Map(d.clocks.map(x=>[x.clock_id,x.duplicate_seconds]));"
 "let list=c.clocks.filter(x=>x.active);for(let sel of document.querySelectorAll('.clockpick')){let keep=sel.value;sel.replaceChildren();for(let x of list){let o=document.createElement('option');o.value=x.clock_id;o.textContent=x.name&&x.name!==x.clock_id?x.name+' ('+x.clock_id+')':x.clock_id;sel.append(o)}if(keep)sel.value=keep}"
 "rows.replaceChildren();for(let x of list){let tr=document.createElement('tr');for(let v of [x.name&&x.name!==x.clock_id?x.name+' ('+x.clock_id+')':x.clock_id,grace.has(x.clock_id)?grace.get(x.clock_id)+' min':'5 min (default)',dup.has(x.clock_id)?dup.get(x.clock_id)+' s':'120 s (default)']){let td=document.createElement('td');td.textContent=v;tr.append(td)}rows.append(tr)}"
 "for(let sel of document.querySelectorAll('.clockpick')){let sync=()=>{let target=sel.closest('form'),id=sel.value,input=target.querySelector('input[type=number]');if(input)input.value=target===f?(grace.get(id)??5):(dup.get(id)??120)};sel.onchange=sync;sync()}"
 "if(!list.length){let tr=document.createElement('tr'),td=document.createElement('td');td.colSpan=3;td.append(tc.flag('Problem','problem'),' No clocks registered.');tr.append(td);rows.append(tr)}}"
 "async function save(form,url,out){out.textContent='Saving...';try{await tc.api(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(new FormData(form))});tc.ok(out,'Saved.');await load()}catch(e){tc.err(out,e)}}"
 "f.onsubmit=e=>{e.preventDefault();save(f,'/api/v1/admin/clock-grace',status)};df.onsubmit=e=>{e.preventDefault();save(df,'/api/v1/admin/clock-duplicate',ds)};load().catch(e=>tc.err(status,e))</script>" UI_FOOT);
}
static void accept_swipe(int fd,const char *body){
 char event[90],clock[60],card[80],at[64];
 if(!json_string(body,"event_id",event,sizeof event)||!json_string(body,"clock_id",clock,sizeof clock)||!json_string(body,"card_uid",card,sizeof card)||!json_string(body,"swiped_at",at,sizeof at)||!valid_id(event,80)||!valid_id(clock,48)||!valid_id(card,64)||!valid_id(at,40)){send_response(fd,400,"application/json","{\"error\":\"invalid event\"}");return;}
 if(!clock_durability(1)){send_response(fd,503,"application/json","{\"error\":\"durability unavailable\"}");return;}
 if(!sql("BEGIN IMMEDIATE")){send_response(fd,500,"application/json","{\"error\":\"The clock is busy. Try again.\"}");clock_durability(0);return;}
 sqlite3_stmt *s=NULL;
 /* Match the mode in force when the clock recorded this punch, not when an offline queue delivered it. */
 const char *query="INSERT OR IGNORE INTO swipes(event_id,clock_id,card_uid,employee_id,swiped_at) VALUES(?1,?2,?3,(SELECT h.employee_id FROM card_ownership_history h WHERE h.id=(SELECT h2.id FROM card_ownership_history h2 WHERE h2.uid=?3 COLLATE NOCASE AND julianday(h2.effective_at)<=julianday(?4) ORDER BY julianday(h2.effective_at) DESC,h2.id DESC LIMIT 1) AND h.active=1 AND (SELECT eh.active FROM employee_active_history eh WHERE eh.employee_id=h.employee_id AND julianday(eh.effective_at)<=julianday(?4) ORDER BY julianday(eh.effective_at) DESC,eh.id DESC LIMIT 1)=1 AND (SELECT m.mode FROM employee_attendance_modes m WHERE m.employee_id=h.employee_id AND julianday(m.effective_at)<=julianday(?4) ORDER BY julianday(m.effective_at) DESC,m.rowid DESC LIMIT 1)='punch_required' ORDER BY julianday(h.effective_at) DESC,h.id DESC LIMIT 1),?4)";
 int ok=sqlite3_prepare_v2(db,query,-1,&s,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(s,1,event,-1,SQLITE_TRANSIENT);sqlite3_bind_text(s,2,clock,-1,SQLITE_TRANSIENT);sqlite3_bind_text(s,3,card,-1,SQLITE_TRANSIENT);sqlite3_bind_text(s,4,at,-1,SQLITE_TRANSIENT);ok=sqlite3_step(s)==SQLITE_DONE;}
 sqlite3_finalize(s);
 int inserted=ok?sqlite3_changes(db):0,employee_id=0;
 if(ok&&inserted){
  if(sqlite3_prepare_v2(db,"SELECT employee_id FROM swipes WHERE event_id=?1",-1,&s,NULL)==SQLITE_OK){
   sqlite3_bind_text(s,1,event,-1,SQLITE_TRANSIENT);
   if(sqlite3_step(s)==SQLITE_ROW&&sqlite3_column_type(s,0)!=SQLITE_NULL)employee_id=sqlite3_column_int(s,0);
  }else ok=0;
  sqlite3_finalize(s);
  if(ok&&employee_id>0&&!attendance_process(event,employee_id,at))fprintf(stderr,"attendance classification failed for swipe %s; raw swipe preserved\n",event);
  /* Notification failure must never cost an attendance record: isolate it. */
  if(ok&&employee_id>0&&sql("SAVEPOINT swipe_email")){
   int late_notice_queued=0;sqlite3_stmt *nq=NULL;
   if(sqlite3_prepare_v2(db,"SELECT 1 FROM email_queue WHERE event_id=?1",-1,&nq,NULL)==SQLITE_OK){char latekey[160];snprintf(latekey,sizeof latekey,"late:%s:employee",event);sqlite3_bind_text(nq,1,latekey,-1,SQLITE_TRANSIENT);late_notice_queued=sqlite3_step(nq)==SQLITE_ROW;}sqlite3_finalize(nq);
   if(late_notice_queued||enqueue_swipe_email(employee_id,event,clock,at))sql("RELEASE swipe_email");
   else{fprintf(stderr,"swipe %s stored; email notification not queued\n",event);sql("ROLLBACK TO swipe_email");sql("RELEASE swipe_email");}
  }
 }
 if(ok){ok=sql("COMMIT");if(!ok)sql("ROLLBACK");}else sql("ROLLBACK");
 if(!clock_durability(0))fprintf(stderr,"cannot restore normal synchronous mode after swipe\n");
 if(ok){
  char classification[16]="ERROR";
  sqlite3_stmt *result=NULL;
  if(sqlite3_prepare_v2(db,"SELECT a.kind,a.late_minutes,a.exception,coalesce((SELECT grace_minutes FROM clock_late_settings WHERE clock_id=s.clock_id),5) FROM attendance_events a JOIN swipes s ON s.event_id=a.event_id WHERE a.event_id=?1",-1,&result,NULL)==SQLITE_OK){
   sqlite3_bind_text(result,1,event,-1,SQLITE_TRANSIENT);
   if(sqlite3_step(result)==SQLITE_ROW){
    const char *kind=(const char*)sqlite3_column_text(result,0),*exception=(const char*)sqlite3_column_text(result,2);
    if(exception&&!strcmp(exception,"duplicate_tap"))snprintf(classification,sizeof classification,"DUPLICATE");
    else if(!exception||!*exception){
     if(kind&&!strcmp(kind,"OUT"))snprintf(classification,sizeof classification,"OUT");
     else if(kind&&!strcmp(kind,"IN"))snprintf(classification,sizeof classification,"%s",sqlite3_column_int(result,1)>sqlite3_column_int(result,3)?"IN_LATE":"IN");
    }
   }
  }
  sqlite3_finalize(result);
  char response[128];snprintf(response,sizeof response,"{\"status\":\"accepted\",\"classification\":\"%s\"}",classification);
  send_response(fd,200,"application/json",response);
 }else send_response(fd,500,"application/json","{\"error\":\"storage failure\"}");
}
static void accept_unknown(int fd,const char *body){
 char uid[80],clock[60];
 if(!json_string(body,"card_uid",uid,sizeof uid)||!json_string(body,"clock_id",clock,sizeof clock)||!valid_id(uid,64)||!valid_id(clock,48)){
     send_response(fd,400,"application/json","{\"error\":\"invalid unknown-card event\"}");return;
 }
 if(strlen(uid)!=8 && strlen(uid)!=14 && strlen(uid)!=20){send_response(fd,422,"application/json","{\"error\":\"invalid uid length\"}");return;}
 for(size_t i=0;uid[i];i++){if(!isxdigit((unsigned char)uid[i])){send_response(fd,422,"application/json","{\"error\":\"uid not hexadecimal\"}");return;}uid[i]=(char)toupper((unsigned char)uid[i]);}
 if(!clock_durability(1)){send_response(fd,503,"application/json","{\"error\":\"durability unavailable\"}");return;}
 sqlite3_stmt *q=NULL;
 int ok=sqlite3_prepare_v2(db,"INSERT INTO unknown_cards(uid,clock_id,first_seen,last_seen) VALUES(?1,?2,strftime('%Y-%m-%dT%H:%M:%fZ','now'),strftime('%Y-%m-%dT%H:%M:%fZ','now')) ON CONFLICT(uid) DO UPDATE SET clock_id=excluded.clock_id,last_seen=excluded.last_seen,seen_count=seen_count+1",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,uid,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,clock,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 if(!clock_durability(0))fprintf(stderr,"cannot restore normal synchronous mode after unknown card\n");
 send_response(fd,ok?200:500,"application/json",ok?"{\"status\":\"seen\"}":"{\"error\":\"storage failure\"}");
}
/* Whole-request deadline (not per recv): a slow or idle client can hold this
   single-threaded server for at most REQUEST_DEADLINE_MS, below the agent's 3 s timeout. */
#define REQUEST_DEADLINE_MS 1500
#define FONT_MAX (512u*1024u)
static long long now_ms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (long long)t.tv_sec*1000+t.tv_nsec/1000000;}
static int read_request(int fd,char *buf,size_t cap){
 size_t used=0;long long deadline=now_ms()+REQUEST_DEADLINE_MS;
 for(;;){
  if(used+1>=cap)return -1;
  long long left=deadline-now_ms();if(left<=0)return -1;
  struct pollfd p={.fd=fd,.events=POLLIN};int r=poll(&p,1,(int)left);
  if(r<0&&errno==EINTR)continue;
  if(r<=0)return -1;
  ssize_t n=recv(fd,buf+used,cap-used-1,0);if(n<=0)return -1;
  used+=(size_t)n;buf[used]=0;
  char *sep=strstr(buf,"\r\n\r\n");
  if(sep){size_t head=(size_t)(sep+4-buf);size_t need=0;const char *p2=buf;
   while(p2<sep){if(!strncasecmp(p2,"Content-Length:",15)){need=(size_t)strtoul(p2+15,NULL,10);break;}const char *e=strstr(p2,"\r\n");if(!e)break;p2=e+2;}
   if(strstr(buf,"POST /api/v1/admin/backups/upload?name=")){
    if(need==0||need>64u*1024u*1024u)return -1;
    return (int)used; /* Return at headers: child streams the body. */
   }
   if(need>(strstr(buf," /api/v1/admin/holidays ")?200000u:(strstr(buf," /api/v1/admin/tls/upload ")?24000u:4096u)))return -1;
   if(used>=head+need)return (int)(head+need);}
 }
}

/* Bounded chunked downloads: never treat a partial send as success. */
static int op_send_all(int fd,const char*p,size_t n,long long deadline){
 while(n){
  long long remaining=deadline-now_ms();if(remaining<=0)return 0;
  struct pollfd f={.fd=fd,.events=POLLOUT};
  int ready=poll(&f,1,(int)(remaining>1000?1000:remaining));
  if(ready<0&&errno==EINTR)continue;
  if(ready<=0)continue;
  ssize_t wrote=send(fd,p,n>16384?16384:n,MSG_DONTWAIT);
  if(wrote<0&&errno==EINTR)continue;
  if(wrote<=0)return 0;
  p+=wrote;n-=(size_t)wrote;
 }return 1;
}
static void op_export_send(int fd,const char*type,const char*filename,sqlite3_str*o){
 int err=sqlite3_str_errcode(o);char*body=sqlite3_str_finish(o);
 if(err!=SQLITE_OK){sqlite3_free(body);send_response(fd,500,"application/json","{\"error\":\"The clock could not complete this. Try again.\"}");return;}
 size_t len=body?strlen(body):0;
 int csv_bom=strncmp(type,"text/csv",8)==0;
 char header[700];int k=snprintf(header,sizeof header,"HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Disposition: attachment; filename=\"%s\"\r\nContent-Length: %zu\r\nConnection: close\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nX-Content-Type-Options: nosniff\r\n\r\n",type,filename,len+(csv_bom?3:0));
 long long deadline=now_ms()+60000;
 if(k>0&&(size_t)k<sizeof header&&op_send_all(fd,header,(size_t)k,deadline)){
  if(csv_bom){static const unsigned char bom[]={0xEF,0xBB,0xBF};if(!op_send_all(fd,(const char*)bom,sizeof bom,deadline)){sqlite3_free(body);return;}}
  if(body)op_send_all(fd,body,len,deadline);
 }
 sqlite3_free(body);
}
#include "admin_ops.inc"
static volatile sig_atomic_t lite_export_client_fd=-1;
static void lite_export_timeout(int sig){
 (void)sig;
 static const char reply[]="HTTP/1.1 504 Gateway Timeout\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: 22\r\nConnection: close\r\n\r\nExcel export timed out";
 int fd=(int)lite_export_client_fd;
 if(fd>=0){ssize_t written=write(fd,reply,sizeof(reply)-1);(void)written;}
 /* Signal the converter group without killing ourselves until after reply. */
 signal(SIGALRM,SIG_IGN);
 kill(-getpgrp(),SIGKILL);
 _exit(124);
}
#include "reconciliation.inc"
static const char*note_link_base(char*,size_t);
#include "payroll_dates.h"
#include "account_review.inc"
#include "time_off.inc"
#include "issues.inc"

/* First-manager setup is deliberately unavailable once any manager account exists.
   Physical RFID presentation binds bootstrap to a card held at the clock. */
static int bootstrap_available(void){
 sqlite3_stmt*q=NULL;int count=1;
 if(sqlite3_prepare_v2(db,"SELECT count(*) FROM user_accounts u JOIN employee_roles r ON r.employee_id=u.employee_id AND r.role='manager'",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW)count=sqlite3_column_int(q,0);
 sqlite3_finalize(q);return count==0;
}
static void bootstrap_page(int fd){
 if(!bootstrap_available()){send_redirect(fd,"/login");return;}
 send_response(fd,200,"text/html; charset=utf-8",UI_HEAD_LOGIN("Create first manager")
 "<h1>Create first manager</h1><p>Set up the first manager account. You need an unused RFID card and access to the clock.</p>"
 "<form id='setup'><label>First name<input name='first_name' maxlength='60' required></label><label>Last name<input name='last_name' maxlength='60' required></label><label>Position<input name='position' maxlength='120' required></label><label>Email<input name='email' type='email' maxlength='200' required></label><label>Password (12+ characters)<input name='password' type='password' minlength='12' autocomplete='new-password' required></label><label>Confirm password<input id='confirm' type='password' minlength='12' autocomplete='new-password' required></label><div class='actions'><button>Create first manager</button></div></form><p id='status' role='status'></p>"
 "<script>let setup=document.getElementById('setup'),st=document.getElementById('status');setup.onsubmit=async e=>{e.preventDefault();if(setup.password.value!==document.getElementById('confirm').value){tc.err(st,'Passwords do not match.');return}let btn=setup.querySelector('button');btn.disabled=true;try{let r=await tc.request('/api/v1/bootstrap/arm',{method:'POST',body:new URLSearchParams(new FormData(setup))});let j=await tc.json(r);if(!r.ok)throw Error(j.error||'Setup refused');setup.password.value='';document.getElementById('confirm').value='';st.textContent='Tap the new manager card on the clock within 30 seconds.';async function poll(){let r=await tc.request('/api/v1/bootstrap/status',{method:'POST',body:new URLSearchParams({token:j.token})});let x=await tc.json(r);if(!r.ok)throw Error(x.error||'Setup failed');if(x.status==='waiting'){setTimeout(()=>poll().catch(fail),700);return}if(x.status==='created'){location.assign('/login');return}throw Error(x.status==='expired'?tc.TIMEOUT:(x.error||tc.TIMEOUT))}function fail(e){tc.err(st,e);btn.disabled=false}setTimeout(()=>poll().catch(fail),700)}catch(e){tc.err(st,e);btn.disabled=false}};</script>"UI_FOOT);
}
static void bootstrap_arm(int fd,const char*body){
 if(!bootstrap_available()){send_response(fd,403,"application/json","{\"error\":\"A manager account already exists. Sign in instead.\"}");return;}
 
 char name[160]="",first[64]="",last[64]="",position[128]="",email[220]="",password[180]="",hash[256]="",token[65]="";
 if(!form_value(body,"first_name",first,sizeof first)||!form_value(body,"last_name",last,sizeof last)||!form_value(body,"position",position,sizeof position)||!form_value(body,"email",email,sizeof email)||!form_value(body,"password",password,sizeof password)||
 !*first||!*last||!*position||!*email||!strchr(email,'@')||strpbrk(email,"\r\n")||strpbrk(first,"\r\n\t")||strpbrk(last,"\r\n\t")||!ar_hash_password(password,hash)||!random_hex(token,32)){
 send_response(fd,422,"application/json","{\"error\":\"Fill in every field. Passwords need at least 12 characters.\"}");return;
 }
 memset(password,0,sizeof password);
 /* Temporary bootstrap record only: separate names without schema migration. */
 snprintf(name,sizeof name,"%s\t%s",first,last);
 int ok=sql("BEGIN IMMEDIATE");sqlite3_stmt*q=NULL;
 if(ok)ok=sqlite3_prepare_v2(db,"INSERT INTO first_manager_setup(id,name,email,position,password_hash,request_token,armed_at) VALUES(1,?1,?2,?3,?4,?5,CAST(strftime('%s','now') AS INTEGER)) ON CONFLICT(id) DO UPDATE SET name=excluded.name,email=excluded.email,position=excluded.position,password_hash=excluded.password_hash,request_token=excluded.request_token,armed_at=excluded.armed_at",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,name,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,email,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,position,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,4,hash,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,5,token,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 if(ok)ok=sql("UPDATE manager_scan SET armed_at=CAST(strftime('%s','now') AS INTEGER),uid='',scanned_at=0,request_token='',scan_mode='bootstrap' WHERE id=1");
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");
 if(!ok){send_response(fd,500,"application/json","{\"error\":\"The card reader is busy. Try again.\"}");return;}
 char out[120];snprintf(out,sizeof out,"{\"token\":\"%s\"}",token);send_response(fd,200,"application/json",out);
}
static void bootstrap_status(int fd,const char*body){
 
 char token[70]="";if(!form_value(body,"token",token,sizeof token)||strlen(token)!=64){send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
 sqlite3_stmt*q=NULL;char name[160]="",email[220]="",position[128]="",hash[256]="",uid[64]="";sqlite3_int64 armed=0,scanned=0;int exists=0;
 if(sqlite3_prepare_v2(db,"SELECT b.name,b.email,b.position,b.password_hash,b.armed_at,s.uid,s.scanned_at FROM first_manager_setup b JOIN manager_scan s ON s.id=1 WHERE b.id=1 AND b.request_token=?1 AND s.scan_mode='bootstrap' AND s.armed_at=b.armed_at",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,token,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW){exists=1;snprintf(name,sizeof name,"%s",ar_text(q,0));snprintf(email,sizeof email,"%s",ar_text(q,1));snprintf(position,sizeof position,"%s",ar_text(q,2));snprintf(hash,sizeof hash,"%s",ar_text(q,3));armed=sqlite3_column_int64(q,4);snprintf(uid,sizeof uid,"%s",ar_text(q,5));scanned=sqlite3_column_int64(q,6);}}sqlite3_finalize(q);
 if(!exists){send_response(fd,409,"application/json","{\"error\":\"Time ran out. Try again.\"}");return;}
 if(time(NULL)-armed>30){sql("DELETE FROM first_manager_setup WHERE id=1");sql("UPDATE manager_scan SET armed_at=0,uid='',scanned_at=0,scan_mode='enrollment' WHERE id=1 AND scan_mode='bootstrap'");send_response(fd,200,"application/json","{\"status\":\"expired\"}");return;}
 if(!*uid||scanned<armed){send_response(fd,200,"application/json","{\"status\":\"waiting\"}");return;}
 if(!bootstrap_available()||!valid_id(uid,48)){send_response(fd,409,"application/json","{\"error\":\"Setup is no longer available. Sign in instead.\"}");return;}
 int ok=sql("BEGIN IMMEDIATE");int employee=0;
 if(ok)ok=bootstrap_available();
 /* A bootstrap record is transient and stores first and last separated by a tab. */
 char *sep=strchr(name,'\t');
 if(!sep||sep==name||!sep[1])ok=0;
 char display_name[160]="";
 if(ok){*sep++=0;snprintf(display_name,sizeof display_name,"%s %s",name,sep);}
 if(ok)ok=sqlite3_prepare_v2(db,"INSERT INTO employees(name,email,position,active,first_name,last_name,created_at) VALUES(?1,?2,?3,1,?4,?5,strftime('%Y-%m-%dT%H:%M:%fZ','now'))",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,display_name,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,email,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,position,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,4,name,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,5,sep,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;if(ok)employee=(int)sqlite3_last_insert_rowid(db);}sqlite3_finalize(q);
 if(ok)ok=sqlite3_prepare_v2(db,"INSERT INTO employee_roles(employee_id,role) VALUES(?1,'manager')",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,employee);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 if(ok)ok=sqlite3_prepare_v2(db,"INSERT INTO user_accounts(employee_id,password_hash,disabled) VALUES(?1,?2,0)",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,employee);sqlite3_bind_text(q,2,hash,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 if(ok)ok=sqlite3_prepare_v2(db,"INSERT INTO manager_cards(uid,manager_id,active) VALUES(?1,?2,1)",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,uid,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,2,employee);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 if(ok)ok=sql("DELETE FROM first_manager_setup WHERE id=1")&&sql("UPDATE manager_scan SET armed_at=0,uid='',scanned_at=0,scan_mode='enrollment' WHERE id=1");
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");
 send_response(fd,ok?200:409,"application/json",ok?"{\"status\":\"created\"}":"{\"error\":\"Could not create manager. Use a card not assigned to another employee.\"}");
}

#include "note_link.inc"

/* Calendar is informational and affects only late-arrival notifications. */
static int holiday_date_valid(const char *v){
 if(strlen(v)<11||v[10]!=','||v[4]!='-'||v[7]!='-')return 0;
 for(int i=0;i<10;i++)if(i!=4&&i!=7&&!isdigit((unsigned char)v[i]))return 0;
 int y=0,m=0,d=0;sscanf(v,"%4d-%2d-%2d",&y,&m,&d);
 if(y<2000||y>2100||m<1||m>12||d<1||d>31)return 0;
 struct tm t={0};t.tm_year=y-1900;t.tm_mon=m-1;t.tm_mday=d;t.tm_isdst=-1;
 mktime(&t);return t.tm_year==y-1900&&t.tm_mon==m-1&&t.tm_mday==d;
}
static void holidays_get(int fd){
 sqlite3_str*o=sqlite3_str_new(db);sqlite3_str_appendall(o,"{\"skip_alerts\":");
 sqlite3_stmt*q=NULL;int skip=0;
 if(sqlite3_prepare_v2(db,"SELECT skip_alerts FROM holiday_settings WHERE id=1",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW)skip=sqlite3_column_int(q,0);
 sqlite3_finalize(q);sqlite3_str_appendall(o,skip?"true,\"days\":[":"false,\"days\":[");
 int first=1;
 if(sqlite3_prepare_v2(db,"SELECT day,name FROM statutory_holidays ORDER BY day",-1,&q,NULL)==SQLITE_OK){
  while(sqlite3_step(q)==SQLITE_ROW){if(!first)sqlite3_str_appendall(o,",");first=0;
   sqlite3_str_appendall(o,"{\"date\":");str_json(o,(const char*)sqlite3_column_text(q,0));
   sqlite3_str_appendall(o,",\"name\":");str_json(o,(const char*)sqlite3_column_text(q,1));sqlite3_str_appendall(o,"}");}}
 sqlite3_finalize(q);sqlite3_str_appendall(o,"]}");send_str(fd,"application/json",o);
}
static void holidays_save(int fd,const char*body){
 char csv[160001]="",skip[8]="";
 if(!form_value_multiline(body,"calendar",csv,sizeof csv)||!form_value(body,"skip",skip,sizeof skip)||(strcmp(skip,"0")&&strcmp(skip,"1"))){
  send_response(fd,400,"application/json","{\"error\":\"Check each holiday has a valid date and a name.\"}");return;}
 /* Validate all rows before replacing the old calendar. Expected YYYY-MM-DD,Name. */
 char *copy=strdup(csv);if(!copy){send_response(fd,500,"application/json","{\"error\":\"memory\"}");return;}
 size_t count=0;int valid=1;
 for(char*line=strtok(copy,"\n");line;line=strtok(NULL,"\n")){
  size_t n=strlen(line);if(n&&line[n-1]=='\r')line[--n]=0;
  if(!n)continue;
  if(n<12||line[10]!=','||!holiday_date_valid(line)||!line[11]||n>150){valid=0;break;}
  for(size_t i=11;i<n;i++)if((unsigned char)line[i]<32){valid=0;break;}
  if(++count>1000){valid=0;break;}
 }free(copy);
 if(!valid){send_response(fd,422,"application/json","{\"error\":\"Check each holiday has a valid date and a name.\"}");return;}
 if(!sql("BEGIN IMMEDIATE")){send_response(fd,503,"application/json","{\"error\":\"The clock is busy. Try again.\"}");return;}
 int ok=sql("DELETE FROM statutory_holidays");sqlite3_stmt*q=NULL;
 if(ok)ok=sqlite3_prepare_v2(db,"INSERT INTO statutory_holidays(day,name) VALUES(?1,?2)",-1,&q,NULL)==SQLITE_OK;
 if(ok){char *cursor=csv;while(*cursor){char*next=strchr(cursor,'\n');if(next)*next=0;
  size_t n=strlen(cursor);if(n&&cursor[n-1]=='\r')cursor[--n]=0;
  if(n){char date[11];memcpy(date,cursor,10);date[10]=0;sqlite3_reset(q);sqlite3_clear_bindings(q);
   sqlite3_bind_text(q,1,date,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,cursor+11,-1,SQLITE_TRANSIENT);
   if(sqlite3_step(q)!=SQLITE_DONE){ok=0;break;}}
  if(!next)break;
  cursor=next+1;}}
 sqlite3_finalize(q);
 if(ok){sqlite3_stmt*u=NULL;ok=sqlite3_prepare_v2(db,"UPDATE holiday_settings SET skip_alerts=?1 WHERE id=1",-1,&u,NULL)==SQLITE_OK;
  if(ok){sqlite3_bind_int(u,1,atoi(skip));ok=sqlite3_step(u)==SQLITE_DONE;}sqlite3_finalize(u);}
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");
 send_response(fd,ok?200:409,"application/json",ok?"{\"status\":\"saved\"}":"{\"error\":\"Not saved. Check for repeated dates.\"}");
}
static void holidays_page(int fd){
 send_response(fd,200,"text/html; charset=utf-8",UI_HEAD("Stat holidays","holidays")
 "<h1>Stat holidays</h1><p>Manage holidays by date and name. Holidays do not change recorded hours.</p>"
 "<section><form id='f'><label><input type='checkbox' id='skip'> No late-arrival emails on these dates</label>"
 "<h2>Holiday dates</h2><div class='table-wrap'><table><thead><tr><th>Date</th><th>Holiday name</th><th>Action</th></tr></thead><tbody id='holidayrows'></tbody></table></div>"
 "<div class='actions'><button type='button' id='addholiday'>Add holiday</button><button type='submit'>Save holidays</button></div>"
 "<label for='holidaycsv'>Import holidays from CSV (Date,Holiday)</label><input id='holidaycsv' type='file' accept='.csv,text/csv'><p id='holidaypreview' role='status'></p>"
 "<p id='holidaymessage' role='status'></p></form></section>"
 "<script>const form=document.getElementById('f'),rows=document.getElementById('holidayrows'),message=document.getElementById('holidaymessage'),skip=document.getElementById('skip');"
 "function addRow(date='',name=''){const empty=rows.querySelector('[data-empty]');if(empty)empty.remove();const tr=document.createElement('tr'),a=document.createElement('td'),b=document.createElement('td'),c=document.createElement('td');"
 "const d=document.createElement('input');d.type='date';d.required=true;d.value=date;d.setAttribute('aria-label','Holiday date');"
 "const n=document.createElement('input');n.type='text';n.required=true;n.maxLength=139;n.value=name;n.placeholder='Holiday name';n.setAttribute('aria-label','Holiday name');"
 "const del=document.createElement('button');del.type='button';del.textContent='Remove';del.onclick=()=>{tr.remove();if(!rows.children.length){let e=document.createElement('tr'),v=document.createElement('td');v.colSpan=3;v.textContent='No holidays';e.dataset.empty='1';e.append(v);rows.append(e)}};a.append(d);b.append(n);c.append(del);tr.append(a,b,c);rows.append(tr)}"
 "async function load(){try{const j=await tc.api('/api/v1/admin/holidays');"
 "skip.checked=!!j.skip_alerts;rows.replaceChildren();for(const h of j.days)addRow(h.date,h.name);if(!j.days.length){let tr=document.createElement('tr'),td=document.createElement('td');td.colSpan=3;td.textContent='No holidays';tr.dataset.empty='1';tr.append(td);rows.append(tr)}}catch(e){tc.err(message,e)}}"
 "document.getElementById('holidaycsv').onchange=async e=>{const f=e.target.files[0];if(!f)return;const msg=document.getElementById('holidaypreview');try{const text=(await f.text()).replace(/^\\uFEFF/,''),lines=text.split(/\\r?\\n/),existing=new Map();for(const tr of rows.children){if(tr.dataset.empty)continue;let x=tr.querySelectorAll('input');existing.set(x[0].value,x[1].value.trim())}let added=0,skipped=0,conflicts=[],toAdd=[];for(let i=0;i<lines.length;i++){let v=lines[i].trim();if(!v)continue;if(i===0&&/^Date,Holiday$/i.test(v))continue;let m=/^(\\d{4}-\\d{2}-\\d{2}),([^,]+)$/.exec(v);if(!m||!/^\\d{4}-\\d{2}-\\d{2}$/.test(m[1])||!m[2].trim()||Number.isNaN(Date.parse(m[1]+'T12:00:00Z'))||new Date(m[1]+'T12:00:00Z').toISOString().slice(0,10)!==m[1])throw Error('Invalid CSV row '+(i+1)+'. Use Date,Holiday with YYYY-MM-DD dates.');let date=m[1],name=m[2].trim();if(existing.has(date)){if(existing.get(date)===name)skipped++;else conflicts.push(date);continue}existing.set(date,name);toAdd.push([date,name]);added++}for(const [d,n] of toAdd)addRow(d,n);msg.textContent=added+' new, '+skipped+' already present, '+conflicts.length+' conflicts. ' + (conflicts.length?'Conflicting dates unchanged: '+conflicts.join(', ')+'. ':'')+'Review the rows and click Save holidays to commit.'}catch(err){tc.err(msg,err)}finally{e.target.value=''}};"
 "document.getElementById('addholiday').onclick=()=>addRow();"
 "form.onsubmit=async e=>{e.preventDefault();const days=[],seen=new Set();for(const tr of rows.children){if(tr.dataset.empty)continue;const [d,n]=tr.querySelectorAll('input');"
 "const date=d.value,name=n.value.trim();if(!date||!name){tc.err(message,'Enter both a date and a holiday name.');return}"
 "if(/[\\r\\n]/.test(name)){tc.err(message,'Holiday names must be on one line.');return}"
 "if(seen.has(date)){tc.err(message,'Each holiday date can appear only once.');return}seen.add(date);days.push(date+','+name)}"
 "try{await tc.api('/api/v1/admin/holidays',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},"
 "body:new URLSearchParams({skip:skip.checked?'1':'0',calendar:days.join('\\n')})}),j=await tc.json(r);if(!r.ok)throw Error(j.error);tc.ok(message,'Holidays saved.')}catch(e){tc.err(message,e)}};load();</script>"UI_FOOT);
}
/* A system action only becomes executable after an active manager card was scanned. */
static void health_page(int fd){
 send_response(fd,200,"text/html; charset=utf-8",UI_HEAD("System health","health")
 "<h1>System health</h1><p>Updates every 30 seconds.</p><p id='healthbanner' role='alert' hidden></p>"
 "<section><h2>Device</h2><table class='diag-table'><tbody id='healthrows'></tbody></table><p class='after-table' role='status' id='healthstatus'>Loading...</p><div class='actions'><button type='button' id='healthrefresh'>Refresh now</button><a href='/api/v1/admin/diagnostics/report' download='timeclock-diagnostics.json'>Download diagnostics</a></div></section>"
 "<section><h2>Related pages</h2><p><a href='/system'>System operations</a> | <a href='/backups'>Backup and restore</a> | <a href='/clocks'>Clock management</a> | <a href='/diagnostics'>Diagnostics</a></p></section>"
 "<script>const fields=[['version','Version'],['hostname','Hostname'],['uptime_seconds','Uptime'],['ntp_indicator','Time sync'],['temperature_millicelsius','CPU temperature'],['storage_available_bytes','Free storage'],['raw_swipes','Swipes stored'],['queued_email','Email waiting to send'],['failed_email','Failed emails'],['wifi_connected','Wi-Fi'],['wifi_ssid','Wi-Fi network'],['wifi_signal_dbm','Wi-Fi signal'],['wifi_signal_bars','Wi-Fi strength'],['wifi_link_mbps','Wi-Fi link rate']];"
 "function value(k,v){if(v===null||v===undefined||v==='')return 'Unavailable';if(k==='wifi_connected')return v?'Connected':'Disconnected';if(['raw_swipes','classified_swipes','rejected_uploads','queued_email','failed_email'].includes(k)&&Number(v)<0)return 'Unavailable';if(k==='uptime_seconds'){let n=Number(v);if(!Number.isFinite(n)||n<0)return 'Unavailable';return Math.floor(n/86400)+'d '+Math.floor(n%86400/3600)+'h '+Math.floor(n%3600/60)+'m'}if(k==='temperature_millicelsius'){let n=Number(v);return Number.isFinite(n)?(n/1000).toFixed(1)+' °C':'Unavailable'}if(k==='storage_available_bytes'){let n=Number(v);return Number.isFinite(n)?(n/1073741824).toFixed(2)+' GiB':'Unavailable'}if(k==='wifi_signal_dbm')return Number(v)===0?'Unavailable':v+' dBm';if(k==='wifi_signal_bars')return v+'/4';if(k==='wifi_link_mbps')return Number(v)>0?v+' Mbps':'Unavailable';return String(v)}"
 "let active=false;async function update(){if(active||document.hidden)return;active=true;const controller=new AbortController(),t=setTimeout(()=>controller.abort(),7000);try{let r=await tc.request('/api/v1/admin/diagnostics/report',{cache:'no-store',signal:controller.signal});if(r.status===401||r.status===403){location.href='/login';return}if(!r.ok)throw Error('Readings unavailable ('+r.status+')');let d=await tc.json(r),root=document.getElementById('healthrows'),problems=0,checks=0;root.replaceChildren();for(let [key,label] of fields){let tr=document.createElement('tr'),th=document.createElement('th'),td=document.createElement('td'),f=tc.health(key,d[key]);th.textContent=label;td.textContent=value(key,d[key]);if(f){td.append(' ',tc.flag(f[1],f[0]));tr.className='row-'+f[0];f[0]==='problem'?problems++:checks++}tr.append(th,td);root.append(tr)}"
 "let b=document.getElementById('healthbanner');if(problems)tc.banner(b,'problem','Problem',problems+(problems===1?' reading needs':' readings need')+' action. Flagged below.');else tc.banner(b,'check','Check',checks?checks+(checks===1?' reading needs':' readings need')+' a look. Flagged below.':'');"
 "document.getElementById('healthstatus').textContent='Updated '+tc.time(new Date())}catch(e){tc.err(document.getElementById('healthstatus'),e)}finally{clearTimeout(t);active=false}}document.getElementById('healthrefresh').onclick=update;document.addEventListener('visibilitychange',()=>{if(!document.hidden)update()});update();setInterval(update,30000);</script>" UI_FOOT);
}
static int backup_name(const char*n){
 size_t l=strlen(n);if(l<27||l>115||strncmp(n,"timeclock-",10)||strcmp(n+l-7,".tar.gz"))return 0;
 for(const char*p=n;*p;p++)if(!((*p>='0'&&*p<='9')||(*p>='A'&&*p<='Z')||(*p>='a'&&*p<='z')||*p=='-'||*p=='.'))return 0;
 return 1;
}
static void backups_list(int fd){
 DIR*d=opendir("/var/backups/timeclock");if(!d){send_response(fd,503,"application/json","{\"error\":\"The backup service is not available on this clock.\"}");return;}
 char b[18000];size_t pos=0;pos+=(size_t)snprintf(b,sizeof b,"{\"files\":[");struct dirent*e;int count=0;
 while((e=readdir(d))&&count<80){if(!backup_name(e->d_name))continue;
 char full[280];snprintf(full,sizeof full,"/var/backups/timeclock/%s",e->d_name);
 struct stat st;if(lstat(full,&st)||!S_ISREG(st.st_mode))continue;
 char one[340];int n=snprintf(one,sizeof one,"%s{\"name\":\"%s\",\"size\":%lld,\"mtime\":%lld}",count?",":"",e->d_name,(long long)st.st_size,(long long)st.st_mtime);
 if(n<0||pos+(size_t)n+4>=sizeof b)break;
 memcpy(b+pos,one,(size_t)n);pos+=(size_t)n;count++;
 }closedir(d);snprintf(b+pos,sizeof b-pos,"]}");send_response(fd,200,"application/json",b);
}
static void backup_settings_get(int fd){
 char b[9000];size_t pos=0;int enabled=0,selected=0;sqlite3_stmt*q=NULL;
 if(sqlite3_prepare_v2(db,"SELECT email_enabled,manager_id FROM backup_settings WHERE id=1",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW){enabled=sqlite3_column_int(q,0);selected=sqlite3_column_int(q,1);}sqlite3_finalize(q);
 pos+=(size_t)snprintf(b,sizeof b,"{\"enabled\":%s,\"manager_id\":%d,\"managers\":[",enabled?"true":"false",selected);
 if(sqlite3_prepare_v2(db,"SELECT e.id,e.name,e.email FROM employees e JOIN employee_roles r ON r.employee_id=e.id AND r.role='manager' JOIN user_accounts u ON u.employee_id=e.id WHERE e.active=1 AND u.disabled=0 AND e.email IS NOT NULL AND e.email<>'' ORDER BY e.name",-1,&q,NULL)==SQLITE_OK){
 int count=0;while(sqlite3_step(q)==SQLITE_ROW&&count<50){const char*name=(const char*)sqlite3_column_text(q,1);const char*email=(const char*)sqlite3_column_text(q,2);char nj[450],ej[450];diag_escape(name?name:"",nj,sizeof nj);diag_escape(email?email:"",ej,sizeof ej);int n=snprintf(b+pos,sizeof b-pos,"%s{\"id\":%d,\"name\":\"%s\",\"email\":\"%s\"}",count?",":"",sqlite3_column_int(q,0),nj,ej);if(n<0||pos+(size_t)n+4>=sizeof b)break;pos+=(size_t)n;count++;}
 }sqlite3_finalize(q);snprintf(b+pos,sizeof b-pos,"]}");send_response(fd,200,"application/json",b);
}
static void backup_settings_save(int fd,const char*body){
 char id[16]="",enable[5]="";if(!form_value(body,"manager_id",id,sizeof id)||!form_value(body,"enabled",enable,sizeof enable)){send_response(fd,400,"application/json","{\"error\":\"Fill in all required fields.\"}");return;}
 for(const char*c=id;*c;c++)if(!isdigit((unsigned char)*c)){send_response(fd,422,"application/json","{\"error\":\"Choose a manager.\"}");return;}
 int mid=atoi(id),on=!strcmp(enable,"1");if(strcmp(enable,"0")&&strcmp(enable,"1")){send_response(fd,422,"application/json","{\"error\":\"Check the entries and try again.\"}");return;}
 sqlite3_stmt*q=NULL;int valid=!on;
 if(on&&sqlite3_prepare_v2(db,"SELECT 1 FROM employees e JOIN employee_roles r ON r.employee_id=e.id AND r.role='manager' JOIN user_accounts u ON u.employee_id=e.id WHERE e.id=?1 AND e.active=1 AND u.disabled=0 AND e.email LIKE '%@%'",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,mid);valid=sqlite3_step(q)==SQLITE_ROW;}sqlite3_finalize(q);
 if(!valid){send_response(fd,422,"application/json","{\"error\":\"Choose an active manager with an email address\"}");return;}
 if(on){int smtp_ok=0;sqlite3_stmt*check=NULL;if(sqlite3_prepare_v2(db,"SELECT 1 FROM smtp_settings WHERE id=1 AND url<>'' AND sender<>'' AND tls_mode IN ('none','starttls','implicit')",-1,&check,NULL)==SQLITE_OK)smtp_ok=sqlite3_step(check)==SQLITE_ROW;sqlite3_finalize(check);if(!smtp_ok){send_response(fd,422,"application/json","{\"error\":\"Set up Outgoing email first.\"}");return;}}
 int ok=sqlite3_prepare_v2(db,"UPDATE backup_settings SET email_enabled=?1,manager_id=?2 WHERE id=1",-1,&q,NULL)==SQLITE_OK;if(ok){sqlite3_bind_int(q,1,on);sqlite3_bind_int(q,2,mid);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 send_response(fd,ok?200:500,"application/json",ok?"{\"status\":\"saved\"}":"{\"error\":\"Not saved. Try again.\"}");
}
static void backup_download_arm(int fd,int who,const char*body){
 char filename[128]="";if(!form_value(body,"filename",filename,sizeof filename)||!backup_name(filename)){send_response(fd,422,"application/json","{\"error\":\"Choose a timeclock backup file.\"}");return;}
 char path[300];snprintf(path,sizeof path,"/var/backups/timeclock/%s",filename);struct stat st;
 if(lstat(path,&st)||!S_ISREG(st.st_mode)){send_response(fd,404,"application/json","{\"error\":\"Backup not found\"}");return;}
 if(!sql("BEGIN IMMEDIATE")){send_response(fd,503,"application/json","{\"error\":\"The clock is busy. Try again.\"}");return;}
 int ok=!authorization_pending();sqlite3_stmt*q=NULL;
 if(ok)ok=sqlite3_prepare_v2(db,"INSERT INTO system_actions(id,requested_by,action,armed_at) VALUES(1,?1,'download',CAST(strftime('%s','now') AS INTEGER)) ON CONFLICT(id) DO UPDATE SET requested_by=excluded.requested_by,action='download',armed_at=excluded.armed_at",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,who);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 if(ok)ok=sqlite3_prepare_v2(db,"INSERT INTO backup_download(id,requested_by,filename,token,approved_at) VALUES(1,?1,?2,'',0) ON CONFLICT(id) DO UPDATE SET requested_by=excluded.requested_by,filename=excluded.filename,token='',approved_at=0",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,who);sqlite3_bind_text(q,2,filename,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 char token[33]="";if(ok)ok=sql("UPDATE manager_scan SET armed_at=CAST(strftime('%s','now') AS INTEGER),uid='',scanned_at=0,scan_mode='approval' WHERE id=1")&&approval_token_new(token);
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");char b[110];snprintf(b,sizeof b,"{\"status\":\"waiting\",\"token\":\"%s\"}",token);
 send_response(fd,ok?200:409,"application/json",ok?b:"{\"error\":\"The card reader is waiting for another approval. Try again in 30 seconds.\"}");
}
static void backup_restore_arm(int fd,int who,const char*body){
 char filename[128]="",confirm[16]="";
 if(!form_value(body,"filename",filename,sizeof filename)||!backup_name(filename)||
    !form_value(body,"confirm",confirm,sizeof confirm)||strcmp(confirm,"RESTORE")){
  send_response(fd,422,"application/json","{\"error\":\"Select a backup and type RESTORE.\"}");return;
 }
 char path[300];snprintf(path,sizeof path,"/var/backups/timeclock/%s",filename);struct stat st;
 if(lstat(path,&st)||!S_ISREG(st.st_mode)){
  send_response(fd,404,"application/json","{\"error\":\"Backup not found\"}");return;
 }
 if(!sql("BEGIN IMMEDIATE")){send_response(fd,503,"application/json","{\"error\":\"The clock is busy. Try again.\"}");return;}
 int ok=!authorization_pending();sqlite3_stmt*q=NULL;
 if(ok)ok=sqlite3_prepare_v2(db,"INSERT INTO system_actions(id,requested_by,action,armed_at) VALUES(1,?1,'restore',CAST(strftime('%s','now') AS INTEGER)) ON CONFLICT(id) DO UPDATE SET requested_by=excluded.requested_by,action='restore',armed_at=excluded.armed_at",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,who);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 if(ok)ok=sqlite3_prepare_v2(db,"INSERT INTO backup_restore(id,requested_by,filename) VALUES(1,?1,?2) ON CONFLICT(id) DO UPDATE SET requested_by=excluded.requested_by,filename=excluded.filename",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,who);sqlite3_bind_text(q,2,filename,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 char token[33]="";if(ok)ok=sql("UPDATE manager_scan SET armed_at=CAST(strftime('%s','now') AS INTEGER),uid='',scanned_at=0,scan_mode='approval' WHERE id=1")&&approval_token_new(token);
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");
 char result[120];snprintf(result,sizeof result,"{\"status\":\"waiting\",\"token\":\"%s\"}",token);
 send_response(fd,ok?200:409,"application/json",ok?result:"{\"error\":\"The card reader is waiting for another approval. Try again in 30 seconds.\"}");
}
static int download_parent_listener=-1;
static void backup_restore_result(int fd){
 int file=open("/var/backups/timeclock/.restore-result.json",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
 if(file<0){send_response(fd,200,"application/json","{\"state\":\"none\",\"message\":\"No restore yet\"}");return;}
 struct stat st;char b[1025];ssize_t n=read(file,b,1024);int safe=!fstat(file,&st)&&S_ISREG(st.st_mode)&&st.st_size>0&&st.st_size<=1024&&n==st.st_size;
 close(file);if(!safe){send_response(fd,503,"application/json","{\"error\":\"The clock could not complete this. Try again.\"}");return;}
 b[n]=0;send_response(fd,200,"application/json",b);
}
static void backup_download_file(int fd,int who,const char*token){
 if(strlen(token)!=64||strspn(token,"0123456789abcdef")!=64){send_response(fd,403,"application/json","{\"error\":\"This approval is no longer valid. Start again.\"}");return;}
 sqlite3_stmt*q=NULL;char filename[128]="";int owner=0;long long at=0;
 if(sqlite3_prepare_v2(db,"SELECT requested_by,filename,approved_at FROM backup_download WHERE id=1 AND token=?1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,token,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW){owner=sqlite3_column_int(q,0);snprintf(filename,sizeof filename,"%s",(const char*)sqlite3_column_text(q,1));at=sqlite3_column_int64(q,2);}}sqlite3_finalize(q);
 if(owner!=who||at<=0||(long long)time(NULL)-at>60||!backup_name(filename)){send_response(fd,403,"application/json","{\"error\":\"Time ran out. Try again.\"}");return;}
 char path[300];snprintf(path,sizeof path,"/var/backups/timeclock/%s",filename);int input=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);struct stat st;
 if(input<0||fstat(input,&st)||!S_ISREG(st.st_mode)||st.st_size<=0){if(input>=0)close(input);send_response(fd,404,"application/json","{\"error\":\"The backup service is not available on this clock.\"}");return;}
 if(!sql("BEGIN IMMEDIATE")){close(input);send_response(fd,503,"application/json","{\"error\":\"The clock is busy. Try again.\"}");return;}
 int ok=sqlite3_prepare_v2(db,"DELETE FROM backup_download WHERE id=1 AND token=?1 AND requested_by=?2",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,token,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,2,who);ok=sqlite3_step(q)==SQLITE_DONE&&sqlite3_changes(db)==1;}sqlite3_finalize(q);
 if(ok)ok=sqlite3_prepare_v2(db,"INSERT INTO backup_download_audit(manager_id,filename) VALUES(?1,?2)",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,who);sqlite3_bind_text(q,2,filename,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");if(!ok){close(input);send_response(fd,409,"application/json","{\"error\":\"This approval is no longer valid. Start again.\"}");return;}
 /* The main single-request server must never wait on a slow download. */
 pid_t child=fork();
 if(child<0){close(input);send_response(fd,503,"application/json","{\"error\":\"The backup service is not available on this clock.\"}");return;}
 if(child>0){close(input);return;}
 if(download_parent_listener>=0)close(download_parent_listener);
 /* Do not access inherited SQLite state in the child. _exit() skips cleanup. */
 char header[520];int n=snprintf(header,sizeof header,"HTTP/1.1 200 OK\r\nContent-Type: application/gzip\r\nContent-Disposition: attachment; filename=\"%s\"\r\nContent-Length: %lld\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n",filename,(long long)st.st_size);
 if(n>0&&(size_t)n<sizeof header){size_t off=0;while(off<(size_t)n){ssize_t sent=send(fd,header+off,(size_t)n-off,0);if(sent<=0)break;off+=(size_t)sent;}
 off_t file_offset=0;
 while(off==(size_t)n&&file_offset<st.st_size){
  size_t remaining=(size_t)((st.st_size-file_offset)>1048576?1048576:(st.st_size-file_offset));
  ssize_t sent=sendfile(fd,input,&file_offset,remaining);
  if(sent>0)continue;
  if(sent<0&&errno==EINTR)continue;
  break; /* Disconnected or stalled client: no effect on the parent. */
 }}
 close(input);
 close(fd);
 _exit(0);
}
/* Backup upload only: child receives a bounded raw octet stream. No SQLite in child. */
#define BACKUP_UPLOAD_MAX (64u*1024u*1024u)
#define BACKUP_INBOX "/var/lib/timeclock/backup-inbox"
static void backup_upload_file(int fd,const char*req,size_t received,const char*name){
 if(!backup_name(name)){send_response(fd,422,"application/json","{\"error\":\"Choose a timeclock backup file.\"}");return;}
 const char*sep=strstr(req,"\r\n\r\n");if(!sep){send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
 size_t offset=(size_t)(sep+4-req),length=0;int have_length=0,octet=0;
 const char*line=req;
 while(line<sep){const char*end=strstr(line,"\r\n");if(!end||end>sep)break;
  if((size_t)(end-line)>15&&!strncasecmp(line,"Content-Length:",15)){
   const char*q=line+15;while(q<end&&(*q==' '||*q=='\t'))q++;
   if(q==end){send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
   size_t value=0;for(;q<end;q++){if(*q<'0'||*q>'9'||value>BACKUP_UPLOAD_MAX){send_response(fd,413,"application/json","{\"error\":\"The backup is larger than 64 MiB.\"}");return;}value=value*10u+(size_t)(*q-'0');}
   length=value;have_length=1;
  }
  if((size_t)(end-line)>=25&&!strncasecmp(line,"Content-Type:",13)&&strstr(line,"application/octet-stream")==line+14)octet=1;
  line=end+2;
 }
 if(!have_length||!length||length>BACKUP_UPLOAD_MAX||!octet||received<offset){send_response(fd,400,"application/json","{\"error\":\"Choose a timeclock backup (.tar.gz, up to 64 MiB).\"}");return;}
 if(access(BACKUP_INBOX "/.ready",F_OK)==0||access(BACKUP_INBOX "/.uploading",F_OK)==0){send_response(fd,409,"application/json","{\"error\":\"Another upload is in progress. Wait for it to finish.\"}");return;}
 pid_t child=fork();if(child<0){send_response(fd,503,"application/json","{\"error\":\"The backup service is not available on this clock.\"}");return;}
 if(child){return;} /* Parent returns to accept. */
 if(download_parent_listener>=0)close(download_parent_listener);
 sqlite3_close(db);db=NULL;
 int out=open(BACKUP_INBOX "/.uploading",O_CREAT|O_EXCL|O_WRONLY|O_NOFOLLOW|O_CLOEXEC,0600);
 if(out<0){send_response(fd,409,"application/json","{\"error\":\"Another upload is in progress. Wait for it to finish.\"}");close(fd);_exit(1);}
 int good=1;size_t copied=received-offset;if(copied>length)copied=length;
 if(copied&&(write(out,req+offset,copied)!=(ssize_t)copied))good=0;
 long long deadline=now_ms()+120000;
 char block[32768];
 while(good&&copied<length){long long left=deadline-now_ms();if(left<=0){good=0;break;}
  struct pollfd pp={.fd=fd,.events=POLLIN};int pr=poll(&pp,1,(int)(left>2000?2000:left));if(pr<0&&errno==EINTR)continue;if(pr<=0){if(pr<0)good=0;continue;}
  size_t want=length-copied;if(want>sizeof block)want=sizeof block;
  ssize_t n=recv(fd,block,want,0);if(n<=0){good=0;break;}
  size_t off=0;while(off<(size_t)n){ssize_t w=write(out,block+off,(size_t)n-off);if(w<=0){good=0;break;}off+=(size_t)w;}
  copied+=(size_t)n;
 }
 if(good&&fsync(out))good=0;
 close(out);
 if(good){
  int meta=open(BACKUP_INBOX "/.name.tmp",O_CREAT|O_EXCL|O_WRONLY|O_NOFOLLOW,0600);
  if(meta<0||write(meta,name,strlen(name))!=(ssize_t)strlen(name)||write(meta,"\n",1)!=1||fsync(meta))good=0;
  if(meta>=0)close(meta);
  if(good){int st=open(BACKUP_INBOX "/.status.upload",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0640);if(st<0)good=0;else{const char*msg="{\"state\":\"validating\",\"message\":\"Upload received; verification pending\"}";if(write(st,msg,strlen(msg))!=(ssize_t)strlen(msg)||fsync(st))good=0;close(st);if(good&&rename(BACKUP_INBOX "/.status.upload",BACKUP_INBOX "/status.json"))good=0;}}
  if(good&&(rename(BACKUP_INBOX "/.name.tmp",BACKUP_INBOX "/.name")||rename(BACKUP_INBOX "/.uploading",BACKUP_INBOX "/.ready")))good=0;
 }
 if(!good){unlink(BACKUP_INBOX "/.uploading");unlink(BACKUP_INBOX "/.name.tmp");unlink(BACKUP_INBOX "/.status.upload");send_response(fd,500,"application/json","{\"error\":\"Upload incomplete. Try again.\"}");}
 else send_response(fd,202,"application/json","{\"status\":\"uploaded\",\"message\":\"Archive received; validation in progress\"}");
 close(fd);_exit(good?0:1);
}
static void backup_upload_status(int fd){
 int f=open(BACKUP_INBOX "/status.json",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(f<0){send_response(fd,200,"application/json","{\"state\":\"idle\"}");return;}
 struct stat st;char b[1025];ssize_t n=read(f,b,1024);int ok=!fstat(f,&st)&&S_ISREG(st.st_mode)&&st.st_size>0&&st.st_size<=1024&&n==st.st_size;close(f);
 if(!ok){send_response(fd,503,"application/json","{\"error\":\"The clock could not complete this. Try again.\"}");return;}b[n]=0;send_response(fd,200,"application/json",b);
}
static void backups_page(int fd){
 send_response(fd,200,"text/html; charset=utf-8",UI_HEAD("Backup and restore","backups")
 "<h1>Backup and restore</h1><p>This clock backs itself up daily at 02:15. Each clock keeps its own backups.</p><p id='bkbanner' role='alert' hidden></p><p id='rsbanner' role='alert' hidden></p>"
 "<section><h2>Backups on this clock</h2><p class='muted'>Downloading needs a manager card tap. The approval allows one download within 60 seconds.</p><div id='files'>Loading...</div><p id='downloadstatus' role='status'></p><div class='actions'><a href='/system?backup=1'>Back up now</a></div></section>"
 "<section><h2>Email the daily backup</h2><p class='muted'>The backup holds employee and card records. Send it only to a trusted mailbox. Backups over 15 MiB are not emailed.</p><label><input type='checkbox' id='enabled'> Email the daily backup</label><label>Send to<select id='recipient'></select></label><div class='actions'><button id='save'>Save</button></div><p id='configstatus' role='status'></p></section>"
 "<section><h2>Upload a backup</h2><p class='muted'>Adds a downloaded backup (.tar.gz, up to 64 MiB) to this clock. Uploading does not restore it.</p><label for='uploadfile'>Backup file</label><input id='uploadfile' type='file' accept='.gz,.tar.gz' aria-label='Backup file'><div class='actions'><button id='uploadstart' type='button'>Upload backup</button></div><progress id='uploadprogress' max='100' value='0' hidden></progress><p id='uploadstatus' role='status'></p></section>"
 "<section><h2>Restore</h2><p>Replaces all attendance, employees and settings on this clock with the backup. Anything recorded after the backup is lost. Restore stops if this clock has swipes waiting to send.</p><p id='lastrestore' role='status'>Checking...</p><label>Backup<select id='restorefile'></select></label><label>Type RESTORE to confirm<input id='restoreconfirm' autocomplete='off'></label><div class='actions'><button id='restorestart'>Restore (manager card required)</button></div><p id='restorestatus' role='status'></p></section>"
 "<script>const cfg=document.getElementById('configstatus'),status=document.getElementById('downloadstatus'),files=document.getElementById('files'),recipient=document.getElementById('recipient'),banner=document.getElementById('bkbanner');"
 "const api=tc.api;"
 "async function waitForCard(token,callback,onError){let done=false;async function poll(){if(done)return;try{const a=await api('/api/v1/admin/system/status',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({token})});if(a.status==='waiting'){setTimeout(poll,700);return;}done=true;if(a.status==='expired')onError(Error(tc.TIMEOUT));else callback(a)}catch(e){done=true;onError(e)}}setTimeout(poll,700)}"
 "function when(t){return tc.datetime(new Date(t*1000))}"
 "async function load(){try{const j=await api('/api/v1/admin/backups/settings');enabled.checked=j.enabled;recipient.replaceChildren();for(const m of j.managers){const o=document.createElement('option');o.value=m.id;o.textContent=m.name+' ('+m.email+')';recipient.append(o)}if([...recipient.options].some(o=>o.value===String(j.manager_id)))recipient.value=String(j.manager_id);if(!j.managers.length){const o=document.createElement('option');o.textContent='No manager has an email address';o.value='0';recipient.append(o)}"
 "const l=await api('/api/v1/admin/backups/list');l.files.sort((a,b)=>b.mtime-a.mtime);files.replaceChildren();document.getElementById('restorefile').replaceChildren();"
 "const table=document.createElement('table'),body=document.createElement('tbody');table.innerHTML='<thead><tr><th>Backup</th><th>Created</th><th>Size</th></tr></thead>';"
 "for(const f of l.files){const tr=document.createElement('tr'),a=document.createElement('a'),c1=document.createElement('td'),c2=document.createElement('td'),c3=document.createElement('td');a.href='#';a.textContent=f.name;a.title='Download '+f.name;a.onclick=e=>{e.preventDefault();download(f.name)};c1.append(a);c2.textContent=when(f.mtime);c3.textContent=(f.size/1048576).toFixed(2)+' MiB';tr.append(c1,c2,c3);body.append(tr);const ro=document.createElement('option');ro.value=f.name;ro.textContent=f.name+' ('+when(f.mtime)+')';document.getElementById('restorefile').append(ro)}"
 "table.append(body);files.append(table);let newest=l.files.length?l.files[0].mtime:0,age=newest?Date.now()/1000-newest:0;"
 "if(!newest){files.replaceChildren(tc.flag('Problem','problem'),' No backups on this clock.');tc.banner(banner,'problem','Problem','No backup exists on this clock.')}"
 "else if(age>2*86400)tc.banner(banner,'problem','Problem','Last backup was '+tc.ago(age)+' ago. The daily backup is not running.');else tc.banner(banner,'','','');"
 "}catch(e){files.replaceChildren();tc.err(status,e);tc.banner(banner,'problem','Problem','Backups cannot be listed: '+e.message)}}"
 "document.getElementById('uploadstart').onclick=()=>{const inp=document.getElementById('uploadfile'),file=inp.files[0],st=document.getElementById('uploadstatus'),bar=document.getElementById('uploadprogress'),btn=document.getElementById('uploadstart');if(!file){tc.err(st,'Choose a backup file.');return;}if(file.size>64*1024*1024||!/^timeclock-[a-zA-Z0-9.-]+\\.tar\\.gz$/.test(file.name)){tc.err(st,'Choose a timeclock backup (.tar.gz, up to 64 MiB).');return;}const xhr=new XMLHttpRequest();btn.disabled=true;bar.hidden=false;bar.value=0;st.textContent='Uploading...';xhr.upload.onprogress=e=>{if(e.lengthComputable)bar.value=Math.round(100*e.loaded/e.total)};xhr.onload=()=>{btn.disabled=false;let j={};try{j=JSON.parse(xhr.responseText)}catch(e){}if(xhr.status!==202){tc.err(st,j.error||'Upload failed ('+xhr.status+')');return;}st.textContent='Received. Checking the backup...';let tries=0;const check=async()=>{try{const q=await api('/api/v1/admin/backups/upload/status');if(q.state==='success'){tc.ok(st,'Backup checked and added: '+q.archive);await load();return;}if(q.state==='failed'){tc.err(st,'Backup rejected: '+q.message);return;}if(++tries<90)setTimeout(check,1000);else st.textContent='Still checking. Reload the page to see the result.'}catch(e){tc.err(st,e)}};setTimeout(check,700)};xhr.onerror=()=>{btn.disabled=false;tc.err(st,'Upload interrupted. Check the connection and try again.')};xhr.open('POST','/api/v1/admin/backups/upload?name='+encodeURIComponent(file.name));xhr.setRequestHeader('Content-Type','application/octet-stream');xhr.send(file)};"
 "save.onclick=async()=>{try{await api('/api/v1/admin/backups/settings',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({enabled:enabled.checked?'1':'0',manager_id:recipient.value||'0'})});tc.ok(cfg,'Saved.')}catch(e){tc.err(cfg,e)}};"
 "document.getElementById('restorestart').onclick=async()=>{const output=document.getElementById('restorestatus'),filename=document.getElementById('restorefile').value;if(document.getElementById('restoreconfirm').value!=='RESTORE'){tc.err(output,'Type RESTORE to confirm.');return;}if(!confirm('Replace all data on this clock with '+filename+'? Anything recorded after this backup is lost.'))return;try{const j=await api('/api/v1/admin/backups/restore/arm',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({filename,confirm:'RESTORE'})});output.textContent=tc.CARD;waitForCard(j.token,a=>{tc.ok(output,'Restore started. This page may disconnect while the clock restarts.');setTimeout(restoreResult,5000)},e=>{tc.err(output,e)});}catch(e){tc.err(output,e)}};"
 "async function restoreResult(){const el=document.getElementById('lastrestore');try{const r=await api('/api/v1/admin/backups/restore/result');el.replaceChildren();const lv={success:['Restored','ok'],failed:['Failed','problem'],critical:['Needs manual recovery','problem'],validating:['In progress','check'],stopping:['In progress','check']}[r.state];el.append('Last restore: ');if(lv)el.append(tc.flag(lv[0],lv[1]),' ');el.append(r.message+(r.archive?' ('+r.archive+')':''));tc.banner(document.getElementById('rsbanner'),'problem','Problem',r.state==='critical'?'The last restore needs manual recovery: '+r.message:'')}catch(e){tc.err(el,e)}}restoreResult();"
 "async function download(filename){try{status.textContent='Starting...';const j=await api('/api/v1/admin/backups/download/arm',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({filename})});status.textContent=tc.CARD;waitForCard(j.token,a=>{if(a.download_token){tc.ok(status,'Approved. Downloading '+filename+'.');location.href='/api/v1/admin/backups/download?token='+encodeURIComponent(a.download_token)}else status.textContent=a.status},e=>{tc.err(status,e)});}catch(e){tc.err(status,e)}}load();</script>"UI_FOOT);
}
static void system_page(int fd){
 send_response(fd,200,"text/html; charset=utf-8",UI_HEAD("System operations","system")
 "<h1>System operations</h1><p>Each action requires a manager card. The clock does not record punches until the action finishes.</p>"
 "<section><label>Action<select id='action'><option value='reboot'>Restart the clock</option><option value='shutdown'>Shut down the clock</option><option value='restart'>Restart timeclock services</option><option value='backup'>Back up now</option></select></label>"
 "<div class='actions'><button id='arm'>Start</button></div><p id='state' role='status'></p></section>"
 "<script>let token='',timer=null,pollActive=false;const state=document.getElementById('state');"
 "if(new URLSearchParams(location.search).get('backup')==='1')document.getElementById('action').value='backup';"
 "arm.onclick=async()=>{if(pollActive)return;if(!confirm(action.options[action.selectedIndex].text+'? Punches are not recorded until it finishes.'))return;arm.disabled=true;"
 "try{const r=await tc.request('/api/v1/admin/system/arm',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({action:action.value})});"
 "const j=await tc.json(r);if(!r.ok)throw Error(j.error);token=j.token;state.textContent=tc.CARD;"
 "pollActive=true;const currentToken=token;async function poll(){if(!pollActive||currentToken!==token)return;try{const r=await tc.request('/api/v1/admin/system/status',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({token:currentToken})});const j=await tc.json(r);"
 "if(!r.ok)throw Error(j.error||'Authorization failed');if(j.status==='waiting'){timer=setTimeout(poll,700);return;}pollActive=false;arm.disabled=false;if(j.status==='expired')tc.err(state,tc.TIMEOUT);else tc.ok(state,j.status||'Started.');}catch(e){pollActive=false;arm.disabled=false;tc.err(state,e)}}timer=setTimeout(poll,700);"
 "}catch(e){pollActive=false;arm.disabled=false;tc.err(state,e)}};</script>"UI_FOOT);
}
static void system_arm(int fd,int who,const char*body){
 char action[128]="",kind[24]="";if(!form_value(body,"action",kind,sizeof kind)){
  send_response(fd,422,"application/json","{\"error\":\"Choose an action.\"}");return;}
 if(!strcmp(kind,"timezone")){
  char zone[110]="";if(!form_value(body,"timezone",zone,sizeof zone)||!timezone_valid(zone)){
   send_response(fd,422,"application/json","{\"error\":\"Choose a timezone from the list.\"}");return;}
  snprintf(action,sizeof action,"timezone:%s",zone);
 }else if(!strcmp(kind,"reboot")||!strcmp(kind,"shutdown")||!strcmp(kind,"restart")||!strcmp(kind,"backup")){
  snprintf(action,sizeof action,"%s",kind);
 }else{send_response(fd,422,"application/json","{\"error\":\"Choose an action.\"}");return;}
 if(!sql("BEGIN IMMEDIATE")){send_response(fd,503,"application/json","{\"error\":\"The clock is busy. Try again.\"}");return;}
 if(authorization_pending()){sql("ROLLBACK");send_response(fd,409,"application/json","{\"error\":\"The card reader is waiting for another approval. Try again in 30 seconds.\"}");return;}
 sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(db,"INSERT INTO system_actions(id,requested_by,action,armed_at) VALUES(1,?1,?2,CAST(strftime('%s','now') AS INTEGER)) ON CONFLICT(id) DO UPDATE SET requested_by=excluded.requested_by,action=excluded.action,armed_at=excluded.armed_at",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,who);sqlite3_bind_text(q,2,action,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 char token[33]="";if(ok)ok=sql("UPDATE manager_scan SET armed_at=CAST(strftime('%s','now') AS INTEGER),uid='',scanned_at=0,scan_mode='approval' WHERE id=1");
 if(ok)ok=approval_token_new(token);
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");
 char result[120];snprintf(result,sizeof result,"{\"status\":\"waiting\",\"token\":\"%s\"}",token);
 send_response(fd,ok?200:500,"application/json",ok?result:"{\"error\":\"The card reader is waiting for another approval. Try again in 30 seconds.\"}");
}
static void system_status(int fd,int who){
 sqlite3_stmt*q=NULL;int owner=0;char action[128]="",uid[48]="";long long armed=0,scanned=0;
 if(sqlite3_prepare_v2(db,"SELECT a.requested_by,a.action,a.armed_at,s.uid,s.scanned_at FROM system_actions a JOIN manager_scan s ON s.id=1 WHERE a.id=1",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW){
  owner=sqlite3_column_int(q,0);snprintf(action,sizeof action,"%s",sqlite3_column_text(q,1));armed=sqlite3_column_int64(q,2);
  const unsigned char*u=sqlite3_column_text(q,3);if(u)snprintf(uid,sizeof uid,"%s",u);scanned=sqlite3_column_int64(q,4);
 }sqlite3_finalize(q);
 if(owner!=who||!armed){send_response(fd,409,"application/json","{\"error\":\"This approval is no longer valid. Start again.\"}");return;}
 if((long long)time(NULL)-armed>30){sql("DELETE FROM system_actions WHERE id=1");sql("UPDATE manager_scan SET armed_at=0,uid='',scanned_at=0,request_token='' WHERE id=1");send_response(fd,200,"application/json","{\"status\":\"expired\"}");return;}
 if(scanned<armed||!uid[0]){send_response(fd,200,"application/json","{\"status\":\"waiting\"}");return;}
 int authorized=0;
 if(sqlite3_prepare_v2(db,"SELECT 1 FROM manager_cards mc JOIN employees e ON e.id=mc.manager_id JOIN user_accounts ua ON ua.employee_id=e.id JOIN employee_roles er ON er.employee_id=e.id AND er.role='manager' WHERE mc.uid=?1 COLLATE NOCASE AND mc.manager_id=?2 AND mc.active=1 AND e.active=1 AND ua.disabled=0",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,uid,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,2,who);authorized=sqlite3_step(q)==SQLITE_ROW;}sqlite3_finalize(q);
 if(!authorized){sql("UPDATE manager_scan SET uid='',scanned_at=0 WHERE id=1");send_response(fd,200,"application/json","{\"status\":\"waiting\"}");return;}
 /* One-use and server-owned action name. Restricted privileged helper validates it again. */
 if(!sql("BEGIN IMMEDIATE")){send_response(fd,503,"application/json","{\"error\":\"The clock is busy. Try again.\"}");return;}
 int ok=sql("DELETE FROM system_actions WHERE id=1")&&sql("UPDATE manager_scan SET armed_at=0,uid='',scanned_at=0,request_token='' WHERE id=1");
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");
 if(!ok){send_response(fd,500,"application/json","{\"error\":\"The clock could not complete this. Try again.\"}");return;}
 if(!strcmp(action,"download")){
  char download_token[65]="";if(!random_hex(download_token,32)){send_response(fd,500,"application/json","{\"error\":\"Download not approved. Try again.\"}");return;}
  int saved=sqlite3_prepare_v2(db,"UPDATE backup_download SET token=?1,approved_at=CAST(strftime('%s','now') AS INTEGER) WHERE id=1 AND requested_by=?2",-1,&q,NULL)==SQLITE_OK;
  if(saved){sqlite3_bind_text(q,1,download_token,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,2,who);saved=sqlite3_step(q)==SQLITE_DONE&&sqlite3_changes(db)==1;}sqlite3_finalize(q);
  char result[180];snprintf(result,sizeof result,"{\"status\":\"approved\",\"download_token\":\"%s\"}",download_token);
  send_response(fd,saved?200:500,"application/json",saved?result:"{\"error\":\"Download not approved. Try again.\"}");return;
 }
 if(!strcmp(action,"restore")){
  char filename[128]="";int ok=0;
  if(sqlite3_prepare_v2(db,"SELECT filename FROM backup_restore WHERE id=1 AND requested_by=?1",-1,&q,NULL)==SQLITE_OK){
   sqlite3_bind_int(q,1,who);if(sqlite3_step(q)==SQLITE_ROW){const unsigned char*v=sqlite3_column_text(q,0);if(v)snprintf(filename,sizeof filename,"%s",(const char*)v);}}
  sqlite3_finalize(q);
  if(!backup_name(filename)){send_response(fd,409,"application/json","{\"error\":\"Select a backup.\"}");return;}
  if(!sql("BEGIN IMMEDIATE")){send_response(fd,503,"application/json","{\"error\":\"The clock is busy. Try again.\"}");return;}
  ok=sqlite3_prepare_v2(db,"INSERT INTO backup_restore_audit(manager_id,filename) VALUES(?1,?2)",-1,&q,NULL)==SQLITE_OK;
  if(ok){sqlite3_bind_int(q,1,who);sqlite3_bind_text(q,2,filename,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
  if(ok)ok=sql("DELETE FROM backup_restore WHERE id=1");
  if(ok)ok=sql("COMMIT");else sql("ROLLBACK");
  if(!ok){send_response(fd,500,"application/json","{\"error\":\"Could not start. Try again.\"}");return;}
  FILE*f=fopen("/var/lib/timeclock/system-action.tmp","w");
  if(!f){send_response(fd,500,"application/json","{\"error\":\"Could not start. Try again.\"}");return;}
  fprintf(f,"restore:%s\n",filename);
  int queued=!fclose(f)&&rename("/var/lib/timeclock/system-action.tmp","/var/lib/timeclock/system-action.request")==0;
  send_response(fd,queued?200:500,"application/json",queued?"{\"status\":\"Approved. Restoring now.\"}":"{\"error\":\"Could not start. Try again.\"}");return;
 }
 if(!strncmp(action,"timezone:",9)){
  const char *zone=action+9;
  if(!timezone_valid(zone)){send_response(fd,422,"application/json","{\"error\":\"Choose a timezone from the list.\"}");return;}
  const char *tmp="/var/lib/timeclock/timezone.tmp",*dst="/var/lib/timeclock/timezone.request";
  int out=open(tmp,O_CREAT|O_TRUNC|O_WRONLY|O_NOFOLLOW,0600);
  if(out<0){send_response(fd,500,"application/json","{\"error\":\"Could not start. Try again.\"}");return;}
  int ok=write(out,zone,strlen(zone))==(ssize_t)strlen(zone)&&write(out,"\n",1)==1&&fsync(out)==0;
  if(close(out)!=0)ok=0;
  if(ok)ok=rename(tmp,dst)==0;else unlink(tmp);
  send_response(fd,ok?200:500,"application/json",ok?"{\"status\":\"Timezone approved. The clock is restarting.\"}":"{\"error\":\"Could not start. Try again.\"}");return;
 }
 /* The request directory is owned by timeclock; only root helper may execute an action. */
 FILE*f=fopen("/var/lib/timeclock/system-action.tmp","w");
 if(!f){send_response(fd,500,"application/json","{\"error\":\"Could not start. Try again.\"}");return;}
 fprintf(f,"%s\n",action);int fileok=!fclose(f)&&rename("/var/lib/timeclock/system-action.tmp","/var/lib/timeclock/system-action.request")==0;
 send_response(fd,fileok?200:500,"application/json",fileok?"{\"status\":\"Approved. Starting now.\"}":"{\"error\":\"Could not start. Try again.\"}");
}


/* Read-only manager diagnostics. Never include UID, credentials, or employee details. */
static void diag_escape(const char *src,char *dst,size_t cap){
 size_t n=0;for(;*src&&n+7<cap;src++){
  unsigned char c=(unsigned char)*src;
  if(c=='"'||c=='\\'){dst[n++]='\\';dst[n++]=(char)c;}
  else if(c<32){n+=(size_t)snprintf(dst+n,cap-n,"\\u%04x",c);}
  else dst[n++]=(char)c;
 }dst[n]=0;
}
static void diag_read(const char *path,char *out,size_t len){
 snprintf(out,len,"Unavailable");FILE*f=fopen(path,"r");if(!f)return;
 if(fgets(out,(int)len,f)){out[strcspn(out,"\r\n")]=0;}else snprintf(out,len,"Unavailable");fclose(f);
}
static long diag_count(const char *query){
 sqlite3_stmt*q=NULL;long n=-1;if(sqlite3_prepare_v2(db,query,-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW)n=(long)sqlite3_column_int64(q,0);
 sqlite3_finalize(q);return n;
}
static void diagnostics_json(int fd){
 char host[128]="Unavailable",os[128],kernel[128],arch[128],temp[64],ntp[64],hostj[270],osj[270],kernj[270],archj[270],tempj[140],ntpj[140];
 struct utsname un;memset(&un,0,sizeof un);if(uname(&un))snprintf(un.sysname,sizeof un.sysname,"Unavailable");
 if(gethostname(host,sizeof host))snprintf(host,sizeof host,"Unavailable");
 host[sizeof host-1]=0;
 snprintf(os,sizeof os,"%s",un.sysname);snprintf(kernel,sizeof kernel,"%s",un.release);snprintf(arch,sizeof arch,"%s",un.machine);
 diag_read("/sys/class/thermal/thermal_zone0/temp",temp,sizeof temp);
 snprintf(ntp,sizeof ntp,"%s",clock_synced()?"Synchronized":"Not synchronized");
 struct sysinfo si;memset(&si,0,sizeof si);long uptime=sysinfo(&si)==0?si.uptime:-1;
 struct statvfs fs;memset(&fs,0,sizeof fs);int disk=statvfs("/",&fs)==0;
 unsigned long long total=disk?(unsigned long long)fs.f_blocks*fs.f_frsize:0,freeb=disk?(unsigned long long)fs.f_bavail*fs.f_frsize:0;
 diag_escape(host,hostj,sizeof hostj);diag_escape(os,osj,sizeof osj);diag_escape(kernel,kernj,sizeof kernj);diag_escape(arch,archj,sizeof archj);diag_escape(temp,tempj,sizeof tempj);diag_escape(ntp,ntpj,sizeof ntpj);
 struct tc_wifi wifi=tc_wifi_read();char ssidj[160];
 diag_escape(wifi.ssid[0]?wifi.ssid:"Unavailable",ssidj,sizeof ssidj);
 char uptime_json[32],total_json[40],free_json[40],wifi_bars_json[16];
 if(uptime<0)snprintf(uptime_json,sizeof uptime_json,"null");else snprintf(uptime_json,sizeof uptime_json,"%ld",uptime);
 if(!disk){snprintf(total_json,sizeof total_json,"null");snprintf(free_json,sizeof free_json,"null");}
 else{snprintf(total_json,sizeof total_json,"%llu",total);snprintf(free_json,sizeof free_json,"%llu",freeb);}
 if(!wifi.associated)snprintf(wifi_bars_json,sizeof wifi_bars_json,"null");else snprintf(wifi_bars_json,sizeof wifi_bars_json,"%d",wifi.bars);
 char b[5000];snprintf(b,sizeof b,"{\"product\":\"mk-timeclock\",\"version\":\"%s\",\"api\":\"1\",\"hostname\":\"%s\",\"os\":\"%s\",\"kernel\":\"%s\",\"architecture\":\"%s\",\"uptime_seconds\":%s,\"time_epoch\":%lld,\"temperature_millicelsius\":\"%s\",\"ntp_indicator\":\"%s\",\"storage_total_bytes\":%s,\"storage_available_bytes\":%s,\"queued_email\":%ld,\"failed_email\":%ld,\"rejected_uploads\":%ld,\"raw_swipes\":%ld,\"classified_swipes\":%ld,\"wifi_ssid\":\"%s\",\"wifi_signal_dbm\":%d,\"wifi_signal_bars\":%s,\"wifi_connected\":%s,\"wifi_link_mbps\":%d}",TIMECLOCK_VERSION,hostj,osj,kernj,archj,uptime_json,(long long)time(NULL),tempj,ntpj,total_json,free_json,diag_count("SELECT count(*) FROM email_queue WHERE status='pending'"),diag_count("SELECT count(*) FROM email_queue WHERE status='failed'"),diag_count("SELECT count(*) FROM rejected_uploads"),diag_count("SELECT count(*) FROM swipes"),diag_count("SELECT count(*) FROM attendance_events"),ssidj,wifi.dbm,wifi_bars_json,wifi.associated?"true":"false",wifi.bitrate_mbps);
 send_response(fd,200,"application/json; charset=utf-8",b);
}
/* Only stock DejaVu fonts may be selected. Existing uploads remain on disk. */
static const char *font_names[]={"DejaVuSansMono.ttf","DejaVuSans.ttf","DejaVuSerif.ttf","DejaVuSansMono-Bold.ttf","DejaVuSans-Bold.ttf","DejaVuSerif-Bold.ttf"};
static int font_path(const char *name,char *path,size_t cap){
 for(size_t i=0;i<sizeof(font_names)/sizeof(font_names[0]);i++)if(!strcmp(name,font_names[i])){
  snprintf(path,cap,"/usr/share/fonts/truetype/dejavu/%s",name);return access(path,R_OK)==0;
 }
 return 0;
}
/* A bounded, manager-only journal view. Fixed unit allowlist, no user command input. */
static void diagnostics_log(int fd){
 const char *cmd="/usr/bin/timeout 1s /usr/bin/journalctl --no-pager --quiet --output=short-iso --lines=120 -u timeclock-agent.service -u timeclock-server.service -u timeclock-mailer.service -u timeclock-backup.service -u timeclock-system-action.service 2>&1";
 FILE *f=popen(cmd,"r");
 if(!f){send_response(fd,503,"text/plain; charset=utf-8","Journal reader unavailable.\n");return;}
 char out[45000];size_t used=0;char line[1200];
 while(fgets(line,sizeof line,f)){
  size_t n=strlen(line);if(used+n>=sizeof(out)-64)break;
  /* Do not allow terminal escapes or control bytes in diagnostic output. */
  for(size_t i=0;i<n;i++)if((unsigned char)line[i]<32 && line[i]!='\n' && line[i]!='\t')line[i]='?';
  memcpy(out+used,line,n);used+=n;
 }
 int status=pclose(f);
 if(!used){/* SIGCHLD=SIG_IGN auto-reaps download children and makes pclose return -1/ECHILD. */
  const char *msg=(status!=-1 && status!=0)?"Journal unavailable. Check systemd-journal access.\n":"No recent timeclock log entries.\n";snprintf(out,sizeof out,"%s",msg);used=strlen(out);}
 out[used]=0;
 send_response(fd,200,"text/plain; charset=utf-8",out);
}
static void audit_json(int fd){
 static const struct {const char*name,*label,*query;} sets[]={
 {"timeoff_audit","Time-off decisions","SELECT a.at,coalesce(e.name,'Employee #'||a.actor_id)||' '||CASE a.action WHEN 'approved' THEN 'approved' WHEN 'denied' THEN 'declined' WHEN 'requested' THEN 'requested' ELSE a.action END||' '||coalesce(t.name,'Employee #'||r.employee_id)||' '||r.kind||', '||r.starts||' to '||r.ends FROM timeoff_audit a LEFT JOIN employees e ON e.id=a.actor_id JOIN timeoff_requests r ON r.id=a.request_id LEFT JOIN employees t ON t.id=r.employee_id ORDER BY a.id DESC LIMIT 50"},
 {"period_signoffs","Pay period approvals","SELECT s.signed_at,coalesce(m.name,'Manager #'||s.signed_by)||' approved '||coalesce(e.name,'Employee #'||s.employee_id)||' for '||coalesce(p.starts||' to '||p.ends,'period #'||s.period_id) FROM period_signoffs s LEFT JOIN employees m ON m.id=s.signed_by LEFT JOIN employees e ON e.id=s.employee_id LEFT JOIN pay_periods p ON p.id=s.period_id WHERE s.role='manager' ORDER BY s.signed_at DESC LIMIT 50"},
 {"attendance_absence_audit","Absences","SELECT a.recorded_at,coalesce(e.name,'Manager #'||a.manager_id)||' '||CASE a.action WHEN 'create' THEN 'recorded absence for' WHEN 'cancel' THEN 'cancelled absence for' ELSE a.action||' absence for' END||' '||coalesce(t.name,'Employee #'||b.employee_id)||' on '||b.day||' ('||a.reason||')' FROM attendance_absence_audit a LEFT JOIN employees e ON e.id=a.manager_id JOIN attendance_absences b ON b.id=a.absence_id LEFT JOIN employees t ON t.id=b.employee_id ORDER BY a.id DESC LIMIT 50"},
 {"account_change_audit","Manager account changes","SELECT at,coalesce((SELECT name FROM employees WHERE id=manager_id),'#'||manager_id)||': '||action||' for '||coalesce((SELECT name FROM employees WHERE id=target_id),'#'||target_id) FROM account_change_audit ORDER BY id DESC LIMIT 50"},
 {"review_link_audit","Timesheet links","SELECT at,coalesce((SELECT name FROM employees WHERE id=manager_id),'#'||manager_id)||': '||action||' for '||coalesce((SELECT name FROM employees WHERE id=employee_id),'#'||employee_id)||', period '||coalesce((SELECT starts||' to '||ends FROM pay_periods WHERE id=period_id),'#'||period_id) FROM review_link_audit ORDER BY id DESC LIMIT 50"},
 {"admin_export_audit","Attendance exports","SELECT created_at,coalesce((SELECT name FROM employees WHERE id=manager_id),'#'||manager_id)||' exported '||scope||' ('||format||'), '||from_date||' to '||to_date FROM admin_export_audit ORDER BY id DESC LIMIT 50"},
 {"backup_download_audit","Backup downloads","SELECT downloaded_at,coalesce((SELECT name FROM employees WHERE id=manager_id),'#'||manager_id)||' downloaded '||filename FROM backup_download_audit ORDER BY id DESC LIMIT 50"},
 {"backup_restore_audit","Backup restores","SELECT requested_at,coalesce((SELECT name FROM employees WHERE id=manager_id),'#'||manager_id)||' restored '||filename FROM backup_restore_audit ORDER BY id DESC LIMIT 50"},
 {"card_replacement_audit","Card replacements","SELECT replaced_at,coalesce((SELECT name FROM employees WHERE id=manager_id),'#'||manager_id)||' replaced card '||old_uid||' with '||new_uid||' for '||coalesce((SELECT name FROM employees WHERE id=employee_id),'#'||employee_id) FROM card_replacement_audit ORDER BY id DESC LIMIT 50"},
 {"email_retry_audit","Email retries","SELECT retried_at,coalesce((SELECT name FROM employees WHERE id=manager_id),'#'||manager_id)||' retried email to '||coalesce((SELECT recipient FROM email_queue WHERE id=email_id),'#'||email_id) FROM email_retry_audit ORDER BY id DESC LIMIT 50"},
 {"employee_change_audit","Employee edits","SELECT recorded_at,coalesce((SELECT name FROM employees WHERE id=manager_id),'#'||manager_id)||' edited '||CASE WHEN employee_id IS NULL THEN 'a new employee' ELSE coalesce((SELECT name FROM employees WHERE id=employee_id),'#'||employee_id) END FROM employee_change_audit ORDER BY id DESC LIMIT 50"},
 {"schedule_change_audit","Schedule edits","SELECT recorded_at,coalesce((SELECT name FROM employees WHERE id=manager_id),'#'||manager_id)||' changed a schedule' FROM schedule_change_audit ORDER BY id DESC LIMIT 50"},
 {"template_restore_audit","Template restores","SELECT restored_at,coalesce((SELECT name FROM employees WHERE id=manager_id),'#'||manager_id)||' restored the default for '||template_id FROM template_restore_audit ORDER BY id DESC LIMIT 50"},
 {"punch_correction_history","Punch corrections","SELECT recorded_at,coalesce((SELECT name FROM employees WHERE id=manager_id),'Unknown')||': '||coalesce(action,'')||' '||coalesce(kind,'')||' at '||coalesce(strftime('%Y-%m-%d %H:%M',effective_at,'localtime'),'')||' for '||coalesce((SELECT name FROM employees e WHERE e.id=punch_correction_history.employee_id),'unknown')||'. Reason: '||coalesce(reason,'') FROM punch_correction_history ORDER BY id DESC LIMIT 50"},
 {"swipe_note_history","Note revisions","SELECT h.updated_at,coalesce((SELECT name FROM employees WHERE id=h.updated_by),'Unknown')||' changed the note on the '||coalesce(strftime('%Y-%m-%d %H:%M',s.swiped_at,'localtime'),'unknown')||' punch'||CASE WHEN s.employee_id IS NOT NULL AND s.employee_id<>coalesce(h.updated_by,0) THEN ' of '||coalesce((SELECT name FROM employees WHERE id=s.employee_id),'#'||s.employee_id) ELSE '' END FROM swipe_note_history h LEFT JOIN swipes s ON s.event_id=h.event_id ORDER BY h.id DESC LIMIT 50"}
 };
 sqlite3_str*j=sqlite3_str_new(db);if(!j){send_response(fd,500,"application/json","{\"error\":\"memory\"}");return;}
 sqlite3_str_appendall(j,"{\"sections\":[");int ok=1;
 for(size_t i=0;i<sizeof(sets)/sizeof(sets[0])&&ok;i++){
  sqlite3_stmt*q=NULL;if(sqlite3_prepare_v2(db,sets[i].query,-1,&q,NULL)!=SQLITE_OK){ok=0;break;}
  if(i){sqlite3_str_appendchar(j,1,',');}sqlite3_str_appendall(j,"{\"name\":");str_json(j,sets[i].name);
  sqlite3_str_appendall(j,",\"label\":");str_json(j,sets[i].label);sqlite3_str_appendall(j,",\"rows\":[");int n=0,rc;
  while((rc=sqlite3_step(q))==SQLITE_ROW){if(n++)sqlite3_str_appendchar(j,1,',');sqlite3_str_appendall(j,"{\"at\":");
   const unsigned char*at=sqlite3_column_text(q,0);str_json(j,(const char*)(at?at:(const unsigned char*)""));
   sqlite3_str_appendall(j,",\"action\":");const unsigned char*action=sqlite3_column_text(q,1);str_json(j,(const char*)(action?action:(const unsigned char*)""));sqlite3_str_appendchar(j,1,'}');}
  if(rc!=SQLITE_DONE){ok=0;}sqlite3_finalize(q);sqlite3_str_appendall(j,"]}");
 }
 sqlite3_str_appendall(j,"]}");char*out=sqlite3_str_finish(j);
 if(!ok||!out)send_response(fd,500,"application/json","{\"error\":\"The clock could not complete this. Try again.\"}");else send_response(fd,200,"application/json",out?out:"{}");sqlite3_free(out);
}
static void audit_page(int fd){send_response(fd,200,"text/html; charset=utf-8",
 UI_HEAD("Audit log","audit")"<h1>Audit log</h1><p>The latest 50 entries in each category. Times are local.</p><div id='auditrows'>Loading...</div>"
 "<script>async function refresh(){let root=document.getElementById('auditrows');try{let r=await tc.request('/api/v1/admin/audit',{cache:'no-store'});if(!r.ok)throw Error('Audit unavailable ('+r.status+')');let data=await tc.json(r);root.replaceChildren();for(let section of data.sections){let box=document.createElement('section'),h=document.createElement('h2');h.textContent=section.label;box.append(h);if(!section.rows.length){let p=document.createElement('p');p.textContent='None';p.className='muted';box.append(p)}else{let t=document.createElement('table'),head=document.createElement('tr');for(let v of ['Time (local)','Activity']){let th=document.createElement('th');th.textContent=v;head.append(th)}t.append(head);for(let record of section.rows){let tr=document.createElement('tr');let d=new Date(record.at);for(let v of [Number.isNaN(d.getTime())?record.at:tc.datetime(d),record.action]){let td=document.createElement('td');td.textContent=v;tr.append(td)}t.append(tr)}box.append(t)}root.append(box)}}catch(e){tc.err(root,e)}}refresh()</script>"UI_FOOT);
}
static void diagnostics_page(int fd){
 send_response(fd,200,"text/html; charset=utf-8",UI_HEAD("Diagnostics","diagnostics")
 "<h1>Diagnostics</h1><p>Updates every 30 seconds.</p><p id='diagbanner' role='alert' hidden></p>"
 "<div class='actions diag-toolbar'><a href='/api/v1/admin/diagnostics/report' download='timeclock-diagnostics.json'>Download diagnostic report</a><span class='muted' id='diagUpdated' role='status'>Loading...</span></div>"
 "<div id='diag' class='diag-grid' aria-live='off'></div><p id='diagerr' role='status'></p>"
 "<section class='diag-log-section'><h2>Activity log</h2><p>Recent clock service events, newest last.</p><div class='diag-toolbar'><div class='actions'><button type='button' id='logRefresh'>Refresh log</button><label class='diag-auto'><input type='checkbox' id='logAuto'> Auto-refresh every 15 seconds</label><a href='/api/v1/admin/diagnostics/log' download='timeclock-activity.log'>Download log</a><span class='muted' id='logUpdated' role='status'></span></div></div><pre class='diag-log' id='diagLog' tabindex='0' aria-label='Recent timeclock service activity'>Log not loaded. Select Refresh log.</pre></section>"
 "<script>const groups=[['System',[['product','Product'],['version','Clock version'],['api','API version'],['uptime_seconds','Uptime'],['time_epoch','Clock time']]],['Network and platform',[['hostname','Hostname'],['wifi_ssid','Wi-Fi network'],['wifi_connected','Wi-Fi'],['wifi_signal_dbm','Wi-Fi signal (dBm)'],['wifi_signal_bars','Wi-Fi strength (of 4)'],['wifi_link_mbps','Wi-Fi link Mbps'],['os','Operating system'],['kernel','Kernel'],['architecture','Architecture'],['ntp_indicator','Time sync'],['temperature_millicelsius','CPU temperature']]],['Storage',[['storage_total_bytes','Total storage'],['storage_available_bytes','Free storage']]],['Timeclock activity',[['raw_swipes','Swipes stored'],['classified_swipes','Swipes classified'],['rejected_uploads','Swipes refused (all time)'],['queued_email','Email waiting to send'],['failed_email','Failed emails']]]];"
 "function format(k,v){if(v===null||v===undefined||v==='Unavailable'||v==='')return 'Unavailable';if(k==='wifi_connected')return v?'Connected':'Disconnected';if(['wifi_signal_dbm','wifi_signal_bars','wifi_link_mbps'].includes(k)&&Number(v)===0)return 'Unavailable';if(k==='time_epoch'&&v>0)return tc.datetime(new Date(v*1000));if(k==='uptime_seconds'&&v>=0)return Math.floor(v/86400)+'d '+Math.floor(v%86400/3600)+'h '+Math.floor(v%3600/60)+'m';if(k==='temperature_millicelsius'){let n=Number(v);return Number.isFinite(n)?(n/1000).toFixed(1)+' °C':'Unavailable'}if(k.endsWith('_bytes'))return Number(v)>0?(Number(v)/1073741824).toFixed(2)+' GiB':'Unavailable';return String(v)}"
 "function draw(d){let root=document.getElementById('diag'),problems=0,checks=0;root.replaceChildren();for(const [name,fields] of groups){let panel=document.createElement('section');panel.className='diag-panel';let h=document.createElement('h2');h.textContent=name;panel.append(h);let table=document.createElement('table');table.className='diag-table';let tbody=document.createElement('tbody');for(const [key,label] of fields){let tr=document.createElement('tr'),th=document.createElement('th'),td=document.createElement('td');th.scope='row';th.textContent=label;td.textContent=format(key,d[key]);let f=tc.health(key,d[key]);if(f){td.append(' ',tc.flag(f[1],f[0]));tr.className='row-'+f[0];f[0]==='problem'?problems++:checks++}tr.append(th,td);tbody.append(tr)}table.append(tbody);panel.append(table);root.append(panel)}let b=document.getElementById('diagbanner');if(problems)tc.banner(b,'problem','Problem',problems+(problems===1?' reading needs':' readings need')+' action. Flagged below.');else tc.banner(b,'check','Check',checks?checks+(checks===1?' reading needs':' readings need')+' a look. Flagged below.':'');}"
 "async function refreshLog(){const pane=document.getElementById('diagLog'),status=document.getElementById('logUpdated');const stick=pane.scrollTop+pane.clientHeight>=pane.scrollHeight-35;try{const r=await tc.request('/api/v1/admin/diagnostics/log',{cache:'no-store'});if(!r.ok)throw Error('Log unavailable ('+r.status+')');pane.textContent=await r.text();if(stick)pane.scrollTop=pane.scrollHeight;status.textContent='Updated '+tc.time(new Date())}catch(e){tc.err(status,e)}}document.getElementById('logRefresh').addEventListener('click',refreshLog);setInterval(()=>{if(document.getElementById('logAuto').checked&&!document.hidden)refreshLog()},15000);"
 "async function refresh(){try{const r=await tc.request('/api/v1/admin/diagnostics/report' ,{cache:'no-store'});if(!r.ok)throw Error('Diagnostics unavailable ('+r.status+')');const d=await tc.json(r);draw(d);document.getElementById('diagUpdated').textContent='Updated '+tc.time(new Date());document.getElementById('diagerr').textContent=''}catch(e){tc.err(document.getElementById('diagerr'),e);document.getElementById('diagUpdated').textContent=''}}refresh();setInterval(refresh,30000)</script>" UI_FOOT);
}


/* Per-device brightness schedule. All values are per clock, never synced as backups. */
static void clock_dim_read(int fd,const char *clock){
 sqlite3_stmt *q=NULL;int en=1,start=1140,end=420,normal=127,dim=24;char font[80]="DejaVuSansMono.ttf";
 if(sqlite3_prepare_v2(db,"SELECT enabled,start_min,end_min,normal_contrast,dim_contrast,font FROM clock_dim_settings WHERE clock_id=?1",-1,&q,NULL)==SQLITE_OK){
  sqlite3_bind_text(q,1,clock,-1,SQLITE_TRANSIENT);
  if(sqlite3_step(q)==SQLITE_ROW){en=sqlite3_column_int(q,0);start=sqlite3_column_int(q,1);end=sqlite3_column_int(q,2);normal=sqlite3_column_int(q,3);dim=sqlite3_column_int(q,4);const unsigned char *f=sqlite3_column_text(q,5);if(f&&strlen((const char*)f)<sizeof font)snprintf(font,sizeof font,"%s",f);}
 }sqlite3_finalize(q);
 char out[360];snprintf(out,sizeof out,"{\"enabled\":%d,\"start_min\":%d,\"end_min\":%d,\"normal_contrast\":%d,\"dim_contrast\":%d,\"font\":\"%s\"}",en,start,end,normal,dim,font);
 send_response(fd,200,"application/json",out);
}
static void clock_dim_admin_get(int fd,const char *path){
 const char *p=strstr(path,"?clock_id=");if(!p||!valid_id(p+10,48)){send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
 sqlite3_stmt *q=NULL;int found=0;
 if(sqlite3_prepare_v2(db,"SELECT 1 FROM registered_clocks WHERE clock_id=?1 AND active=1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,p+10,-1,SQLITE_TRANSIENT);found=sqlite3_step(q)==SQLITE_ROW;}sqlite3_finalize(q);
 if(!found){send_response(fd,404,"application/json","{\"error\":\"This clock is not registered.\"}");return;}clock_dim_read(fd,p+10);
}
static void clock_dim_admin_save(int fd,const char *body){
 char clock[49],e[8],a[12],b[12],n[12],d[12],font[80],fontfile[256];
 if(!form_value(body,"clock_id",clock,sizeof clock)||!valid_id(clock,48)||!form_value(body,"enabled",e,sizeof e)||!form_value(body,"start_min",a,sizeof a)||!form_value(body,"end_min",b,sizeof b)||!form_value(body,"normal_contrast",n,sizeof n)||!form_value(body,"dim_contrast",d,sizeof d)||!form_value(body,"font",font,sizeof font)||!font_path(font,fontfile,sizeof fontfile)){
  send_response(fd,400,"application/json","{\"error\":\"Check the entries and try again.\"}");return;
 }
 int enabled,begin,finish,normal,dim;char tail;
 if(sscanf(e,"%d%c",&enabled,&tail)!=1||sscanf(a,"%d%c",&begin,&tail)!=1||sscanf(b,"%d%c",&finish,&tail)!=1||sscanf(n,"%d%c",&normal,&tail)!=1||sscanf(d,"%d%c",&dim,&tail)!=1||enabled<0||enabled>1||begin<0||begin>1439||finish<0||finish>1439||begin==finish||normal<1||normal>255||dim<1||dim>255||dim>normal){send_response(fd,400,"application/json","{\"error\":\"Brightness must be 1 to 255 and both times must be valid.\"}");return;}
 sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(db,"INSERT INTO clock_dim_settings(clock_id,enabled,start_min,end_min,normal_contrast,dim_contrast,font) SELECT clock_id,?2,?3,?4,?5,?6,?7 FROM registered_clocks WHERE clock_id=?1 AND active=1 ON CONFLICT(clock_id) DO UPDATE SET enabled=excluded.enabled,start_min=excluded.start_min,end_min=excluded.end_min,normal_contrast=excluded.normal_contrast,dim_contrast=excluded.dim_contrast,font=excluded.font",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,clock,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,2,enabled);sqlite3_bind_int(q,3,begin);sqlite3_bind_int(q,4,finish);sqlite3_bind_int(q,5,normal);sqlite3_bind_int(q,6,dim);sqlite3_bind_text(q,7,font,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE&&sqlite3_changes(db)>0;}sqlite3_finalize(q);
 send_response(fd,ok?200:409,"application/json",ok?"{\"status\":\"saved\"}":"{\"error\":\"This clock is not registered.\"}");
}
static void clock_dim_page(int fd){
 send_response(fd,200,"text/html; charset=utf-8",UI_HEAD("Clock display","display")
 "<h1>Clock display</h1><p>Font and brightness for each clock's screen. Times are the clock's local time. Cards still work while the screen is dim.</p>"
 "<section><form id='dimform'><label>Clock<select id='clock_id' name='clock_id' required></select></label><label><input type='checkbox' id='enabled' checked> Dim on a schedule</label>"
 "<label>Dim start<input type='time' id='start' value='19:00' required></label><label>Dim end<input type='time' id='end' value='07:00' required></label>"
 "<label>Normal brightness (1 to 255)<input type='number' id='normal' min='1' max='255' value='127'></label><label>Dim brightness (1 to 255)<input type='number' id='dim' min='1' max='255' value='24'></label>"
 "<label>Display font<select id='font' required><option value='DejaVuSansMono.ttf'>DejaVu Sans Mono</option><option value='DejaVuSans.ttf'>DejaVu Sans</option><option value='DejaVuSerif.ttf'>DejaVu Serif</option><option value='DejaVuSansMono-Bold.ttf'>DejaVu Sans Mono Bold</option><option value='DejaVuSans-Bold.ttf'>DejaVu Sans Bold</option><option value='DejaVuSerif-Bold.ttf'>DejaVu Serif Bold</option></select></label>"
 "<div class='actions'><button type='submit'>Save clock settings</button></div><p id='result' role='status'></p></form></section>"
 "<script>const f=document.getElementById('dimform'),id=document.getElementById('clock_id'),result=document.getElementById('result');const mins=v=>{let a=v.split(':').map(Number);return a[0]*60+a[1]};const hhmm=v=>String(Math.floor(v/60)).padStart(2,'0')+':'+String(v%60).padStart(2,'0');"
 "async function load(){if(!id.value)return;try{let r=await tc.request('/api/v1/admin/clock-dim?clock_id='+encodeURIComponent(id.value));let j=await tc.json(r);if(!r.ok)throw Error(j.error||'Unable to load');document.getElementById('enabled').checked=!!j.enabled;document.getElementById('start').value=hhmm(j.start_min);document.getElementById('end').value=hhmm(j.end_min);document.getElementById('normal').value=j.normal_contrast;document.getElementById('dim').value=j.dim_contrast;document.getElementById('font').value=j.font;result.textContent=''}catch(e){tc.err(result,e)}}"
 "async function init(){try{let r=await tc.request('/api/v1/admin/adoption/list');let j=await tc.json(r);if(!r.ok)throw Error('Clock list unavailable');for(let x of j.clocks.filter(x=>x.active)){let o=document.createElement('option');o.value=x.clock_id;o.textContent=(x.name||x.clock_id)+' ('+x.clock_id+')';id.append(o)}await load()}catch(e){tc.err(result,e)}}"
 "id.onchange=load;f.onsubmit=async ev=>{ev.preventDefault();let data=new URLSearchParams({clock_id:id.value,enabled:document.getElementById('enabled').checked?'1':'0',start_min:String(mins(document.getElementById('start').value)),end_min:String(mins(document.getElementById('end').value)),normal_contrast:document.getElementById('normal').value,dim_contrast:document.getElementById('dim').value,font:document.getElementById('font').value});try{let r=await tc.request('/api/v1/admin/clock-dim',{method:'POST',body:data});let j=await tc.json(r);if(!r.ok)throw Error(j.error||'Save failed');tc.ok(result,'Saved. Online clocks apply this within 30 seconds; offline clocks when they reconnect.')}catch(e){tc.err(result,e)}};init()</script>" UI_FOOT);
}
/* Manual adoption: ephemeral code + request ID, manager approval, one-time key delivery.
   Codes and requests expire; a rejected/expired request cannot be redeemed. */
static void adoption_page(int fd){
 send_response(fd,200,"text/html; charset=utf-8",UI_HEAD("Clock management","clocks")
 "<h1>Clock management</h1><p id='clkbanner' role='alert' hidden></p>"
 "<section><h2>Clocks</h2><table class='clocks-table'><thead><tr><th>Name</th><th>Clock ID</th><th>Role</th><th>Status</th><th></th></tr></thead><tbody id='adopted'><tr><td colspan='5'>Loading...</td></tr></tbody></table><p id='notice' role='status'></p><div class='actions'><button id='refresh' type='button'>Refresh</button><span id='updated' class='muted'></span></div></section>"
 "<section><h2>Add a Secondary clock</h2><p>Generate a code, enter it on the new clock's setup page, then approve the request below. The code expires after 10 minutes.</p>"
 "<div class='actions'><button id='create' type='button'>Generate code</button></div><p id='code' role='status'></p><h3>Waiting for approval</h3><div id='requests'><p>Loading...</p></div></section>"
 "<script>const notice=document.getElementById('notice'),create=document.getElementById('create');let busy=false,loading=false;"
 "async function call(url,data){const c=new AbortController(),t=setTimeout(()=>c.abort(),8000);try{const r=await tc.request(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data),signal:c.signal});const j=await tc.json(r);if(!r.ok)throw Error(j.error||'Request failed');return j}finally{clearTimeout(t)}}"
 "function msg(v){notice.textContent=v}function fail(v,e){tc.err(notice,v+': '+e.message)}function cell(tr,v){const td=document.createElement('td');td.append(v);tr.append(td);return td}"
 "async function refresh(){if(loading||document.hidden)return;loading=true;const c=new AbortController(),t=setTimeout(()=>c.abort(),8000);try{const r=await tc.request('/api/v1/admin/adoption/list',{cache:'no-store',signal:c.signal});if(!r.ok)throw Error('Clock list unavailable ('+r.status+')');const j=await tc.json(r),p=document.getElementById('requests'),a=document.getElementById('adopted');p.replaceChildren();a.replaceChildren();"
 "if(!j.pending.length)p.textContent='None.';for(const x of j.pending){const d=document.createElement('div'),name=document.createElement('span'),actions=document.createElement('div'),b=document.createElement('button');d.className='clock-entry';actions.className='actions';name.append(tc.flag('Waiting','check'),' '+x.clock_id);d.append(name);b.textContent='Approve';b.disabled=busy;b.onclick=async()=>{if(busy||!confirm('Approve '+x.clock_id+' as a Secondary clock?'))return;busy=true;b.disabled=true;msg('Approving '+x.clock_id+'...');try{await call('/api/v1/admin/adoption/approve',{id:x.id});tc.ok(notice,'Approved '+x.clock_id+'.');await refreshLater()}catch(e){fail('Approval failed',e)}finally{busy=false;b.disabled=false}};actions.append(b);d.append(actions);p.append(d)}"
 "let offline=0;if(!j.clocks.length){const tr=document.createElement('tr');cell(tr,'').colSpan=5;tr.firstChild.append(tc.flag('Problem','problem'),' No clocks registered.');a.append(tr)}"
 "for(const x of j.clocks){const tr=document.createElement('tr'),st=document.createElement('span');let level='';"
 "if(!x.active){st.append(tc.flag('Revoked','muted'))}else if(!x.last_contact_epoch){st.append(tc.flag('No contact','problem'));level='problem'}else if(x.contact_age_seconds>=180){st.append(tc.flag('Offline','problem'),' last contact '+tc.ago(x.contact_age_seconds)+' ago');level='problem'}else{st.append(tc.flag('Online','ok'),' last contact '+tc.ago(x.contact_age_seconds)+' ago')}"
 "if(level){tr.className='row-'+level;offline++}cell(tr,x.name||x.clock_id);cell(tr,x.clock_id);cell(tr,x.is_master?'Primary':'Secondary');cell(tr,st);"
 "const actions=cell(tr,''),rename=document.createElement('button'),b=document.createElement('button');rename.type='button';rename.className='secondary';rename.textContent='Rename';rename.disabled=!x.active||busy;rename.onclick=async()=>{if(busy)return;const value=prompt('Name for '+x.clock_id,x.name||x.clock_id);if(value===null)return;busy=true;msg('Saving name...');try{await call('/api/v1/admin/clock-name',{clock_id:x.clock_id,name:value});tc.ok(notice,'Name saved.');await refreshLater()}catch(e){fail('Rename failed',e)}finally{busy=false}};actions.append(rename);"
 "if(!x.is_master&&x.active){b.type='button';b.className='secondary';b.textContent='Revoke';b.disabled=busy;b.onclick=async()=>{if(busy||!confirm('Revoke '+(x.name||x.clock_id)+'? The clock can no longer send punches. Punches still stored on it are not transferred.'))return;busy=true;b.disabled=true;msg('Revoking '+x.clock_id+'...');try{await call('/api/v1/admin/adoption/remove',{clock_id:x.clock_id});tc.ok(notice,'Revoked '+x.clock_id+'.');await refreshLater()}catch(e){fail('Revoke failed',e)}finally{busy=false;b.disabled=false}};actions.append(' ',b)}a.append(tr)}"
 "tc.banner(document.getElementById('clkbanner'),'problem','Problem',offline?offline+(offline===1?' clock is not reporting. Punches on it are held until it reconnects.':' clocks are not reporting. Punches on them are held until they reconnect.'):'');"
 "document.getElementById('updated').textContent='Updated '+tc.time(new Date())}catch(e){fail('Refresh failed',e)}finally{clearTimeout(t);loading=false}}"
 "async function refreshLater(){loading=false;await refresh()}"
 "create.onclick=async()=>{if(busy)return;busy=true;create.disabled=true;msg('Generating code...');try{const r=await call('/api/v1/admin/adoption/create',{});document.getElementById('code').textContent='Code: '+r.code+' (expires in 10 minutes)';msg('Enter the code on the new clock.')}catch(e){fail('Code not generated',e)}finally{busy=false;create.disabled=false}};"
 "document.getElementById('refresh').onclick=refresh;document.addEventListener('visibilitychange',()=>{if(!document.hidden)refresh()});refresh();setInterval(refresh,15000)</script>" UI_FOOT);
}
static void adoption_create(int fd,int manager){
 char code[25];if(!random_hex(code,12)){send_response(fd,500,"application/json","{\"error\":\"Could not create a code. Try again.\"}");return;}
 sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(db,"INSERT INTO adoption_codes(code,created_by,expires_at) VALUES(?1,?2,CAST(strftime('%s','now') AS INTEGER)+600)",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,code,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,2,manager);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 char out[100];snprintf(out,sizeof out,"{\"code\":\"%s\"}",code);send_response(fd,ok?200:500,"application/json",ok?out:"{\"error\":\"The clock could not complete this. Try again.\"}");
}
static int clock_name_utf8_count(const char *s){
 const unsigned char *p=(const unsigned char*)s;int count=0;
 while(*p){
  unsigned c=*p,code=0;int len=0;
  if(c<0x80){code=c;len=1;}
  else if(c>=0xC2&&c<=0xDF){code=c&0x1F;len=2;}
  else if(c>=0xE0&&c<=0xEF){code=c&0x0F;len=3;}
  else if(c>=0xF0&&c<=0xF4){code=c&7;len=4;}
  else return -1;
  for(int i=1;i<len;i++){
   unsigned b=p[i];if(!b||(b&0xC0)!=0x80)return -1;
   code=(code<<6)|(b&0x3F);
  }
  if((len==3&&code<0x800)||(len==4&&code<0x10000)||
     (code>=0xD800&&code<=0xDFFF)||code>0x10FFFF)return -1;
  p+=len;if(++count>60)return -1;
 }
 return count;
}
static void clock_name_save(int fd,const char*body){
 char clock[49],name[256];
 if(!form_value(body,"clock_id",clock,sizeof clock)||!valid_id(clock,48)||!form_value(body,"name",name,sizeof name)){
  send_response(fd,400,"application/json","{\"error\":\"Enter a name of 1 to 60 characters.\"}");return;
 }
 size_t n=strlen(name);
 if(clock_name_utf8_count(name)<1||name[0]==' '||name[n-1]==' '){send_response(fd,400,"application/json","{\"error\":\"Enter a name of 1 to 60 characters with no leading or trailing spaces.\"}");return;}
 for(size_t i=0;i<n;i++)if((unsigned char)name[i]<32||(unsigned char)name[i]==127){send_response(fd,400,"application/json","{\"error\":\"Enter a name of 1 to 60 characters.\"}");return;}
 sqlite3_stmt*q=NULL;
 int ok=sqlite3_prepare_v2(db,"INSERT INTO clock_names(clock_id,name) SELECT clock_id,?2 FROM registered_clocks WHERE clock_id=?1 AND active=1 ON CONFLICT(clock_id) DO UPDATE SET name=excluded.name",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,clock,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,name,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE&&sqlite3_changes(db)>0;}
 sqlite3_finalize(q);
 send_response(fd,ok?200:409,"application/json",ok?"{\"status\":\"saved\"}":"{\"error\":\"Clock unavailable\"}");
}
static void adoption_list(int fd){
 char out[15000];size_t n=0;n+=(size_t)snprintf(out+n,sizeof out-n,"{\"pending\":[");sqlite3_stmt*q=NULL;
 if(sqlite3_prepare_v2(db,"SELECT r.id,r.clock_id FROM adoption_requests r JOIN adoption_codes c ON c.code=r.code WHERE r.status='pending' AND c.expires_at>CAST(strftime('%s','now') AS INTEGER) ORDER BY r.created_at DESC LIMIT 40",-1,&q,NULL)==SQLITE_OK){int first=1;while(sqlite3_step(q)==SQLITE_ROW&&n+180<sizeof out){n+=(size_t)snprintf(out+n,sizeof out-n,"%s{\"id\":\"%s\",\"clock_id\":\"%s\"}",first?"":",",sqlite3_column_text(q,0),sqlite3_column_text(q,1));first=0;}}sqlite3_finalize(q);
 n+=(size_t)snprintf(out+n,sizeof out-n,"],\"clocks\":[");q=NULL;
 if(sqlite3_prepare_v2(db,"SELECT r.clock_id,r.active,COALESCE(CAST(strftime('%s',p.updated_at) AS INTEGER),0),CAST(strftime('%s','now') AS INTEGER)-COALESCE(CAST(strftime('%s',p.updated_at) AS INTEGER),0),json_quote(COALESCE(n.name,r.clock_id)),(r.device_key=?1) FROM registered_clocks r LEFT JOIN oled_previews p ON p.clock_id=r.clock_id LEFT JOIN clock_names n ON n.clock_id=r.clock_id ORDER BY r.clock_id LIMIT 100",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,device_key,-1,SQLITE_TRANSIENT);int first=1;while(sqlite3_step(q)==SQLITE_ROW&&n+520<sizeof out){n+=(size_t)snprintf(out+n,sizeof out-n,"%s{\"clock_id\":\"%s\",\"active\":%d,\"last_contact_epoch\":%lld,\"contact_age_seconds\":%lld,\"name\":%s,\"is_master\":%s}",first?"":",",sqlite3_column_text(q,0),sqlite3_column_int(q,1),(long long)sqlite3_column_int64(q,2),(long long)sqlite3_column_int64(q,3),sqlite3_column_text(q,4),sqlite3_column_int(q,5)?"true":"false");first=0;}}sqlite3_finalize(q);
 snprintf(out+n,sizeof out-n,"]}");send_response(fd,200,"application/json",out);
}
static void adoption_request(int fd,const char*body){
 char code[30],clock[49],id[65];if(!form_value(body,"code",code,sizeof code)||!form_value(body,"clock_id",clock,sizeof clock)||strlen(code)!=24||!valid_id(clock,48)||!random_hex(id,24)){send_response(fd,400,"application/json","{\"error\":\"Invalid adoption details\"}");return;}
 sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(db,"INSERT INTO adoption_requests(id,code,clock_id,created_at) SELECT ?1,code,?3,CAST(strftime('%s','now') AS INTEGER) FROM adoption_codes WHERE code=?2 AND used=0 AND expires_at>CAST(strftime('%s','now') AS INTEGER) AND NOT EXISTS(SELECT 1 FROM registered_clocks WHERE clock_id=?3 AND active=1)",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,id,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,code,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,clock,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE&&sqlite3_changes(db)==1;}sqlite3_finalize(q);
 char out[110];snprintf(out,sizeof out,"{\"request_id\":\"%s\",\"status\":\"pending\"}",id);send_response(fd,ok?200:403,"application/json",ok?out:"{\"error\":\"The code has expired or was already used. Generate a new code on the Primary.\"}");
}
static void adoption_approve(int fd,const char*body){
 char id[65],key[65];if(!form_value(body,"id",id,sizeof id)||strlen(id)!=48||!random_hex(key,32)){send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
 sqlite3_stmt*q=NULL;int ok=sql("SAVEPOINT adoption_approve");char code[30]="",clock[49]="";
 if(ok&&sqlite3_prepare_v2(db,"SELECT r.code,r.clock_id FROM adoption_requests r JOIN adoption_codes c ON c.code=r.code WHERE r.id=?1 AND r.status='pending' AND c.used=0 AND c.expires_at>CAST(strftime('%s','now') AS INTEGER)",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,id,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW){snprintf(code,sizeof code,"%s",sqlite3_column_text(q,0));snprintf(clock,sizeof clock,"%s",sqlite3_column_text(q,1));}}sqlite3_finalize(q);q=NULL;ok=ok&&code[0];
 if(ok&&sqlite3_prepare_v2(db,"INSERT INTO registered_clocks(clock_id,device_key,active) VALUES(?1,?2,1) ON CONFLICT(clock_id) DO UPDATE SET device_key=excluded.device_key,active=1 WHERE active=0",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,clock,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,key,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE&&sqlite3_changes(db)==1;}else ok=0;sqlite3_finalize(q);q=NULL;
 if(ok&&sqlite3_prepare_v2(db,"UPDATE adoption_requests SET status='approved',device_key=?2 WHERE id=?1 AND status='pending'",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,id,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,key,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE&&sqlite3_changes(db)==1;}else ok=0;sqlite3_finalize(q);q=NULL;
 if(ok&&sqlite3_prepare_v2(db,"UPDATE adoption_codes SET used=1 WHERE code=?1 AND used=0",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,code,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE&&sqlite3_changes(db)==1;}else ok=0;sqlite3_finalize(q);
 if(ok)ok=sql("RELEASE adoption_approve");else{sql("ROLLBACK TO adoption_approve");sql("RELEASE adoption_approve");}
 send_response(fd,ok?200:409,"application/json",ok?"{\"status\":\"approved\"}":"{\"error\":\"Could not approve. The code may have expired; generate a new one.\"}");
}
static void adoption_poll(int fd,const char*body){
 char id[65];if(!form_value(body,"request_id",id,sizeof id)||strlen(id)!=48){send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
 sqlite3_stmt*q=NULL;char out[250]="{\"status\":\"expired\"}";
 if(sqlite3_prepare_v2(db,"SELECT r.status,r.device_key,r.clock_id,c.expires_at FROM adoption_requests r JOIN adoption_codes c ON c.code=r.code WHERE r.id=?1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,id,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW){const char*status=(const char*)sqlite3_column_text(q,0);const char*key=(const char*)sqlite3_column_text(q,1);const char*clock=(const char*)sqlite3_column_text(q,2);if(!strcmp(status,"approved")){snprintf(out,sizeof out,"{\"status\":\"approved\",\"clock_id\":\"%s\",\"device_key\":\"%s\"}",clock,key);}else if(sqlite3_column_int64(q,3)>time(NULL))snprintf(out,sizeof out,"{\"status\":\"pending\"}");}}sqlite3_finalize(q);
 /* A request token can retrieve the credential once only. */
 if(strstr(out,"\"status\":\"approved\"")){
  sqlite3_stmt*clear=NULL;
  if(sqlite3_prepare_v2(db,"UPDATE adoption_requests SET status='delivered',device_key='' WHERE id=?1 AND status='approved'",-1,&clear,NULL)==SQLITE_OK){
   sqlite3_bind_text(clear,1,id,-1,SQLITE_TRANSIENT);
   if(sqlite3_step(clear)!=SQLITE_DONE||sqlite3_changes(db)!=1)snprintf(out,sizeof out,"{\"status\":\"expired\"}");
  }else snprintf(out,sizeof out,"{\"status\":\"expired\"}");
  sqlite3_finalize(clear);
 }
 send_response(fd,200,"application/json",out);
}
static void adoption_remove(int fd,const char*body){char clock[49];if(!form_value(body,"clock_id",clock,sizeof clock)||!valid_id(clock,48)){send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(db,"UPDATE registered_clocks SET active=0 WHERE clock_id=?1 AND active=1 AND device_key<>?2",-1,&q,NULL)==SQLITE_OK;if(ok){sqlite3_bind_text(q,1,clock,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,device_key,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE&&sqlite3_changes(db)==1;}sqlite3_finalize(q);send_response(fd,ok?200:409,"application/json",ok?"{\"status\":\"removed\"}":"{\"error\":\"Could not revoke this clock.\"}");}

/* A clock key is valid for exactly one active registered clock. */
/* Case-insensitive HTTP header lookup using POSIX strncasecmp().
   Match only at the start of a header line, never inside a value. */
static const char *clock_header_value(const char *headers,const char *name){
 size_t n=strlen(name);
 const char *line=headers;
 while(line&&*line){
  if(!strncasecmp(line,name,n)&&line[n]==':')return line+n+1;
  line=strchr(line,'\n');
  if(line)line++;
 }
 return NULL;
}
static int clock_auth(const char *headers,char clock[49]){
 const char *p=clock_header_value(headers,"X-Clock-ID");
 if(!p)return 0;
 while(*p==' '||*p=='\t')p++;
 size_t len=strcspn(p,"\r\n");if(len==0||len>=49)return 0;
 memcpy(clock,p,len);clock[len]=0;if(!valid_id(clock,48))return 0;
 p=clock_header_value(headers,"X-Device-Key");if(!p)return 0;
 while(*p==' '||*p=='\t')p++;
 len=strcspn(p,"\r\n");
 if(len<16||len>128)return 0;
 sqlite3_stmt*q=NULL;int ok=0;
 if(sqlite3_prepare_v2(db,"SELECT device_key FROM registered_clocks WHERE clock_id=?1 AND active=1",-1,&q,NULL)==SQLITE_OK){
  sqlite3_bind_text(q,1,clock,-1,SQLITE_TRANSIENT);
  if(sqlite3_step(q)==SQLITE_ROW){const char*k=(const char*)sqlite3_column_text(q,0);if(k&&strlen(k)==len){unsigned char d=0;for(size_t i=0;i<len;i++)d|=(unsigned char)(k[i]^p[i]);ok=d==0;}}
 }sqlite3_finalize(q);
 return ok;
}
static void clock_scan(int fd,const char *body){
 char uid[65];if(!json_string(body,"card_uid",uid,sizeof uid)||!valid_id(uid,64)){send_response(fd,400,"application/json","{\"error\":\"Enter a card UID of 8 to 20 hexadecimal characters.\"}");return;}
 sqlite3_stmt*q=NULL;int manager=0,enrolled=0,approved=0;
 if(sqlite3_prepare_v2(db,"SELECT 1 FROM manager_cards WHERE uid=?1 COLLATE NOCASE AND active=1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,uid,-1,SQLITE_TRANSIENT);manager=sqlite3_step(q)==SQLITE_ROW;}sqlite3_finalize(q);
 time_t now=time(NULL);q=NULL;
 if(sqlite3_prepare_v2(db,"SELECT armed_at,scan_mode FROM manager_scan WHERE id=1 AND armed_at>0 AND scanned_at<armed_at",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW){
  time_t armed=(time_t)sqlite3_column_int64(q,0);const char *mode=(const char*)sqlite3_column_text(q,1);
  if(now>=armed&&now-armed<=30 && (mode&&(!strcmp(mode,"enrollment")||!strcmp(mode,"bootstrap")||(!strcmp(mode,"approval")&&manager)))){
   sqlite3_stmt*set=NULL;
   if(sqlite3_prepare_v2(db,"UPDATE manager_scan SET uid=?1,scanned_at=?2 WHERE id=1 AND armed_at=?3 AND scanned_at<armed_at",-1,&set,NULL)==SQLITE_OK){sqlite3_bind_text(set,1,uid,-1,SQLITE_TRANSIENT);sqlite3_bind_int64(set,2,(sqlite3_int64)now);sqlite3_bind_int64(set,3,(sqlite3_int64)armed);int recorded=sqlite3_step(set)==SQLITE_DONE&&sqlite3_changes(db)==1;if(recorded){approved=mode&&(!strcmp(mode,"approval")||!strcmp(mode,"bootstrap"));enrolled=!approved;}}sqlite3_finalize(set);
  }
 }sqlite3_finalize(q);
 send_response(fd,200,"application/json",approved?"{\"result\":\"APPROVED\"}":enrolled?"{\"result\":\"ENROLLED\"}":manager?"{\"result\":\"MANAGER\"}":"{\"result\":\"NORMAL\"}");
}
static void serve(int fd){static char buf[FONT_MAX+8192];int n=read_request(fd,buf,sizeof buf);if(n<0){send_response(fd,400,"text/plain","Bad request");return;}(void)n;
 char method[10],path[200];if(sscanf(buf,"%9s %199s",method,path)!=2){send_response(fd,400,"text/plain","Bad request");return;}
 if(!strcmp(method,"GET")&&!strncmp(path,"/api/v1/preview?clock_id=",25)){if(!session_user(buf)){send_response(fd,401,"text/plain","Login required");return;}preview_read(fd,path);return;}
 if(!strcmp(method,"GET")&&!strncmp(path,"/ui.css",7)&&(!path[7]||path[7]=='?')){send_css(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/setup")){bootstrap_page(fd);return;}
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/bootstrap/arm")){char*b=strstr(buf,"\r\n\r\n");if(b)bootstrap_arm(fd,b+4);else send_response(fd,400,"text/plain","Bad request");return;}
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/bootstrap/status")){char*b=strstr(buf,"\r\n\r\n");if(b)bootstrap_status(fd,b+4);else send_response(fd,400,"text/plain","Bad request");return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/login")){login_page(fd);return;}
 if(!strcmp(path,"/employee/login")||!strcmp(path,"/my-timesheet")){send_response(fd,410,"text/html; charset=utf-8",UI_HEAD_LOGIN("Employee sign-in")"<h1>Employee sign-in</h1><p>Employees do not sign in. Use the timesheet link your manager sends you.</p>" UI_FOOT);return;}
 
 if(!strncmp(path,"/review/",8)){review_route(fd,method,path,buf);return;}
 if((!strcmp(path,"/time-off")||!strncmp(path,"/time-off/",10))){timeoff_route(fd,method,path,buf);return;}
 if(!strncmp(path,"/api/v1/me/",11)){send_response(fd,410,"text/plain","Employees do not sign in. Use the timesheet link your manager sends you.");return;}
 if(!strcmp(method,"POST")&&!strcmp(path,"/login")){char*b=strstr(buf,"\r\n\r\n");if(b)login_post_role(fd,b+4);else send_response(fd,400,"text/plain","Bad request");return;}
 if(!strcmp(method,"POST")&&!strcmp(path,"/logout")){
  
  /* Revoke every current session for the authenticated manager. */
  int who=session_user(buf);if(who){sqlite3_stmt*q=NULL;if(sqlite3_prepare_v2(db,"DELETE FROM login_sessions WHERE employee_id=?1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,who);sqlite3_step(q);}sqlite3_finalize(q);}
  send_cookie_response(fd,303,"","/login");return;}
 if(!strncmp(path,"/n/",3)){note_route(fd,method,path,buf);return;}
 if(!strcmp(method,"POST")&&(!strcmp(path,"/api/v1/adoption/request")||!strcmp(path,"/api/v1/adoption/poll"))){char*b=strstr(buf,"\r\n\r\n");if(!b){send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}if(!strcmp(path,"/api/v1/adoption/request"))adoption_request(fd,b+4);else adoption_poll(fd,b+4);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/health")){send_response(fd,200,"application/json","{\"status\":\"ok\"}");return;}
 if((!strcmp(path,"/")||!strcmp(path,"/employees")||!strcmp(path,"/email-templates")||!strcmp(path,"/smtp")||!strcmp(path,"/tls")||!strcmp(path,"/ntp")||!strcmp(path,"/system")||!strcmp(path,"/system?backup=1")||!strcmp(path,"/device-health")||!strcmp(path,"/backups")||!strcmp(path,"/diagnostics")||!strcmp(path,"/audit")||!strcmp(path,"/display")||!strcmp(path,"/holidays")||!strcmp(path,"/manager-cards")||!strcmp(path,"/schedules")||!strcmp(path,"/clock-grace")||!strcmp(path,"/payroll")||!strcmp(path,"/attendance-export")||!strcmp(path,"/email-queue")||!strcmp(path,"/rejected-cards")||!strcmp(path,"/rejected-uploads")||!strcmp(path,"/card-status")||!strcmp(path,"/swipe-notes")||!strcmp(path,"/punch-corrections")||!strcmp(path,"/presence")||!strcmp(path,"/clocks")||!strcmp(path,"/clock-dim")||!strcmp(path,"/pay-periods")||!strcmp(path,"/time-off-requests")||!strcmp(path,"/accounts")||!strcmp(path,"/review-links")||!strncmp(path,"/api/v1/admin/",14))&&!session_user(buf)){
  if(!strcmp(method,"GET")&&!strncmp(path,"/api/",5))send_response(fd,401,"application/json","{\"error\":\"Sign in again.\"}");
  else if(!strcmp(method,"GET"))send_cookie_response(fd,303,"","/login");
  else send_response(fd,401,"application/json","{\"error\":\"Sign in again.\"}");
  return;
 }
 
 /* Manager bootstrap: force a new signed-in manager to register a physical card
    before using administration. Existing configured installations are unaffected. */
 if(session_user(buf)&&manager_card_setup_needed()&&
    strcmp(path,"/manager-cards")&&strcmp(path,"/logout")&&strcmp(path,"/api/v1/admin/manager-cards")&&
    strcmp(path,"/api/v1/admin/manager-scan/arm")&&
    strcmp(path,"/api/v1/admin/manager-scan/status")){
  if(!strcmp(method,"GET")&&strncmp(path,"/api/",5))send_cookie_response(fd,303,"","/manager-cards");
  else send_response(fd,409,"application/json","{\"error\":\"Register a manager card before continuing.\"}");
  return;
 }

 if(!strcmp(method,"GET")&&!strcmp(path,"/api/v1/admin/backups/restore/result")){backup_restore_result(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/api/v1/admin/backups/upload/status")){backup_upload_status(fd);return;}
 if(!strcmp(method,"POST")&&!strncmp(path,"/api/v1/admin/backups/upload?name=",34)){backup_upload_file(fd,buf,(size_t)n,path+34);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/api/v1/admin/issues")){issues_json(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/api/v1/admin/backups/list")){backups_list(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/api/v1/admin/backups/settings")){backup_settings_get(fd);return;}
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/backups/settings")){char*b=strstr(buf,"\r\n\r\n");if(b)backup_settings_save(fd,b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/backups/restore/arm")){char*b=strstr(buf,"\r\n\r\n");if(b)backup_restore_arm(fd,session_user(buf),b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/backups/download/arm")){char*b=strstr(buf,"\r\n\r\n");if(b)backup_download_arm(fd,session_user(buf),b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
 if(!strcmp(method,"GET")&&!strncmp(path,"/api/v1/admin/backups/download?token=",37)){backup_download_file(fd,session_user(buf),path+37);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/clocks")){adoption_page(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/clock-dim")){send_redirect(fd,"/display");return;}
 if(!strncmp(path,"/api/v1/admin/clock-dim",23)){if(!strcmp(method,"GET"))clock_dim_admin_get(fd,path);else if(!strcmp(method,"POST")){char*b=strstr(buf,"\r\n\r\n");if(b)clock_dim_admin_save(fd,b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");}return;}
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/clock-name")){char*b=strstr(buf,"\r\n\r\n");if(b)clock_name_save(fd,b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
 if(!strncmp(path,"/api/v1/admin/adoption/",23)){
  if(!strcmp(method,"GET")&&!strcmp(path,"/api/v1/admin/adoption/list")){adoption_list(fd);return;}
  if(!strcmp(method,"POST")){char*b=strstr(buf,"\r\n\r\n");if(!b){send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}b+=4;
   if(!strcmp(path,"/api/v1/admin/adoption/create"))adoption_create(fd,session_user(buf));
   else if(!strcmp(path,"/api/v1/admin/adoption/approve"))adoption_approve(fd,b);
   else if(!strcmp(path,"/api/v1/admin/adoption/remove"))adoption_remove(fd,b);
   else {send_response(fd,404,"application/json","{\"error\":\"not found\"}");}return;
  }send_response(fd,405,"text/plain","Method not allowed");return;
 }
 if(!strcmp(method,"GET")&&!strcmp(path,"/clock-grace")){clock_grace_page(fd);return;}
 if(!strcmp(path,"/api/v1/admin/clock-duplicate")){if(!strcmp(method,"GET"))clock_duplicate_get(fd);else if(!strcmp(method,"POST")){char*b=strstr(buf,"\r\n\r\n");if(b)clock_duplicate_save(fd,b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");}else send_response(fd,405,"text/plain","Method not allowed");return;}
 if(!strcmp(path,"/api/v1/admin/clock-grace")){if(!strcmp(method,"GET"))clock_grace_get(fd);else if(!strcmp(method,"POST")){char*b=strstr(buf,"\r\n\r\n");if(b)clock_grace_save(fd,b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");}return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/punch-corrections")){rec_page(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/presence")){rec_presence_page(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/pay-periods")){rec_period_page(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/time-off-requests")){timeoff_manager_page(fd);return;}
 if(!strcmp(method,"GET")&&!strncmp(path,"/api/v1/admin/time-off/annual.csv?",34)){timeoff_annual_export(fd,path);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/api/v1/admin/time-off")){timeoff_manager_list(fd);return;}
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/time-off/decide")){const char*b=strstr(buf,"\r\n\r\n");if(b)timeoff_manager_decide(fd,session_user(buf),b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete.\"}");return;}
 if(!strcmp(path,"/api/v1/admin/absences")){if(!strcmp(method,"GET"))rec_absences(fd);else if(!strcmp(method,"POST")){char*b=strstr(buf,"\r\n\r\n");if(b)rec_absence_save(fd,session_user(buf),b+4);else rec_error(fd,400,"Invalid request.");}return;}
 if(!strcmp(path,"/api/v1/admin/punches")){if(!strcmp(method,"GET"))rec_events(fd);else if(!strcmp(method,"POST")){char*b=strstr(buf,"\r\n\r\n");if(b)rec_apply(fd,session_user(buf),b+4);}return;}
 if(!strcmp(method,"GET")&&!strncmp(path,"/api/v1/admin/effective-export?",31)){rec_effective_export(fd,path);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/api/v1/admin/presence")){rec_presence(fd);return;}
 if(!strcmp(method,"GET")&&!strncmp(path,"/api/v1/admin/periods/people?",29)){rec_period_people(fd,path+29);return;}
 if(!strcmp(path,"/api/v1/admin/periods/cycle")){if(!strcmp(method,"GET"))rec_cycle_get(fd);else if(!strcmp(method,"POST")){char*b=strstr(buf,"\r\n\r\n");if(b)rec_cycle_save(fd,session_user(buf),b+4);}return;}
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/periods/send-report")){char*b=strstr(buf,"\r\n\r\n");if(b)rec_period_send_report(fd,session_user(buf),b+4);else send_response(fd,400,"application/json","{\"error\":\"Missing request\"}");return;}
 if(!strcmp(method,"GET")&&!strncmp(path,"/api/v1/admin/periods/exports?",30)){lite_export_list(fd,path+30);return;}
 if(!strcmp(method,"GET")&&!strncmp(path,"/api/v1/admin/periods/export.xlsx?",34)){
  /* One converter worker at a time. The listening process never waits for
     conversion, so an Excel download cannot delay a clock's punch upload. */
  int gate=open("/tmp/mk-timeclock-lite-xlsx.lock",O_CREAT|O_RDWR|O_CLOEXEC,0600);
  if(gate<0){rec_error(fd,503,"Export worker unavailable.");return;}
  if(flock(gate,LOCK_EX|LOCK_NB)<0){close(gate);rec_error(fd,503,"An Excel export is in progress. Try again.");return;}
  const char *dbpath=sqlite3_db_filename(db,"main");
  char *worker_db=dbpath?strdup(dbpath):NULL;
  if(!worker_db){close(gate);rec_error(fd,503,"Export worker unavailable.");return;}
  pid_t worker=fork();
  if(worker==0){
    /* Forked child must use its own SQLite connection. Do not close or use
       the parent's SQLite handle after fork. */
    db=NULL;
    if(download_parent_listener>=0)close(download_parent_listener);
    int opened=sqlite3_open_v2(worker_db,&db,SQLITE_OPEN_READONLY,NULL)==SQLITE_OK;
    if(opened){
      /* A wedged converter must not hold the single-download lock forever. */
      if(setpgid(0,0)!=0){rec_error(fd,503,"Cannot isolate export worker.");close(fd);close(gate);_exit(1);}
      lite_export_client_fd=fd;
      signal(SIGALRM,lite_export_timeout);
      alarm(120);
      sqlite3_busy_timeout(db,3000);lite_export_download(fd,path+34);
      alarm(0);
      sqlite3_close(db);
    }
    else rec_error(fd,503,"Cannot read saved export.");
    close(fd);close(gate);free(worker_db);_exit(opened?0:1);
  }
  close(gate);free(worker_db);
  if(worker<0)rec_error(fd,503,"Cannot start Excel export.");
  return;
 }
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/periods/exports")){char*b=strstr(buf,"\r\n\r\n");if(b)lite_export_create(fd,b+4);else rec_error(fd,400,"Missing request.");return;}
 if(!strcmp(path,"/api/v1/admin/periods")){if(!strcmp(method,"GET"))rec_periods(fd);else if(!strcmp(method,"POST")){char*b=strstr(buf,"\r\n\r\n");if(b)rec_period_action(fd,session_user(buf),b+4);}return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/")){recent_page(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/attendance-export")){attendance_export_page(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/email-queue")){email_queue_page(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/rejected-cards")){rejected_cards_page(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/rejected-uploads")){rejected_uploads_page(fd);return;}
 if(!strcmp(method,"GET")&&!strncmp(path,"/api/v1/admin/export?",21)){attendance_export(fd,session_user(buf),path);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/api/v1/admin/email-queue")){email_queue_json(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/api/v1/admin/rejected-cards")){rejected_cards_json(fd);return;}
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/rejected-cards/clear")){rejected_cards_clear(fd,session_user(buf));return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/api/v1/admin/rejected-uploads")){rejected_upload_list(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/card-status")){card_manage_page(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/swipe-notes")){send_redirect(fd,"/punch-corrections");return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/api/v1/admin/card-status")){card_list_json(fd);return;}
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/card-replace")){char*b=strstr(buf,"\r\n\r\n");if(b)card_replace(fd,session_user(buf),b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/accounts")){accounts_page(fd);return;}
 if(!strcmp(path,"/api/v1/admin/review/auto-settings")&&(!strcmp(method,"GET")||!strcmp(method,"POST"))){char*b=strstr(buf,"\r\n\r\n");review_auto_settings(fd,!strcmp(method,"POST")?(b?b+4:""):NULL);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/review-links")){review_links_page(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/api/v1/admin/accounts")){accounts_list(fd);return;}
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/accounts/change-password")){char*b=strstr(buf,"\r\n\r\n");if(b)accounts_self_password(fd,session_user(buf),b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/accounts/arm")){char*b=strstr(buf,"\r\n\r\n");if(b)accounts_arm(fd,session_user(buf),b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/accounts/status")){char*b=strstr(buf,"\r\n\r\n");if(b&&approval_token_matches(b+4))accounts_status(fd,session_user(buf));else send_response(fd,409,"application/json","{\"error\":\"This approval is no longer valid. Start again.\"}");return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/api/v1/admin/review/choices")){review_choices(fd);return;}
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/review/generate")){char*b=strstr(buf,"\r\n\r\n");if(b){char em[8]="";form_value(b+4,"send",em,sizeof em);review_generate(fd,session_user(buf),b+4,!strcmp(em,"1"));}else send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/review/revoke")){char*b=strstr(buf,"\r\n\r\n");if(b)review_revoke(fd,session_user(buf),b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/employees")){management_page(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/email-templates")){template_page(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/api/v1/admin/note-link")){note_link_settings_get(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/smtp")){smtp_config_page(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/tls")){tls_page(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/api/v1/admin/tls/status")){tls_status(fd);return;}
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/tls/upload")){char*b=strstr(buf,"\r\n\r\n");if(b)tls_upload(fd,b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/holidays")){holidays_page(fd);return;}
 if(!strcmp(path,"/api/v1/admin/holidays")){if(!strcmp(method,"GET"))holidays_get(fd);else if(!strcmp(method,"POST")){char*b=strstr(buf,"\r\n\r\n");if(b)holidays_save(fd,b+4);}return;}
 
 if(!strcmp(method,"GET")&&!strcmp(path,"/display")){clock_dim_page(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/audit")){audit_page(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/api/v1/admin/audit")){audit_json(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/diagnostics")){diagnostics_page(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/api/v1/admin/diagnostics/report")){diagnostics_json(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/api/v1/admin/diagnostics/log")){diagnostics_log(fd);return;}
 if(!strcmp(method,"GET")&&(!strcmp(path,"/system")||!strcmp(path,"/system?backup=1"))){system_page(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/device-health")){health_page(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/backups")){backups_page(fd);return;}
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/system/arm")){char*b=strstr(buf,"\r\n\r\n");if(b)system_arm(fd,session_user(buf),b+4);return;}
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/system/status")){char*b=strstr(buf,"\r\n\r\n");if(!b||!approval_token_matches(b+4)){send_response(fd,409,"application/json","{\"error\":\"This approval is no longer valid. Start again.\"}");return;}system_status(fd,session_user(buf));return;}
 if(!strcmp(path,"/api/v1/admin/timezone")){if(!strcmp(method,"GET"))timezone_get(fd);else send_response(fd,405,"application/json","{\"error\":\"This change requires a manager card.\"}");return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/ntp")){ntp_page(fd);return;}
 if(!strcmp(path,"/api/v1/admin/ntp")){if(!strcmp(method,"GET"))ntp_get(fd);else if(!strcmp(method,"POST")){char*b=strstr(buf,"\r\n\r\n");if(b)ntp_save(fd,b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");}return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/manager-cards")){manager_cards_page(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/schedules")){schedule_page(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/payroll")){payroll_page(fd);return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/api/v1/admin/payroll")){payroll_get(fd);return;}
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/payroll/arm")){char*b=strstr(buf,"\r\n\r\n");if(b)payroll_arm(fd,session_user(buf),b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/payroll/status")){char*b=strstr(buf,"\r\n\r\n");if(!b||!approval_token_matches(b+4)){send_response(fd,409,"application/json","{\"error\":\"This approval is no longer valid. Start again.\"}");return;}payroll_status(fd,session_user(buf));return;}
 if(!strcmp(method,"GET")&&!strcmp(path,"/api/v1/admin/schedules")){schedules_json(fd);return;}
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/schedules/save")){char*b=strstr(buf,"\r\n\r\n");if(b)schedule_save_direct(fd,session_user(buf),b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/schedules/arm")){char*b=strstr(buf,"\r\n\r\n");if(b)schedule_arm(fd,session_user(buf),b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/schedules/status")){char*b=strstr(buf,"\r\n\r\n");if(!b||!approval_token_matches(b+4)){send_response(fd,409,"application/json","{\"error\":\"This approval is no longer valid. Start again.\"}");return;}schedule_status(fd,session_user(buf));return;}
 if(!strcmp(path,"/api/v1/admin/manager-cards")){int who=session_user(buf);if(!who){send_response(fd,401,"application/json","{\"error\":\"Sign in again.\"}");return;}if(!strcmp(method,"GET"))manager_cards_get(fd);else if(!strcmp(method,"POST")){char*b=strstr(buf,"\r\n\r\n");if(b)manager_card_assign(fd,who,b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");}return;}
 if(!strcmp(path,"/api/v1/admin/manager-scan/arm")&&!strcmp(method,"POST")){manager_scan_arm(fd);return;}
 if(!strcmp(path,"/api/v1/admin/manager-scan/status")&&!strcmp(method,"GET")){manager_scan_status(fd);return;}
 if(!strcmp(path,"/api/v1/admin/smtp")&&(!strcmp(method,"GET")||!strcmp(method,"POST"))){
  if(!strcmp(method,"GET"))smtp_config_get(fd);else{char*b=strstr(buf,"\r\n\r\n");if(b)smtp_config_save(fd,b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");}return;
 }
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/smtp-test")){char*b=strstr(buf,"\r\n\r\n");if(b)smtp_test_enqueue(fd,b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
 if(!strcmp(method,"GET")&&!strncmp(path,"/api/v1/admin/templates/default?",32)){
  char *query=strchr(path,'?');
  if(query)template_default_get(fd,query+1);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;
 }
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/templates/restore")){
  send_response(fd,403,"application/json","{\"error\":\"This change requires a manager card.\"}");return;
 }
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/templates/restore/arm")){
  char*b=strstr(buf,"\r\n\r\n");if(b)template_restore_arm(fd,session_user(buf),b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;
 }
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/templates/restore/status")){
  char*b=strstr(buf,"\r\n\r\n");if(!b||!approval_token_matches(b+4)){send_response(fd,409,"application/json","{\"error\":\"This approval is no longer valid. Start again.\"}");return;}template_restore_status(fd,session_user(buf));return;
 }
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/email-queue/retry")){
  char*b=strstr(buf,"\r\n\r\n");if(b)email_retry(fd,session_user(buf),b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;
 }
 if(!strcmp(path,"/api/v1/admin/templates")&&(!strcmp(method,"GET")||!strcmp(method,"POST"))){
  if(!session_user(buf)){send_response(fd,401,"application/json","{\"error\":\"Sign in again.\"}");return;}
  if(!strcmp(method,"GET"))template_list(fd);else {char *body=strstr(buf,"\r\n\r\n");if(body)template_save(fd,body+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");}return;
 }
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/employees/timeoff/send")){char*b=strstr(buf,"\r\n\r\n");if(b)timeoff_manager_send_link(fd,session_user(buf),b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete.\"}");return;}
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/employees/welcome/resend")){char*b=strstr(buf,"\r\n\r\n");if(b)welcome_resend(fd,session_user(buf),b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete.\"}");return;}
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/employees/cards/revoke")){char*b=strstr(buf,"\r\n\r\n");if(b)employee_card_revoke(fd,session_user(buf),b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/employees/save")){char*b=strstr(buf,"\r\n\r\n");if(b)employee_save_routine(fd,session_user(buf),b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/employee-change/arm")){
  char*b=strstr(buf,"\r\n\r\n");if(b)employee_change_arm(fd,session_user(buf),b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;
 }
 if(!strcmp(method,"POST")&&!strcmp(path,"/api/v1/admin/employee-change/status")){
  char*b=strstr(buf,"\r\n\r\n");if(!b||!approval_token_matches(b+4)){send_response(fd,409,"application/json","{\"error\":\"This approval is no longer valid. Start again.\"}");return;}employee_change_status(fd,session_user(buf));return;
 }
 if(!strcmp(path,"/api/v1/admin/employees")&&(!strcmp(method,"GET")||!strcmp(method,"POST"))){
  if(!strcmp(method,"GET"))management_list(fd);
  else send_response(fd,403,"application/json","{\"error\":\"This change requires a manager card.\"}");
  return;
 }
 /* All hardware readers use the same per-clock authenticated API. */
 if(!strncmp(path,"/api/v1/clock/",14)||!strcmp(path,"/api/v1/swipes")){
  char authenticated_clock[49],body_clock[65]="";
  if(!clock_auth(buf,authenticated_clock)){send_response(fd,401,"application/json","{\"error\":\"unregistered clock\"}");return;}
  if(!strcmp(method,"GET")&&!strcmp(path,"/api/v1/clock/cards")){send_cards(fd);return;}
  if(!strcmp(method,"GET")&&!strcmp(path,"/api/v1/clock/dim")){clock_dim_read(fd,authenticated_clock);return;}
  if(!strcmp(method,"POST")){
   char *body=strstr(buf,"\r\n\r\n");if(!body){send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}body+=4;
   if(!((!strcmp(path,"/api/v1/clock/rejected"))?form_value(body,"clock_id",body_clock,sizeof body_clock):json_string(body,"clock_id",body_clock,sizeof body_clock))||strcmp(authenticated_clock,body_clock)){send_response(fd,403,"application/json","{\"error\":\"clock identity mismatch\"}");return;}
   if(!strcmp(path,"/api/v1/clock/scan")){clock_scan(fd,body);return;}
   if(!strcmp(path,"/api/v1/clock/unknown")){accept_unknown(fd,body);return;}
   if(!strcmp(path,"/api/v1/clock/rejected")){rejected_upload_receive(fd,body);return;}
   if(!strcmp(path,"/api/v1/clock/preview")){preview_write(fd,body);return;}
   if(!strcmp(path,"/api/v1/swipes")){accept_swipe(fd,body);return;}
  }
  send_response(fd,404,"application/json","{\"error\":\"unknown clock endpoint\"}");return;
 }
 send_response(fd,404,"application/json","{\"error\":\"not found\"}");
}
int main(int argc,char **argv){
 /* DEVICE_KEY is loaded from the environment; never expose it through process arguments. */
 if(argc!=3){fprintf(stderr,"Usage: %s DB_PATH LISTEN_PORT  (DEVICE_KEY in environment)\n",argv[0]);return 2;}
 device_key=getenv("DEVICE_KEY");
 if(!device_key){fprintf(stderr,"Set DEVICE_KEY\n");return 2;}
 if(strlen(device_key)<16){fprintf(stderr,"Device key must be at least 16 characters\n");return 2;}if(!init_db(argv[1]))return 1;
 int port=atoi(argv[2]);if(port<1||port>65535)return 2;int sock=socket(AF_INET,SOCK_STREAM,0);if(sock<0)return 1;int yes=1;setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof yes);struct sockaddr_in addr={.sin_family=AF_INET,.sin_port=htons((unsigned short)port),.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};if(bind(sock,(void *)&addr,sizeof addr)||listen(sock,16)){perror("bind/listen");return 1;}signal(SIGPIPE,SIG_IGN);signal(SIGCHLD,SIG_IGN);download_parent_listener=sock;
 struct sigaction sa;memset(&sa,0,sizeof sa);sa.sa_handler=stop_handler;
 sigemptyset(&sa.sa_mask); /* Deliberately no SA_RESTART: wake blocking accept on SIGTERM. */
 if(sigaction(SIGINT,&sa,NULL)<0||sigaction(SIGTERM,&sa,NULL)<0){perror("sigaction");close(sock);sqlite3_close(db);return 1;}fprintf(stderr,"mk-timeclock listening on %d\n",port);
 time_t last_period_check=(time_t)-1;
 while(!stop_flag){time_t period_tick=time(NULL)/60;if(period_tick!=last_period_check){rec_period_auto_due();last_period_check=period_tick;}if(stop_flag)break;review_auto_due();if(stop_flag)break;struct pollfd listener={.fd=sock,.events=POLLIN};
  /* Block termination signals while testing stop_flag. ppoll atomically
     restores the original mask during the wait, eliminating the lost wakeup. */
  sigset_t block_mask,old_mask;
  sigemptyset(&block_mask);sigaddset(&block_mask,SIGINT);sigaddset(&block_mask,SIGTERM);
  if(sigprocmask(SIG_BLOCK,&block_mask,&old_mask)<0)break;
  if(stop_flag){sigprocmask(SIG_SETMASK,&old_mask,NULL);break;}
  const struct timespec wait_time={.tv_sec=60,.tv_nsec=0};
  int pr=ppoll(&listener,1,&wait_time,&old_mask);
  int poll_errno=errno;
  if(sigprocmask(SIG_SETMASK,&old_mask,NULL)<0)break;
  if(stop_flag)break;
  if(pr<0&&poll_errno==EINTR)continue;
  if(pr<=0)continue;
  struct sockaddr_in peer;socklen_t plen=sizeof peer;int c=accept(sock,(struct sockaddr*)&peer,&plen);if(c<0){if(errno==EINTR||errno==ECONNABORTED)continue;break;}
  current_peer=(plen>=sizeof peer&&peer.sin_family==AF_INET)?ntohl(peer.sin_addr.s_addr):0;
  struct timeval tv={.tv_sec=2,.tv_usec=0};setsockopt(c,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof tv);setsockopt(c,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof tv);serve(c);close(c);}close(sock);sqlite3_close(db);return 0;}
