/* Issue summary for the menu counts and page banners (v0.9.130-r18).
   Each item is something a person must look at. Counts stay cheap: plain indexed counts,
   no effective_punches recursion, so every page load can ask for them. */
static int clock_synced(void){
 if(access("/run/systemd/timesync/synchronized",F_OK)==0)return 1;
 struct timex tx;memset(&tx,0,sizeof tx);int state=adjtimex(&tx);
 return state!=-1&&state!=TIME_ERROR&&!(tx.status&STA_UNSYNC);
}
/* Certificate end time written by timeclock-tls-apply.sh; 0 when unknown. */
static long long tls_not_after(void){
 FILE*f=fopen("/etc/timeclock/tls/not_after","r");long long v=0;
 if(f){if(fscanf(f,"%lld",&v)!=1)v=0;fclose(f);}
 return v>0?v:0;
}
static int tls_active(void){return access("/etc/timeclock/tls/active",F_OK)==0;}
/* Set by timeclock-tls-bootstrap.sh for its self-signed certificate; removed when a real one is installed. */
static int tls_temporary(void){return access("/etc/timeclock/tls/temporary",F_OK)==0;}
/* Newest local backup archive time: 0 when none exist, -1 when the directory cannot be read. */
static long long newest_backup_epoch(void){
 DIR*d=opendir("/var/backups/timeclock");if(!d)return -1;long long best=0;struct dirent*e;
 while((e=readdir(d))){size_t l=strlen(e->d_name);
  if(l<17||strncmp(e->d_name,"timeclock-",10)||strcmp(e->d_name+l-7,".tar.gz"))continue;
  char full[300];snprintf(full,sizeof full,"/var/backups/timeclock/%s",e->d_name);
  struct stat st;if(!lstat(full,&st)&&S_ISREG(st.st_mode)&&(long long)st.st_mtime>best)best=(long long)st.st_mtime;}
 closedir(d);return best;
}
static long issue_count(const char*query){
 sqlite3_stmt*q=NULL;long n=0;
 if(sqlite3_prepare_v2(db,query,-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW)n=sqlite3_column_int64(q,0);
 sqlite3_finalize(q);return n;
}
static int smtp_configured(void){return issue_count("SELECT count(*) FROM smtp_settings WHERE id=1 AND url<>'' AND sender<>''")>0;}
/* Active employees whose records stop notices or review: missing name or position, late emails
   on with no address, or an attendance card but no schedule (late and missing checks never run). */
#define EMPLOYEE_ISSUE_SQL "e.active=1 AND (trim(e.first_name)='' OR trim(e.last_name)='' OR trim(e.position)='' " \
 "OR (e.late_email_enabled=1 AND trim(coalesce(e.email,''))='') " \
 "OR (EXISTS(SELECT 1 FROM cards c WHERE c.employee_id=e.id AND c.active=1) AND e.schedule_exempt=0 AND e.attendance_mode='punch_required' AND NOT EXISTS(SELECT 1 FROM schedule_profiles s WHERE s.employee_id=e.id)))"
static void issue_item(sqlite3_str*o,int*n,const char*key,long count,const char*text){
 if(count<=0)return;
 sqlite3_str_appendf(o,"%s{\"key\":\"%s\",\"count\":%ld,\"text\":",(*n)++?",":"",key,count);str_json(o,text);sqlite3_str_appendchar(o,1,'}');
}
/* A period is overdue if it was not closed by its first scheduled dispatch
   after the period ended. No new schema: use its existing closed_at. */
static long overdue_payroll_periods(void){
 sqlite3_stmt*q=NULL;int lead=0,enabled=0;
 if(sqlite3_prepare_v2(db,"SELECT lead_days,enabled FROM payroll_settings WHERE id=1",-1,&q,NULL)!=SQLITE_OK)return 0;
 if(sqlite3_step(q)==SQLITE_ROW){lead=sqlite3_column_int(q,0);enabled=sqlite3_column_int(q,1);}
 sqlite3_finalize(q);if(!enabled)return 0;
 time_t today=time(NULL);struct tm today_tm;localtime_r(&today,&today_tm);today_tm.tm_hour=12;today_tm.tm_min=0;today_tm.tm_sec=0;today_tm.tm_isdst=-1;today=mktime(&today_tm);
 long count=0;
 if(sqlite3_prepare_v2(db,"SELECT p.id,p.ends,p.status,coalesce(date(p.closed_at,'localtime'),'') FROM pay_periods p WHERE p.ends<date('now','localtime') AND p.ends>=coalesce((SELECT nullif(enabled_since,'') FROM payroll_settings WHERE id=1),'9999-12-31') AND NOT EXISTS (SELECT 1 FROM email_queue e WHERE e.event_id='payroll-period:'||p.id AND e.status='sent')",-1,&q,NULL)!=SQLITE_OK)return 0;
 while(sqlite3_step(q)==SQLITE_ROW){const char*ends=(const char*)sqlite3_column_text(q,1),*status=(const char*)sqlite3_column_text(q,2),*closed=(const char*)sqlite3_column_text(q,3);
  time_t due=payroll_shift(payroll_day(ends),lead);
  if(due==(time_t)-1||due>=today)continue;
  if(strcmp(status,"closed")||!closed||!*closed||payroll_day(closed)>due)count++;
 }
 sqlite3_finalize(q);return count;
}
static void issues_json(int fd){
 sqlite3_str*o=sqlite3_str_new(db);int n=0;char t[160];long c;time_t now=time(NULL);
 sqlite3_str_appendall(o,"{\"items\":[");
 c=issue_count("SELECT count(*) FROM unknown_cards WHERE last_seen>=strftime('%Y-%m-%dT%H:%M:%fZ','now','-7 days')");
 snprintf(t,sizeof t,"%ld unknown card%s tapped in the last 7 days",c,c==1?"":"s");issue_item(o,&n,"unknown",c,t);
 c=issue_count("SELECT count(*) FROM pay_periods WHERE status='open' AND ends<date('now','localtime')");
 snprintf(t,sizeof t,"%ld ended pay period%s not closed",c,c==1?"":"s");issue_item(o,&n,"periods",c,t);
 c=overdue_payroll_periods();
 snprintf(t,sizeof t,"%ld pay period%s missed the scheduled payroll report send date",c,c==1?"":"s");issue_item(o,&n,"payroll",c,t);
 c=issue_count("SELECT count(*) FROM timeoff_requests WHERE status='pending' AND cancelled_at IS NULL");
 snprintf(t,sizeof t,"%ld time-off request%s awaiting a manager decision",c,c==1?"":"s");issue_item(o,&n,"time-off",c,t);
 c=issue_count("SELECT count(*) FROM employees e WHERE " EMPLOYEE_ISSUE_SQL);
 snprintf(t,sizeof t,"%ld employee record%s need%s attention",c,c==1?"":"s",c==1?"s":"");issue_item(o,&n,"employees",c,t);
 c=issue_count("SELECT count(*) FROM email_queue WHERE status='failed'");
 snprintf(t,sizeof t,"%ld email%s failed",c,c==1?"":"s");issue_item(o,&n,"queue",c,t);
 if(!smtp_configured())issue_item(o,&n,"smtp",1,"No mail server set. No email is sent.");
 if(!clock_synced())issue_item(o,&n,"ntp",1,"Clock time is not synchronized. Punches are refused.");
 long long expires=tls_not_after();
 if(!tls_active())issue_item(o,&n,"tls",1,"HTTPS is not active");
 else if(tls_temporary())issue_item(o,&n,"tls",1,"Temporary self-signed certificate in use");
 else if(expires&&expires-(long long)now<30LL*86400){long long days=(expires-(long long)now)/86400;if(days<0)days=0;
  snprintf(t,sizeof t,"HTTPS certificate expires in %lld day%s",days,days==1?"":"s");issue_item(o,&n,"tls",1,t);}
 c=issue_count("SELECT count(*) FROM rejected_uploads WHERE received_at>=strftime('%Y-%m-%dT%H:%M:%fZ','now','-7 days')");
 snprintf(t,sizeof t,"%ld swipe%s refused in the last 7 days",c,c==1?"":"s");issue_item(o,&n,"uploads",c,t);
 c=issue_count("SELECT count(*) FROM registered_clocks r LEFT JOIN oled_previews p ON p.clock_id=r.clock_id WHERE r.active=1 AND (p.updated_at IS NULL OR strftime('%s','now')-strftime('%s',p.updated_at)>=180)");
 snprintf(t,sizeof t,"%ld clock%s %s a stale or unavailable display preview",c,c==1?"":"s",c==1?"has":"have");issue_item(o,&n,"clocks",c,t);
 long long newest=newest_backup_epoch();
 if(newest<0)issue_item(o,&n,"backups",1,"Backup folder cannot be read");
 else if(newest==0)issue_item(o,&n,"backups",1,"No backup yet");
 else if((long long)now-newest>2LL*86400){snprintf(t,sizeof t,"Last backup %lld days ago",((long long)now-newest)/86400);issue_item(o,&n,"backups",1,t);}
 sqlite3_str_appendall(o,"]}");send_str(fd,"application/json",o);
}
