/* Time-off requests: passwordless employee access, manager decisions, durable emails. */
static int timeoff_schema(void){return sql(
 "CREATE TABLE IF NOT EXISTS timeoff_links(employee_id INTEGER PRIMARY KEY REFERENCES employees(id),token_hash TEXT NOT NULL,expires_at INTEGER NOT NULL,last_sent_at INTEGER NOT NULL);"
 "CREATE TABLE IF NOT EXISTS timeoff_requests(id INTEGER PRIMARY KEY,employee_id INTEGER NOT NULL REFERENCES employees(id),kind TEXT NOT NULL CHECK(kind IN ('vacation','personal','unpaid','other')),starts TEXT NOT NULL,ends TEXT NOT NULL,note TEXT NOT NULL DEFAULT '',status TEXT NOT NULL DEFAULT 'pending' CHECK(status IN ('pending','approved','denied')),manager_id INTEGER REFERENCES employees(id),decision_note TEXT NOT NULL DEFAULT '',requested_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),decided_at TEXT,cancelled_at TEXT,requested_days INTEGER,return_date TEXT,acknowledged_at TEXT, CHECK(ends>=starts));"
 "CREATE INDEX IF NOT EXISTS timeoff_employee_history ON timeoff_requests(employee_id,requested_at);"
 "CREATE INDEX IF NOT EXISTS timeoff_pending ON timeoff_requests(status,starts);"
 "CREATE TABLE IF NOT EXISTS timeoff_audit(id INTEGER PRIMARY KEY,request_id INTEGER NOT NULL REFERENCES timeoff_requests(id),actor_id INTEGER NOT NULL REFERENCES employees(id),action TEXT NOT NULL,at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
 );
}
static int timeoff_iso(const char*d);
/* Calendar-day verification uses recorded shifts first, salary weekly mask second,
   and a clearly identified Mon-Fri estimate when no schedule is available. */
static int timeoff_day_count(int employee,const char*starts,const char*ends,int *estimate){
 sqlite3_stmt*q=NULL;int days=-1;*estimate=0;
 const char*sqlq="WITH RECURSIVE dates(day) AS (SELECT date(?1) UNION ALL SELECT date(day,'+1 day') FROM dates WHERE day<date(?2)) "
 "SELECT COALESCE(SUM(CASE WHEN EXISTS(SELECT 1 FROM statutory_holidays h WHERE h.day=dates.day) THEN 0 "
 "WHEN EXISTS(SELECT 1 FROM schedule_shifts sh JOIN schedule_profiles sp ON sp.employee_id=sh.employee_id AND sp.schedule_mode='dated_shifts' WHERE sh.employee_id=?3 AND sh.work_date=dates.day) THEN 1 "
 "WHEN EXISTS(SELECT 1 FROM schedule_profiles p WHERE p.employee_id=?3 AND p.schedule_mode='recurring_weekly') THEN "
 "CASE WHEN (SELECT weekday_mask FROM schedule_profiles WHERE employee_id=?3) & (1 << ((CAST(strftime('%w',dates.day) AS INTEGER)+6)%7)) !=0 THEN 1 ELSE 0 END "
 "WHEN CAST(strftime('%w',dates.day) AS INTEGER) BETWEEN 1 AND 5 THEN 1 ELSE 0 END),0), "
 "COALESCE(MAX(CASE WHEN EXISTS(SELECT 1 FROM statutory_holidays h WHERE h.day=dates.day) THEN 0 "
 "WHEN EXISTS(SELECT 1 FROM schedule_shifts sh JOIN schedule_profiles sp ON sp.employee_id=sh.employee_id AND sp.schedule_mode='dated_shifts' WHERE sh.employee_id=?3 AND sh.work_date=dates.day) THEN 0 "
 "WHEN EXISTS(SELECT 1 FROM schedule_profiles p WHERE p.employee_id=?3 AND p.schedule_mode='recurring_weekly') THEN 0 "
 "WHEN CAST(strftime('%w',dates.day) AS INTEGER) BETWEEN 1 AND 5 THEN 1 ELSE 0 END),0) "
 "FROM dates";
 if(sqlite3_prepare_v2(db,sqlq,-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,ends,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,3,employee);if(sqlite3_step(q)==SQLITE_ROW){days=sqlite3_column_int(q,0);*estimate=sqlite3_column_int(q,1);}}
 sqlite3_finalize(q);return days;
}
static void timeoff_day_preview(int fd,int employee,const char*starts,const char*ends){
 if(!timeoff_iso(starts)||!timeoff_iso(ends)||strcmp(starts,ends)>0){send_response(fd,422,"application/json","{\"error\":\"Choose valid dates.\"}");return;}
 sqlite3_stmt*q=NULL;int valid=0;
 if(sqlite3_prepare_v2(db,"SELECT julianday(?2)-julianday(?1) BETWEEN 0 AND 365",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,ends,-1,SQLITE_TRANSIENT);valid=sqlite3_step(q)==SQLITE_ROW&&sqlite3_column_int(q,0);}sqlite3_finalize(q);
 if(!valid){send_response(fd,422,"application/json","{\"error\":\"Request cannot exceed one year.\"}");return;}
 int estimate=0,days=timeoff_day_count(employee,starts,ends,&estimate);
 if(days==0){send_response(fd,422,"application/json","{\"error\":\"These dates have no scheduled working days.\"}");return;}
 sqlite3_stmt*shift_q=NULL;int has_shifts=0;
 if(sqlite3_prepare_v2(db,"SELECT EXISTS(SELECT 1 FROM schedule_shifts WHERE employee_id=?1 AND work_date BETWEEN ?2 AND ?3)",-1,&shift_q,NULL)==SQLITE_OK){sqlite3_bind_int(shift_q,1,employee);sqlite3_bind_text(shift_q,2,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(shift_q,3,ends,-1,SQLITE_TRANSIENT);if(sqlite3_step(shift_q)==SQLITE_ROW)has_shifts=sqlite3_column_int(shift_q,0);}sqlite3_finalize(shift_q);
 char returned[16]="";
 for(int offset=1;offset<=21;offset++){
  sqlite3_stmt*next=NULL;char day[16]="";
  if(sqlite3_prepare_v2(db,"SELECT date(?1, '+'||?2||' days')",-1,&next,NULL)==SQLITE_OK){sqlite3_bind_text(next,1,ends,-1,SQLITE_TRANSIENT);sqlite3_bind_int(next,2,offset);if(sqlite3_step(next)==SQLITE_ROW)snprintf(day,sizeof day,"%s",ar_text(next,0));}sqlite3_finalize(next);
  if(*day){int estimated=0;if(timeoff_day_count(employee,day,day,&estimated)>0){snprintf(returned,sizeof returned,"%s",day);break;}}
 }
 char reply[240];snprintf(reply,sizeof reply,"{\"days\":%d,\"estimate\":%s,\"scheduled_shifts\":%s,\"suggested_return\":\"%s\"}",days,estimate?"true":"false",has_shifts?"true":"false",returned);send_response(fd,days>=0?200:500,"application/json",days>=0?reply:"{\"error\":\"Cannot calculate requested days.\"}");
}
static int timeoff_email(const char*recipient,const char*subject,const char*body,const char*key){
 sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(db,"INSERT OR IGNORE INTO email_queue(recipient,subject,body,status,next_attempt,event_id) VALUES(?1,?2,?3,'pending',strftime('%Y-%m-%dT%H:%M:%fZ','now'),?4)",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,recipient,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,subject,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,body,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,4,key,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);return ok;
}
static int timeoff_render_template(const char *id,const struct email_field *fields,size_t n,char *subject,size_t subject_cap,char *message,size_t message_cap){
 sqlite3_stmt*q=NULL;int enabled=0;
 if(sqlite3_prepare_v2(db,"SELECT subject,body FROM email_templates WHERE id=?1 AND enabled=1",-1,&q,NULL)==SQLITE_OK){
  sqlite3_bind_text(q,1,id,-1,SQLITE_STATIC);
  if(sqlite3_step(q)==SQLITE_ROW){
   char *ts=email_render(ar_text(q,0),fields,n),*tm=email_render(ar_text(q,1),fields,n);
   if(ts&&tm&&strlen(ts)<subject_cap&&strlen(tm)<message_cap){snprintf(subject,subject_cap,"%s",ts);snprintf(message,message_cap,"%s",tm);enabled=1;}
   free(ts);free(tm);
  }
 }sqlite3_finalize(q);return enabled;
}
static int timeoff_token_employee(const char *token){char hash[256];if(!review_hash(token,hash))return 0;sqlite3_stmt*q=NULL;int id=0;
 if(sqlite3_prepare_v2(db,"SELECT l.employee_id FROM timeoff_links l JOIN employees e ON e.id=l.employee_id WHERE l.token_hash=?1 AND l.expires_at>CAST(strftime('%s','now') AS INTEGER) AND e.active=1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,hash,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW)id=sqlite3_column_int(q,0);}sqlite3_finalize(q);return id;
}
static int timeoff_iso(const char*d){if(strlen(d)!=10||d[4]!='-'||d[7]!='-')return 0;for(int i=0;i<10;i++)if(i!=4&&i!=7&&!isdigit((unsigned char)d[i]))return 0;
 sqlite3_stmt*q=NULL;int ok=0;if(sqlite3_prepare_v2(db,"SELECT date(?1,'+0 days')=?1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,d,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_ROW&&sqlite3_column_int(q,0);}sqlite3_finalize(q);return ok;}
static void timeoff_request_link(int fd,const char*body){char email[256]="";form_value(body,"email",email,sizeof email);
 if(strlen(email)>3&&strchr(email,'@')&&!strpbrk(email,"\r\n")){
 sqlite3_stmt*q=NULL;int employee=0;char name[192]="",base[270]="";
 if(sqlite3_prepare_v2(db,"SELECT id,name FROM employees WHERE lower(email)=lower(?1) AND active=1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,email,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW){employee=sqlite3_column_int(q,0);snprintf(name,sizeof name,"%s",ar_text(q,1));}}sqlite3_finalize(q);
 note_link_base(base,sizeof base);
 if(employee&&*base){char token[65],hash[256];if(random_hex(token,32)&&review_hash(token,hash)&&sql("BEGIN IMMEDIATE")){
 int ok=sqlite3_prepare_v2(db,"INSERT INTO timeoff_links(employee_id,token_hash,expires_at,last_sent_at) VALUES(?1,?2,CAST(strftime('%s','now') AS INTEGER)+86400,CAST(strftime('%s','now') AS INTEGER)) ON CONFLICT(employee_id) DO UPDATE SET token_hash=excluded.token_hash,expires_at=excluded.expires_at,last_sent_at=excluded.last_sent_at WHERE timeoff_links.last_sent_at<=CAST(strftime('%s','now') AS INTEGER)-900",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,employee);sqlite3_bind_text(q,2,hash,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE&&sqlite3_changes(db)==1;}sqlite3_finalize(q);
 if(ok){char message[800],key[160];snprintf(message,sizeof message,"Hello %s,\n\nUse this private link to request time off and view your request history:\n\n%s/time-off/%s\n\nThe link expires in 24 hours. Do not forward it.",name,base,token);snprintf(key,sizeof key,"timeoff_link:%d:%s",employee,token);ok=timeoff_email(email,"Your time-off request link",message,key);}if(ok)sql("COMMIT");else sql("ROLLBACK");
 }}
 }
 send_response(fd,200,"application/json","{\"status\":\"If this email is on file, a private link will be sent. Check your inbox.\"}");
}
/* Manager-issued private vacation link. No employee update or new authentication model. */
static void timeoff_manager_send_link(int fd,int manager,const char *body){
 if(manager<=0){send_response(fd,401,"application/json","{\"error\":\"Sign in again.\"}");return;}
 char txt[24]="";if(!form_value(body,"id",txt,sizeof txt)){send_response(fd,400,"application/json","{\"error\":\"Select an employee.\"}");return;}
 char *end=NULL;long employee=strtol(txt,&end,10);if(employee<=0||!end||*end){send_response(fd,400,"application/json","{\"error\":\"Invalid employee.\"}");return;}
 sqlite3_stmt *q=NULL;char email[256]="",name[192]="",base[270]="";
 if(sqlite3_prepare_v2(db,"SELECT email,name FROM employees WHERE id=?1 AND active=1 AND email LIKE '%@%'",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int64(q,1,employee);if(sqlite3_step(q)==SQLITE_ROW){snprintf(email,sizeof email,"%s",ar_text(q,0));snprintf(name,sizeof name,"%s",ar_text(q,1));}}sqlite3_finalize(q);
 if(!email[0]||strpbrk(email,"\r\n ")){send_response(fd,409,"application/json","{\"error\":\"Employee must be active and have a valid email address.\"}");return;}
 note_link_base(base,sizeof base);if(!base[0]){send_response(fd,409,"application/json","{\"error\":\"Configure the public HTTPS address first.\"}");return;}
 char token[65],hash[256];if(!random_hex(token,32)||!review_hash(token,hash)||!sql("BEGIN IMMEDIATE")){send_response(fd,500,"application/json","{\"error\":\"Could not create link.\"}");return;}
 int ok=sqlite3_prepare_v2(db,"INSERT INTO timeoff_links(employee_id,token_hash,expires_at,last_sent_at) VALUES(?1,?2,CAST(strftime('%s','now') AS INTEGER)+86400,CAST(strftime('%s','now') AS INTEGER)) ON CONFLICT(employee_id) DO UPDATE SET token_hash=excluded.token_hash,expires_at=excluded.expires_at,last_sent_at=excluded.last_sent_at WHERE timeoff_links.last_sent_at<=CAST(strftime('%s','now') AS INTEGER)-900",-1,&q,NULL)==SQLITE_OK;
 int changed=0;if(ok){sqlite3_bind_int64(q,1,employee);sqlite3_bind_text(q,2,hash,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;changed=sqlite3_changes(db);}sqlite3_finalize(q);
 if(ok&&!changed){sql("ROLLBACK");send_response(fd,200,"application/json","{\"status\":\"already_recent\"}");return;}
 if(ok){char message[900],key[160];snprintf(message,sizeof message,"Hello %s,\n\nUse this private link to request time off and view your request history:\n\n%s/time-off/%s\n\nThe link expires in 24 hours. Do not forward it.",name,base,token);snprintf(key,sizeof key,"timeoff_link:%ld:%s",employee,token);ok=timeoff_email(email,"Your time-off request link",message,key);}
 if(ok){q=NULL;ok=sqlite3_prepare_v2(db,"INSERT INTO employee_change_audit(manager_id,employee_id,change_body) VALUES(?1,?2,'Sent private time-off link')",-1,&q,NULL)==SQLITE_OK;if(ok){sqlite3_bind_int(q,1,manager);sqlite3_bind_int64(q,2,employee);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);}
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");
 send_response(fd,ok?200:500,"application/json",ok?"{\"status\":\"queued\"}":"{\"error\":\"Could not queue the vacation link.\"}");
}
static void timeoff_vacation_get(int fd,int employee,const char *path){
 time_t now=time(NULL);struct tm local;localtime_r(&now,&local);int year=local.tm_year+1900;
 if(strchr(path,'?')){char text[8]="";if(!op_query_param(path,"year",text,sizeof text)||strlen(text)!=4||strspn(text,"0123456789")!=4||atoi(text)<2000||atoi(text)>2100){send_response(fd,400,"application/json","{\"error\":\"Choose a valid year.\"}");return;}year=atoi(text);}
 int used=0,specified=0;double allowed=0;if(!vacation_year_usage(db,employee,year,&used,&allowed,&specified)){send_response(fd,500,"application/json","{\"error\":\"Cannot calculate annual vacation.\"}");return;}
 char allowance[32]="null",reply[200];if(specified)snprintf(allowance,sizeof allowance,"%g",allowed);
 snprintf(reply,sizeof reply,"{\"year\":%d,\"used\":%d,\"allowed\":%s}",year,used,allowance);send_response(fd,200,"application/json",reply);
}
static void timeoff_list(int fd,int employee){sqlite3_stmt*q=NULL;sqlite3_str*o=sqlite3_str_new(db);sqlite3_str_appendall(o,"{\"requests\":[");int n=0;
 if(sqlite3_prepare_v2(db,"SELECT id,kind,starts,ends,note,CASE WHEN cancelled_at IS NOT NULL THEN 'cancelled' ELSE status END,coalesce(decision_note,''),requested_at,coalesce(requested_days,0),coalesce(return_date,'') FROM timeoff_requests WHERE employee_id=?1 ORDER BY id DESC LIMIT 250",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,employee);while(sqlite3_step(q)==SQLITE_ROW){if(n++)sqlite3_str_appendchar(o,1,',');sqlite3_str_appendf(o,"{\"id\":%d,\"kind\":",sqlite3_column_int(q,0));str_json(o,ar_text(q,1));sqlite3_str_appendall(o,",\"starts\":");str_json(o,ar_text(q,2));sqlite3_str_appendall(o,",\"ends\":");str_json(o,ar_text(q,3));sqlite3_str_appendall(o,",\"note\":");str_json(o,ar_text(q,4));sqlite3_str_appendall(o,",\"status\":");str_json(o,ar_text(q,5));sqlite3_str_appendall(o,",\"decision_note\":");str_json(o,ar_text(q,6));sqlite3_str_appendall(o,",\"requested_at\":");str_json(o,ar_text(q,7));sqlite3_str_appendf(o,",\"requested_days\":%d,\"return_date\":",sqlite3_column_int(q,8));str_json(o,ar_text(q,9));sqlite3_str_appendchar(o,1,'}');}}sqlite3_finalize(q);sqlite3_str_appendall(o,"]}");send_str(fd,"application/json",o);
}
static void timeoff_submit(int fd,int employee,const char*body){char kind[24]="",starts[24]="",ends[24]="",note[501]="",daysbuf[16]="",return_date[24]="";form_value(body,"requested_days",daysbuf,sizeof daysbuf);form_value(body,"return_date",return_date,sizeof return_date);form_value(body,"kind",kind,sizeof kind);form_value(body,"starts",starts,sizeof starts);form_value(body,"ends",ends,sizeof ends);form_value(body,"note",note,sizeof note);
 if((strcmp(kind,"vacation")&&strcmp(kind,"personal")&&strcmp(kind,"unpaid")&&strcmp(kind,"other"))||!timeoff_iso(starts)||!timeoff_iso(ends)||strcmp(starts,ends)>0||strpbrk(note,"\r")){send_response(fd,422,"application/json","{\"error\":\"Check the request type and dates.\"}");return;}
 if(!timeoff_iso(return_date)||strcmp(return_date,ends)<=0||!daysbuf[0]||strspn(daysbuf,"0123456789")!=strlen(daysbuf)) {send_response(fd,422,"application/json","{\"error\":\"Confirm the number of days and a return date after the requested dates.\"}");return;}
 int estimated=0,calculated=timeoff_day_count(employee,starts,ends,&estimated);
 if(calculated==0){send_response(fd,422,"application/json","{\"error\":\"These dates have no scheduled working days.\"}");return;}
 if(calculated<0||atoi(daysbuf)!=calculated){send_response(fd,422,"application/json","{\"error\":\"Requested days do not match the calculated total. Check the dates and schedule with your manager.\"}");return;}
 /* Reject stale, unbounded, or overlapping requests before writing anything. */
 sqlite3_stmt *validation=NULL;int date_ok=0;
 if(sqlite3_prepare_v2(db,"SELECT date(?1)>=date('now','-3 days') AND julianday(?2)-julianday(?1) BETWEEN 0 AND 365",-1,&validation,NULL)==SQLITE_OK){sqlite3_bind_text(validation,1,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(validation,2,ends,-1,SQLITE_TRANSIENT);date_ok=sqlite3_step(validation)==SQLITE_ROW&&sqlite3_column_int(validation,0);}
 sqlite3_finalize(validation);
 if(!date_ok){send_response(fd,422,"application/json","{\"error\":\"Choose a recent or future start date and a range no longer than one year.\"}");return;}
 if(!sql("BEGIN IMMEDIATE")){send_response(fd,503,"application/json","{\"error\":\"The clock is busy. Try again.\"}");return;}
 sqlite3_stmt *overlap=NULL;int conflict=1;
 if(sqlite3_prepare_v2(db,"SELECT EXISTS(SELECT 1 FROM timeoff_requests WHERE employee_id=?1 AND status IN ('pending','approved') AND cancelled_at IS NULL AND starts<=?3 AND ends>=?2)",-1,&overlap,NULL)==SQLITE_OK){sqlite3_bind_int(overlap,1,employee);sqlite3_bind_text(overlap,2,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(overlap,3,ends,-1,SQLITE_TRANSIENT);if(sqlite3_step(overlap)==SQLITE_ROW)conflict=sqlite3_column_int(overlap,0);}
 sqlite3_finalize(overlap);
 if(conflict){sql("ROLLBACK");send_response(fd,409,"application/json","{\"error\":\"These dates overlap an existing pending or approved request.\"}");return;}
 sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(db,"INSERT INTO timeoff_requests(employee_id,kind,starts,ends,note,requested_days,return_date,acknowledged_at) VALUES(?1,?2,?3,?4,?5,?6,?7,strftime('%Y-%m-%dT%H:%M:%fZ','now'))",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,employee);sqlite3_bind_text(q,2,kind,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,4,ends,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,5,note,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,6,calculated);sqlite3_bind_text(q,7,return_date,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);sqlite3_int64 id=sqlite3_last_insert_rowid(db);
 if(ok&&sqlite3_prepare_v2(db,"INSERT INTO timeoff_audit(request_id,actor_id,action) VALUES(?1,?2,'requested')",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int64(q,1,id);sqlite3_bind_int(q,2,employee);ok=sqlite3_step(q)==SQLITE_DONE;}else ok=0;sqlite3_finalize(q);
 char name[192]="",email[256]="";if(ok&&sqlite3_prepare_v2(db,"SELECT name,coalesce(email,'') FROM employees WHERE id=?1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,employee);if(sqlite3_step(q)==SQLITE_ROW){snprintf(name,sizeof name,"%s",ar_text(q,0));snprintf(email,sizeof email,"%s",ar_text(q,1));}}sqlite3_finalize(q);
 char message[1000],subject[200],key[100];snprintf(subject,sizeof subject,"Time off requested: %s to %s",starts,ends);snprintf(message,sizeof message,"%s requested %s time off from %s to %s (%d working %s, returns %s).\n\nNotes: %s\n\nStatus: Pending. Review under Time off requests.",name,kind,starts,ends,calculated,calculated==1?"day":"days",return_date,*note?note:"None");
 if(ok&&strchr(email,'@')){char employee_message[900],employee_subject[200];snprintf(employee_subject,sizeof employee_subject,"Time-off request received: %s to %s",starts,ends);snprintf(employee_message,sizeof employee_message,"Hello %s,\n\nYour %s request for %s to %s was sent to your manager.\n\nStatus: Pending. You will receive a message when a decision is made.",name,kind,starts,ends);char days_label[40];snprintf(days_label,sizeof days_label,"%d working %s",calculated,calculated==1?"day":"days");const struct email_field ef[]={{"{{days_label}}",days_label},{"{{employee_name}}",name},{"{{request_type}}",kind},{"{{period_start}}",starts},{"{{period_end}}",ends},{"{{days}}",daysbuf},{"{{return_date}}",return_date}};
 if(timeoff_render_template("timeoff_received_employee",ef,7,employee_subject,sizeof employee_subject,employee_message,sizeof employee_message)){snprintf(key,sizeof key,"timeoff_ack:%lld",(long long)id);ok=timeoff_email(email,employee_subject,employee_message,key);}}
 if(ok){sqlite3_stmt*m=NULL;if(sqlite3_prepare_v2(db,"SELECT e.email FROM employees e JOIN employee_roles r ON r.employee_id=e.id AND r.role='manager' WHERE e.active=1 AND e.email LIKE '%@%'",-1,&m,NULL)==SQLITE_OK){while(sqlite3_step(m)==SQLITE_ROW){const char*addr=ar_text(m,0);snprintf(key,sizeof key,"timeoff_manager:%lld:%s",(long long)id,addr);char ms[200],mb[1000];snprintf(ms,sizeof ms,"%s",subject);snprintf(mb,sizeof mb,"%s",message);
 const struct email_field mf[]={{"{{employee_name}}",name},{"{{request_type}}",kind},{"{{period_start}}",starts},{"{{period_end}}",ends},{"{{days}}",daysbuf},{"{{return_date}}",return_date},{"{{note}}",*note?note:"None"}};
 if(timeoff_render_template("timeoff_received_manager",mf,7,ms,sizeof ms,mb,sizeof mb)&&!timeoff_email(addr,ms,mb,key))ok=0;}}sqlite3_finalize(m);}
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");send_response(fd,ok?200:500,"application/json",ok?"{\"status\":\"Request submitted. Your manager will review it.\"}":"{\"error\":\"Request could not be saved. Try again.\"}");
}
static void timeoff_manager_list(int fd){sqlite3_stmt*q=NULL;sqlite3_str*o=sqlite3_str_new(db);sqlite3_str_appendall(o,"{\"requests\":[");int n=0;
 if(sqlite3_prepare_v2(db,"SELECT t.id,e.name,t.kind,t.starts,t.ends,t.note,CASE WHEN t.cancelled_at IS NOT NULL THEN 'cancelled' ELSE t.status END,coalesce(t.decision_note,''),coalesce(t.requested_days,0),coalesce(t.return_date,'') FROM timeoff_requests t JOIN employees e ON e.id=t.employee_id ORDER BY (t.status='pending') DESC,t.id DESC LIMIT 300",-1,&q,NULL)==SQLITE_OK){while(sqlite3_step(q)==SQLITE_ROW){if(n++)sqlite3_str_appendchar(o,1,',');sqlite3_str_appendf(o,"{\"id\":%d,\"name\":",sqlite3_column_int(q,0));str_json(o,ar_text(q,1));const char*labels[]={"kind","starts","ends","note","status","decision_note"};for(int i=0;i<6;i++){sqlite3_str_appendf(o,",\"%s\":",labels[i]);str_json(o,ar_text(q,i+2));}sqlite3_str_appendf(o,",\"requested_days\":%d,\"return_date\":",sqlite3_column_int(q,8));str_json(o,ar_text(q,9));sqlite3_str_appendchar(o,1,'}');}}sqlite3_finalize(q);sqlite3_str_appendall(o,"]}");send_str(fd,"application/json",o);
}
static void timeoff_manager_decide(int fd,int manager,const char*body){char idtxt[24]="",action[24]="",note[501]="";form_value(body,"id",idtxt,sizeof idtxt);form_value(body,"action",action,sizeof action);form_value(body,"note",note,sizeof note);int id=ar_employee(idtxt);
 if(!id||(strcmp(action,"approved")&&strcmp(action,"denied")&&strcmp(action,"cancelled"))){send_response(fd,422,"application/json","{\"error\":\"Choose Approve, Deny or Cancel.\"}");return;}
 if(!sql("BEGIN IMMEDIATE")){send_response(fd,503,"application/json","{\"error\":\"The clock is busy. Try again.\"}");return;}
 sqlite3_stmt*q=NULL;int cancelling=!strcmp(action,"cancelled");
 const char *update_sql=cancelling?
  "UPDATE timeoff_requests SET cancelled_at=strftime('%Y-%m-%dT%H:%M:%fZ','now'),manager_id=?2,decision_note=?3 WHERE id=?4 AND status='approved' AND cancelled_at IS NULL":
  "UPDATE timeoff_requests SET status=?1,manager_id=?2,decision_note=?3,decided_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=?4 AND status='pending'";
 int ok=sqlite3_prepare_v2(db,update_sql,-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,action,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,2,manager);sqlite3_bind_text(q,3,note,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,4,id);ok=sqlite3_step(q)==SQLITE_DONE&&sqlite3_changes(db)==1;}sqlite3_finalize(q);
 if(ok&&sqlite3_prepare_v2(db,"INSERT INTO timeoff_audit(request_id,actor_id,action) VALUES(?1,?2,?3)",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,id);sqlite3_bind_int(q,2,manager);sqlite3_bind_text(q,3,action,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}else ok=0;sqlite3_finalize(q);
 char email[256]="",name[192]="",starts[16]="",ends[16]="",mgr[192]="";
 if(ok&&sqlite3_prepare_v2(db,"SELECT e.email,e.name,t.starts,t.ends,(SELECT name FROM employees WHERE id=?2) FROM timeoff_requests t JOIN employees e ON e.id=t.employee_id WHERE t.id=?1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,id);sqlite3_bind_int(q,2,manager);if(sqlite3_step(q)==SQLITE_ROW){snprintf(email,sizeof email,"%s",ar_text(q,0));snprintf(name,sizeof name,"%s",ar_text(q,1));snprintf(starts,sizeof starts,"%s",ar_text(q,2));snprintf(ends,sizeof ends,"%s",ar_text(q,3));snprintf(mgr,sizeof mgr,"%s",ar_text(q,4));}}sqlite3_finalize(q);
 if(ok&&strchr(email,'@')){char subject[180],message[900],key[100];snprintf(subject,sizeof subject,"Time-off request %s: %s to %s",action,starts,ends);snprintf(message,sizeof message,"Hello %s,\n\nYour time-off request for %s to %s was %s by %s.\n\nManager note: %s\n\nYou can view your request history through the Time off requests page.",name,starts,ends,action,mgr,*note?note:"None");const struct email_field df[]={{"{{employee_name}}",name},{"{{period_start}}",starts},{"{{period_end}}",ends},{"{{decision}}",action},{"{{manager_name}}",mgr},{"{{note}}",*note?note:"None"}};
 const char *template_id=cancelling?"timeoff_cancelled_employee":"timeoff_decision_employee";
 if(timeoff_render_template(template_id,df,6,subject,sizeof subject,message,sizeof message)){snprintf(key,sizeof key,"timeoff_decision:%d:%s",id,action);ok=timeoff_email(email,subject,message,key);}}
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");send_response(fd,ok?200:409,"application/json",ok?"{\"status\":\"Decision saved.\"}":"{\"error\":\"This request has already been decided or could not be saved.\"}");
}
static void timeoff_public_page(int fd){send_response(fd,200,"text/html; charset=utf-8",UI_HEAD_LOGIN("Time off requests")
 "<script>document.body.classList.add('timeoff-public')</script><h1>Time off requests</h1><p>Use your work email to request time off and review earlier requests.</p>"
 "<section><h2>Get a private link</h2><form id='access'><label>Work email<input type='email' id='email' autocomplete='email' required maxlength='250' placeholder='you@company.com'></label><div class='actions'><button>Send private link</button></div></form><p id='message' role='status' aria-live='polite'></p><p class='muted'>Your link expires in 24 hours. Requests are reviewed by your manager.</p></section>"
 "<script>access.onsubmit=async e=>{e.preventDefault();let b=access.querySelector('button');b.disabled=true;try{let r=await tc.request('/time-off/link',{method:'POST',body:new URLSearchParams({email:document.getElementById('email').value})});let j=await tc.json(r);if(!r.ok)throw Error(j.error||'Link request failed');tc.ok(document.getElementById('message'),'Check your email now. The link works for 24 hours.');access.hidden=true}catch(e){tc.err(document.getElementById('message'),e)}finally{b.disabled=false}}</script>" UI_FOOT);}
static void timeoff_employee_page(int fd){send_response(fd,200,"text/html; charset=utf-8",UI_HEAD_LOGIN("Your time off")
 "<script>document.body.classList.add('timeoff-employee')</script><h1>Your time off</h1><p>Request time away from work and review your previous requests.</p>"
 "<section><h2>New request</h2><p id='vacationUsage' role='status' aria-live='polite'></p><p class='muted'>Calendar-year usage counts approved vacation, including future approved dates. This is informational; you may request more than your allowance.</p><form id='newrequest'><label>Request type<select name='kind'><option value='vacation'>Vacation</option><option value='personal'>Personal day</option><option value='unpaid'>Unpaid time off</option><option value='other'>Other</option></select></label><div class='grid'><label>Starting date<input type='date' name='starts' required></label><label>Ending date<input type='date' name='ends' required></label></div><p id='dayshint' class='muted' aria-live='polite'>Select dates to calculate working days.</p><div class='grid'><label>Total days requested<input type='number' name='requested_days' min='0' max='366' step='1' required></label><label>Return to work<input type='date' name='return_date' required></label></div><label>Notes (optional)<textarea name='note' maxlength='500' placeholder='Anything your manager should know'></textarea></label><div class='actions'><button>Submit request</button></div></form><p id='status' role='status' aria-live='polite'></p></section>"
 "<section style='order:-1'><div class='section-head'><h2>Previous requests</h2></div><div id='history' aria-live='polite'><p class='muted'>Loading requests...</p></div></section>"
 "<script>let base=location.pathname;const api=tc.api;"
 "function requestType(k){return {vacation:'Vacation',personal:'Personal day',unpaid:'Unpaid time off',other:'Other'}[k]||k}function flagStatus(s){let kind={pending:'check',approved:'ok',denied:'problem',cancelled:'muted'}[s]||'muted';let f=document.createElement('span');f.className='flag flag-'+kind;f.textContent=s==='pending'?'Waiting':s.charAt(0).toUpperCase()+s.slice(1);return f}"
 "async function load(){let j=await api(base+'/list'),h=document.getElementById('history');h.replaceChildren();if(!j.requests.length){let p=document.createElement('p');p.className='muted';p.textContent='No requests yet.';h.append(p);return}let list=document.createElement('div');list.className='timeoff-history';for(let x of j.requests){let item=document.createElement('article');item.className='timeoff-history-item';let top=document.createElement('div');top.className='timeoff-history-head';top.append(flagStatus(x.status));let dates=document.createElement('strong');dates.textContent=x.starts+' to '+x.ends+' · '+x.requested_days+' '+(x.requested_days===1?'day':'days');top.append(dates);item.append(top);let detail=document.createElement('p');detail.className='muted';detail.textContent=requestType(x.kind)+(x.return_date?' · Returns '+x.return_date:'');item.append(detail);if(x.decision_note){let note=document.createElement('p');note.textContent='Manager note: '+x.decision_note;item.append(note)}list.append(item)}h.append(list);if(j.requests.some(x=>x.status==='pending')){let historySection=h.closest('section'),formSection=newrequest.closest('section');if(historySection&&formSection)formSection.before(historySection)}}"
 "let vacationLoad=0;async function loadVacation(){const request=++vacationLoad;let years=[newrequest.elements.starts.value.slice(0,4),newrequest.elements.ends.value.slice(0,4)].filter(Boolean);years=[...new Set(years)];if(!years.length)years=[''];try{let rows=await Promise.all(years.map(y=>api(base+'/vacation'+(y?'?year='+encodeURIComponent(y):''))));if(request!==vacationLoad)return;document.getElementById('vacationUsage').textContent=rows.map(x=>x.year+': Vacation: '+x.used+' of '+(x.allowed===null?'unspecified':x.allowed)+' days used.').join(' ')}catch(e){tc.err(document.getElementById('vacationUsage'),e)}}"
 "let dayCount=null;async function recalc(){let f=new FormData(newrequest),s=f.get('starts'),e=f.get('ends'),hint=document.getElementById('dayshint');loadVacation();dayCount=null;if(!s||!e||s>e){hint.textContent='Select valid starting and ending dates.';return}try{let j=await api(base+'/days?starts='+encodeURIComponent(s)+'&ends='+encodeURIComponent(e));dayCount=j.days;newrequest.elements.requested_days.value=j.days;if(j.suggested_return)newrequest.elements.return_date.value=j.suggested_return;hint.textContent='Calculated working days: '+j.days+(j.scheduled_shifts?' (scheduled shifts and weekdays, excluding stat holidays)':j.estimate?' (weekday estimate; no fixed weekly schedule)':' (scheduled days, excluding stat holidays)')+'. You can adjust the total if needed.'}catch(err){hint.textContent=err.message}}for(let k of ['starts','ends'])newrequest.elements[k].addEventListener('change',recalc);"
 "newrequest.onsubmit=async e=>{e.preventDefault();let b=newrequest.querySelector('button');try{let f=new FormData(newrequest);if(f.get('starts')>f.get('ends'))throw Error('Ending date must be on or after the starting date.');if(dayCount===null||Number(f.get('requested_days'))!==dayCount)throw Error('The day total does not match the calculated working days.');b.disabled=true;await api(base+'/request',{method:'POST',body:new URLSearchParams(f)});newrequest.reset();dayCount=null;document.getElementById('dayshint').textContent='Select dates to calculate working days.';tc.ok(document.getElementById('status'),'Request submitted. Your manager will review it.');await load();await loadVacation()}catch(e){tc.err(document.getElementById('status'),e)}finally{b.disabled=false}};loadVacation();load().catch(e=>tc.err(document.getElementById('status'),e))</script>" UI_FOOT);}
/* Manager-only annual vacation report. Days use the existing time-off calendar,
   clipped to the requested year; pending and cancelled requests are excluded. */
static void timeoff_annual_export(int fd,const char *path){
 char year[8]="";
 if(!op_query_param(path,"year",year,sizeof year)||strlen(year)!=4||strspn(year,"0123456789")!=4||atoi(year)<2000||atoi(year)>2100){
  send_response(fd,400,"application/json","{\"error\":\"Choose a valid year.\"}");return;
 }
 char first[16],last[16],filename[80];
 snprintf(first,sizeof first,"%s-01-01",year);
 snprintf(last,sizeof last,"%s-12-31",year);
 snprintf(filename,sizeof filename,"Vacation_%s.csv",year);
 sqlite3_stmt *q=NULL;
 const char *query="SELECT t.id,e.id,e.name,t.starts,t.ends,"
  "max(t.starts,?1),min(t.ends,?2),t.requested_days,"
  "coalesce(m.name,''),coalesce(strftime('%Y-%m-%d %H:%M',t.decided_at,'localtime'),''),coalesce(t.decision_note,'') "
  "FROM timeoff_requests t JOIN employees e ON e.id=t.employee_id "
  "LEFT JOIN employees m ON m.id=t.manager_id "
  "WHERE t.kind='vacation' AND t.status='approved' AND t.cancelled_at IS NULL "
  "AND t.starts<=?2 AND t.ends>=?1 ORDER BY e.name,t.starts,t.id";
 if(sqlite3_prepare_v2(db,query,-1,&q,NULL)!=SQLITE_OK){send_response(fd,500,"application/json","{\"error\":\"Cannot prepare vacation report.\"}");return;}
 sqlite3_bind_text(q,1,first,-1,SQLITE_TRANSIENT);
 sqlite3_bind_text(q,2,last,-1,SQLITE_TRANSIENT);
 sqlite3_str *out=sqlite3_str_new(db);
 sqlite3_str_appendall(out,"Employee ID,Employee,Vacation days in year,Estimated (no schedule),Original start,Original end,Dates within year,Through,Original requested days,Approved by,Approved at,Manager note,Annual allowance (days),Vacation used / allowed\r\n");
 int rc,failed=0;
 while((rc=sqlite3_step(q))==SQLITE_ROW){
  int employee=sqlite3_column_int(q,1),estimate=0;
  const char *starts=(const char*)sqlite3_column_text(q,5),*ends=(const char*)sqlite3_column_text(q,6);
  int days=timeoff_day_count(employee,starts,ends,&estimate);
  if(days<0){sqlite3_finalize(q);sqlite3_str_finish(out);send_response(fd,500,"application/json","{\"error\":\"Cannot calculate vacation days.\"}");return;}
  char number[32];snprintf(number,sizeof number,"%d",employee);op_csv(out,number);
  sqlite3_str_appendchar(out,1,',');op_csv(out,(const char*)sqlite3_column_text(q,2));
  snprintf(number,sizeof number,"%d",days);sqlite3_str_appendchar(out,1,',');op_csv(out,number);
  sqlite3_str_appendchar(out,1,',');op_csv(out,estimate?"Yes":"No");
  for(int i=3;i<=6;i++){sqlite3_str_appendchar(out,1,',');op_csv(out,(const char*)sqlite3_column_text(q,i));}
  sqlite3_str_appendchar(out,1,',');if(sqlite3_column_type(q,7)!=SQLITE_NULL){snprintf(number,sizeof number,"%d",sqlite3_column_int(q,7));op_csv(out,number);}else op_csv(out,"");
  for(int i=8;i<=10;i++){sqlite3_str_appendchar(out,1,',');op_csv(out,(const char*)sqlite3_column_text(q,i));}
  int annual=0,specified=0;double allowed=0;if(!vacation_year_usage(db,employee,atoi(year),&annual,&allowed,&specified)){failed=1;break;}
  char limit[32]="",usage[100];if(specified)snprintf(limit,sizeof limit,"%g",allowed);
  snprintf(usage,sizeof usage,"%d of %s days used",annual,specified?limit:"unspecified");sqlite3_str_appendchar(out,1,',');op_csv(out,limit);sqlite3_str_appendchar(out,1,',');op_csv(out,usage);
  sqlite3_str_appendall(out,"\r\n");
 }
 sqlite3_finalize(q);
 if(failed||rc!=SQLITE_DONE){sqlite3_str_finish(out);send_response(fd,500,"application/json","{\"error\":\"Cannot read vacation requests.\"}");return;}
 sqlite3_str_appendall(out,"\r\nEmployee ID,Employee,Approved vacation days in year,Annual allowance (days),Vacation used / allowed\r\n");
 sqlite3_stmt *employees=NULL;
 const char *annual_query="SELECT e.id,e.name FROM employees e WHERE e.vacation_days IS NOT NULL OR EXISTS(SELECT 1 FROM timeoff_requests t WHERE t.employee_id=e.id AND t.kind='vacation' AND t.status='approved' AND t.cancelled_at IS NULL AND t.starts<=?2 AND t.ends>=?1) ORDER BY e.name,e.id";
 if(sqlite3_prepare_v2(db,annual_query,-1,&employees,NULL)!=SQLITE_OK){sqlite3_str_finish(out);send_response(fd,500,"application/json","{\"error\":\"Cannot read annual allowances.\"}");return;}
 sqlite3_bind_text(employees,1,first,-1,SQLITE_TRANSIENT);sqlite3_bind_text(employees,2,last,-1,SQLITE_TRANSIENT);
 while((rc=sqlite3_step(employees))==SQLITE_ROW){
  int employee=sqlite3_column_int(employees,0),annual=0,specified=0;double allowed=0;
  if(!vacation_year_usage(db,employee,atoi(year),&annual,&allowed,&specified)){failed=1;break;}
  char num[32],limit[32]="",usage[100];snprintf(num,sizeof num,"%d",employee);op_csv(out,num);sqlite3_str_appendchar(out,1,',');op_csv(out,ar_text(employees,1));sqlite3_str_appendchar(out,1,',');snprintf(num,sizeof num,"%d",annual);op_csv(out,num);
  if(specified)snprintf(limit,sizeof limit,"%g",allowed);
  snprintf(usage,sizeof usage,"%d of %s days used",annual,specified?limit:"unspecified");sqlite3_str_appendchar(out,1,',');op_csv(out,limit);sqlite3_str_appendchar(out,1,',');op_csv(out,usage);sqlite3_str_appendall(out,"\r\n");
 }
 sqlite3_finalize(employees);
 if(failed||rc!=SQLITE_DONE){sqlite3_str_finish(out);send_response(fd,500,"application/json","{\"error\":\"Cannot calculate annual vacation.\"}");return;}
 op_export_send(fd,"text/csv; charset=utf-8",filename,out);
}
static void timeoff_manager_page(int fd){send_response(fd,200,"text/html; charset=utf-8",UI_HEAD("Time off requests","time-off")
 "<h1>Time off requests</h1><p>Review requests, record decisions and notify employees. All decisions are audited.</p>"
 "<section><div class='section-head'><h2>Employee requests</h2></div><div id='requests'><p class='muted'>Loading requests...</p></div><p id='status' role='status' aria-live='polite'></p></section>"
 "<section><h2>Annual vacation report</h2><form method='get' action='/api/v1/admin/time-off/annual.csv'><label>Calendar year<input id='vacation-year' name='year' type='number' min='2000' max='2100' required value='2026'></label><div class='actions'><button type='submit' id='vacation-download'>Download Excel-compatible CSV</button></div></form><p class='muted'>Approved calendar-year vacation, including future approved dates. Allowances are informational; no accrual or limits. Historical totals use current schedules and holidays and may change if these are edited.</p></section>"
 "<script>const vacationYear=document.getElementById('vacation-year');vacationYear.value=String(new Date().getFullYear());const api=tc.api;"
 "function statusFlag(s){let f=document.createElement('span');f.className='flag flag-'+({pending:'check',approved:'ok',denied:'problem',cancelled:'muted'}[s]||'muted');f.textContent=s==='pending'?'Waiting':s.charAt(0).toUpperCase()+s.slice(1);return f}"
 "async function load(){let j=await api('/api/v1/admin/time-off'),h=document.getElementById('requests');h.replaceChildren();if(!j.requests.length){let p=document.createElement('p');p.className='muted';p.textContent='No time-off requests.';h.append(p);return}let t=document.createElement('table'),head=document.createElement('tr');for(let v of ['Employee','Dates','Days','Type','Status','Notes','Action']){let th=document.createElement('th');th.scope='col';th.textContent=v;head.append(th)}t.append(head);for(let x of j.requests){let tr=document.createElement('tr');for(let v of [x.name,x.starts+' to '+x.ends+' (Return '+(x.return_date||'not recorded')+')',x.requested_days||'',({vacation:'Vacation',personal:'Personal day',unpaid:'Unpaid time off',other:'Other'}[x.kind]||x.kind)]){let td=document.createElement('td');td.textContent=v;tr.append(td)}let st=document.createElement('td');st.append(statusFlag(x.status));tr.append(st);let notes=document.createElement('td');notes.textContent=[x.note,x.decision_note?'Decision: '+x.decision_note:''].filter(Boolean).join(' | ');tr.append(notes);let actions=document.createElement('td');for(let a of (x.status==='approved'?['cancelled']:x.status==='pending'?['approved','denied']:[])){let b=document.createElement('button');b.type='button';b.className='secondary';b.textContent=a==='approved'?'Approve':a==='cancelled'?'Cancel approval':'Deny';b.onclick=async()=>{let note=prompt('Manager note (optional):','');if(note===null)return;b.disabled=true;try{await api('/api/v1/admin/time-off/decide',{method:'POST',body:new URLSearchParams({id:x.id,action:a,note})});tc.ok(document.getElementById('status'),'Decision saved.');window.dispatchEvent(new Event('timeclock:issues-changed'));await load()}catch(e){tc.err(document.getElementById('status'),e);b.disabled=false}};actions.append(b)}if(!actions.childNodes.length)actions.textContent='';tr.append(actions);t.append(tr)}let wrap=document.createElement('div');wrap.className='timeoff-table-scroll';wrap.tabIndex=0;wrap.setAttribute('aria-label','Time off requests, scroll horizontally for more columns');wrap.append(t);h.append(wrap)}load().catch(e=>tc.err(document.getElementById('status'),e))</script>" UI_FOOT);}
static void timeoff_route(int fd,const char*method,const char*path,const char*request){
 if(!strcmp(path,"/time-off")&& !strcmp(method,"GET")){timeoff_public_page(fd);return;}
 if(!strcmp(path,"/time-off/link")&&!strcmp(method,"POST")){const char*b=strstr(request,"\r\n\r\n");if(b)timeoff_request_link(fd,b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete.\"}");return;}
 if(strncmp(path,"/time-off/",10)){send_response(fd,404,"text/plain","Not found");return;}
 const char*t=path+10;if(strlen(t)<64){send_response(fd,404,"text/plain","Link not valid");return;}
 char token[65];memcpy(token,t,64);token[64]=0;int employee=timeoff_token_employee(token);if(!employee){send_response(fd,404,"text/html; charset=utf-8",UI_HEAD_LOGIN("Link expired")"<h1>Link expired</h1><p class='banner banner-problem' role='alert'><span class='flag flag-problem'>Problem</span><span>This private link has expired or been replaced.</span></p><p><a href='/time-off'>Request a new link</a></p>" UI_FOOT);return;}
 const char*suffix=t+64;
 if(!strncmp(suffix,"/days?starts=",13)&&!strcmp(method,"GET")){
  char starts[16]="",ends[16]="";const char *s=suffix+13,*e=strstr(s,"&ends=");
  if(e&&e-s==10&&strlen(e+6)==10){memcpy(starts,s,10);starts[10]=0;snprintf(ends,sizeof ends,"%s",e+6);timeoff_day_preview(fd,employee,starts,ends);}else send_response(fd,422,"application/json","{\"error\":\"Choose valid dates.\"}");return;
 }
if(!strcmp(method,"GET")&&!*suffix){timeoff_employee_page(fd);return;}
 if(!strcmp(method,"GET")&&(!strcmp(suffix,"/vacation")||!strncmp(suffix,"/vacation?",10))){timeoff_vacation_get(fd,employee,suffix);return;}
 if(!strcmp(method,"GET")&&!strcmp(suffix,"/list")){timeoff_list(fd,employee);return;}
 if(!strcmp(method,"POST")&&!strcmp(suffix,"/request")){const char*b=strstr(request,"\r\n\r\n");if(b)timeoff_submit(fd,employee,b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete.\"}");return;}
 send_response(fd,404,"text/plain","Not found");
}
