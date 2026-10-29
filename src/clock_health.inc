static int clock_health_last_sync=-1;
/* A new Primary observation window never includes silence from before startup
   or an unsynchronized clock. Preserve genuine outage episodes until a new heartbeat. */
static int clock_health_start(int synced){
 int ok=db_run(db,"UPDATE clock_health SET monitor_since=CAST(strftime('%s','now') AS INTEGER)","")>=0;
 if(ok){clock_health_last_sync=synced;}return ok;
}
/* Health is independent of OLED previews. All times below are Primary UTC epochs. */
static void clock_health_receive(int fd,const char*clock,const char*body){
 char pending[16],rejected[16],healthy[4],synced[4],oldest[40]="";char*end;
 if(!form_value(body,"pending",pending,sizeof pending)||!form_value(body,"rejected",rejected,sizeof rejected)||!form_value(body,"healthy",healthy,sizeof healthy)||!form_value(body,"synced",synced,sizeof synced)||!form_value(body,"oldest",oldest,sizeof oldest)){reply_incomplete(fd);return;}
 long p=strtol(pending,&end,10);if(!*pending||*end||p<0||p>2147483647){reply_incomplete(fd);return;}
 long r=strtol(rejected,&end,10);if(!*rejected||*end||r<0||r>2147483647||(strcmp(healthy,"0")&&strcmp(healthy,"1"))||(strcmp(synced,"0")&&strcmp(synced,"1"))){reply_incomplete(fd);return;}
 time_t stamp;struct tm local;if((p>0&&!oldest[0])||(oldest[0]&&!attendance_stamp(oldest,&stamp,&local))){reply_incomplete(fd);return;}
 char ip[64]="",version[64]="";unsigned char address[16];
 if((form_has(body,"ip_address")&&!form_value(body,"ip_address",ip,sizeof ip))||
    (form_has(body,"software_version")&&!form_value(body,"software_version",version,sizeof version))){reply_incomplete(fd);return;}
 if((*ip&&inet_pton(AF_INET,ip,address)!=1&&inet_pton(AF_INET6,ip,address)!=1)||
    strspn(version,"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._+-")!=strlen(version)){reply_incomplete(fd);return;}
 int ok=db_run(db,"INSERT INTO clock_health(clock_id,last_seen,monitor_since,pending,oldest,rejected,healthy,synced,ip_address,software_version) VALUES(?1,CAST(strftime('%s','now') AS INTEGER),CAST(strftime('%s','now') AS INTEGER),?2,?3,?4,?5,?6,?7,?8) ON CONFLICT(clock_id) DO UPDATE SET last_seen=excluded.last_seen,pending=excluded.pending,oldest=excluded.oldest,rejected=excluded.rejected,healthy=excluded.healthy,synced=excluded.synced,ip_address=excluded.ip_address,software_version=excluded.software_version","titiiitt",clock,(int)p,oldest,(int)r,healthy[0]=='1',synced[0]=='1',ip,version)>=0;
 if(!ok){reply_failed(fd);return;}send_response(fd,200,"application/json","{\"status\":\"received\"}");
}
static void clock_health_tick(int synced){
 if(!synced){clock_health_last_sync=0;return;}
 if(clock_health_last_sync==0&&!clock_health_start(1))return;
 clock_health_last_sync=1;
 /* Start a first-heartbeat grace period for active adopted clocks, never for Primary. */
 db_run(db,"INSERT OR IGNORE INTO clock_health(clock_id,monitor_since) SELECT clock_id,CAST(strftime('%s','now') AS INTEGER) FROM registered_clocks WHERE active=1 AND device_key<>?1","t",device_key);
 sqlite3_stmt*q=db_prepare(db,"SELECT h.clock_id,coalesce(n.name,h.clock_id),h.last_seen,h.pending,h.oldest,h.outage_started,CAST(strftime('%s','now') AS INTEGER)-CASE WHEN h.last_seen BETWEEN h.monitor_since AND CAST(strftime('%s','now') AS INTEGER) THEN h.last_seen ELSE h.monitor_since END>=180,(h.last_seen BETWEEN h.monitor_since AND CAST(strftime('%s','now') AS INTEGER)) FROM clock_health h JOIN registered_clocks r ON r.clock_id=h.clock_id LEFT JOIN clock_names n ON n.clock_id=h.clock_id WHERE r.active=1 AND r.device_key<>?1","t",device_key);
 if(!q)return;
 int scan_rc;
 while((scan_rc=sqlite3_step(q))==SQLITE_ROW){
  char clock[49],name[160],oldest[40];snprintf(clock,sizeof clock,"%s",col_text(q,0));snprintf(name,sizeof name,"%s",col_text(q,1));snprintf(oldest,sizeof oldest,"%s",col_text(q,4));
  if(oldest[0]){time_t t;struct tm local;if(attendance_stamp(oldest,&t,&local))strftime(oldest,sizeof oldest,"%Y-%m-%d %H:%M UTC%z",&local);}
  long long last=sqlite3_column_int64(q,2),episode=sqlite3_column_int64(q,5);int pending=sqlite3_column_int(q,3),offline=sqlite3_column_int(q,6);
  if(!offline&&!episode)continue;
  if(!offline&&!sqlite3_column_int(q,7))continue;
  /* Close observed recovery independently of email preparation. A later outage
     must get its own ID even if the recovery notice cannot enter the queue. */
  if(!offline&&db_run(db,"UPDATE clock_health SET outage_started=0 WHERE clock_id=?1","t",clock)<0)continue;
  if(offline&&!episode){
   if(db_run(db,"UPDATE clock_health SET outage_started=CAST(strftime('%s','now') AS INTEGER) WHERE clock_id=?1 AND outage_started=0","t",clock)<0)continue;
   episode=db_int(db,0,"SELECT outage_started FROM clock_health WHERE clock_id=?1","t",clock);if(!episode)continue;
  }
  int ok=sql("BEGIN IMMEDIATE");if(!ok)continue;
  sqlite3_stmt*managers=db_prepare(db,"SELECT DISTINCT email FROM employees e JOIN employee_roles r ON r.employee_id=e.id AND r.role='manager' WHERE e.active=1 AND email<>''","");
  if(!managers)ok=0;
  int manager_rc=SQLITE_DONE,recipients=0;
  while(ok&&(manager_rc=sqlite3_step(managers))==SQLITE_ROW){
   const char*to=col_text(managers,0);if(!email_address_ok(to))continue;recipients++;
   char event[500],last_text[64],backlog[180],subject[32768],message[32768];
   snprintf(event,sizeof event,"clock-%s:%s:%lld:%s",offline?"offline":"recovered",clock,episode,to);
   if(db_int(db,0,"SELECT count(*) FROM email_queue WHERE event_id=?1","t",event))continue;
   if(last){time_t t=(time_t)last;struct tm local;localtime_r(&t,&local);strftime(last_text,sizeof last_text,"%Y-%m-%d %H:%M UTC%z",&local);}else snprintf(last_text,sizeof last_text,"No heartbeat received");
   if(offline)snprintf(backlog,sizeof backlog,"Last reported: %d pending. Current count is unknown while disconnected.",pending);
   else snprintf(backlog,sizeof backlog,"%d pending%s%s. Uploads may still be draining; check Clock management.",pending,oldest[0]?", oldest ":"",oldest);
   const struct email_field fields[]={{"{{clock_name}}",name},{"{{clock_id}}",clock},{"{{last_contact}}",last_text},{"{{backlog}}",backlog}};
   int rendered=template_render(db,offline?"clock_offline_manager":"clock_recovered_manager",fields,4,subject,sizeof subject,message,sizeof message);
   if(rendered<0)ok=0;else if(rendered>0)ok=email_enqueue(db,to,subject,message,event,NULL)>=0;
  }
  if(manager_rc!=SQLITE_DONE||!recipients)ok=0;
  sqlite3_finalize(managers);
  char failure[120];snprintf(failure,sizeof failure,"clock-health:%s:%lld",clock,episode);
  if(ok)ok=email_preparation_clear(failure);
  if(ok){ok=sql("COMMIT");}if(!ok){sql("ROLLBACK");email_preparation_failure(failure,0,name,"Clock warning could not be queued. Check manager email addresses, templates and database.");}
 }
 sqlite3_finalize(q);
 if(scan_rc!=SQLITE_DONE)email_preparation_failure("clock-monitor-read",0,"Clock monitoring","Could not read clock health. Check the database.");else email_preparation_clear("clock-monitor-read");
}

static void clock_health_due(void){clock_health_tick(clock_synced());}

static long clock_health_issues(void){
 return db_int(db,0,"SELECT count(*) FROM registered_clocks r LEFT JOIN clock_health h ON h.clock_id=r.clock_id WHERE r.active=1 AND r.device_key<>?1 AND (h.pending>0 OR h.rejected>0 OR h.healthy=0 OR h.synced=0 OR h.monitor_since IS NOT NULL AND CAST(strftime('%s','now') AS INTEGER)-CASE WHEN h.last_seen BETWEEN h.monitor_since AND CAST(strftime('%s','now') AS INTEGER) THEN h.last_seen ELSE h.monitor_since END>=180)","t",device_key)+db_int(db,0,"SELECT count(*) FROM registered_clocks r LEFT JOIN oled_previews p ON p.clock_id=r.clock_id WHERE r.active=1 AND r.device_key=?1 AND (p.updated_at IS NULL OR CAST(strftime('%s','now') AS INTEGER)-CAST(strftime('%s',p.updated_at) AS INTEGER)>=180)","t",device_key);
}
