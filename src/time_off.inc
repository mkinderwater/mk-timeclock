/* Vacation requests: passwordless employee access, manager decisions, durable emails. */
static int timeoff_schema(void){return sql(
 "CREATE TABLE IF NOT EXISTS timeoff_links(employee_id INTEGER PRIMARY KEY REFERENCES employees(id),token_hash TEXT NOT NULL,expires_at INTEGER NOT NULL,last_sent_at INTEGER NOT NULL);"
 "CREATE TABLE IF NOT EXISTS timeoff_requests(id INTEGER PRIMARY KEY,employee_id INTEGER NOT NULL REFERENCES employees(id),kind TEXT NOT NULL CHECK(kind IN ('vacation','personal','unpaid','other')),starts TEXT NOT NULL,ends TEXT NOT NULL,note TEXT NOT NULL DEFAULT '',status TEXT NOT NULL DEFAULT 'pending' CHECK(status IN ('pending','approved','denied')),manager_id INTEGER REFERENCES employees(id),decision_note TEXT NOT NULL DEFAULT '',requested_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')),decided_at TEXT,cancelled_at TEXT,requested_days INTEGER,return_date TEXT,acknowledged_at TEXT, CHECK(ends>=starts));"
 "CREATE INDEX IF NOT EXISTS timeoff_employee_history ON timeoff_requests(employee_id,requested_at);"
 "CREATE INDEX IF NOT EXISTS timeoff_pending ON timeoff_requests(status,starts);"
 "CREATE TABLE IF NOT EXISTS timeoff_audit(id INTEGER PRIMARY KEY,request_id INTEGER NOT NULL REFERENCES timeoff_requests(id),actor_id INTEGER NOT NULL REFERENCES employees(id),action TEXT NOT NULL,at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ','now')));"
 );
}
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
/* Shared holiday breakdown for previews and current request/history displays. */
static int timeoff_holiday_fields(sqlite3_str *out,const char *starts,const char *ends){
 sqlite3_stmt *q=NULL;sqlite3_str *items=sqlite3_str_new(db);int count=0,rc=SQLITE_ERROR;
 if(sqlite3_prepare_v2(db,"SELECT day,name FROM statutory_holidays WHERE day BETWEEN ?1 AND ?2 ORDER BY day",-1,&q,NULL)==SQLITE_OK){
  sqlite3_bind_text(q,1,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,ends,-1,SQLITE_TRANSIENT);
  while((rc=sqlite3_step(q))==SQLITE_ROW){if(count++)sqlite3_str_appendchar(items,1,',');sqlite3_str_appendall(items,"{\"date\":");json_text(items,col_text(q,0));sqlite3_str_appendall(items,",\"name\":");json_text(items,col_text(q,1));sqlite3_str_appendchar(items,1,'}');}
 }
 sqlite3_finalize(q);char *json=sqlite3_str_finish(items);
 if(rc!=SQLITE_DONE){sqlite3_free(json);return 0;}
 sqlite3_str_appendf(out,",\"stat_days\":%d,\"holidays\":[%s]",count,json?json:"");sqlite3_free(json);return 1;
}
static void timeoff_no_vacation_days(int fd,const char *starts,const char *ends){
 sqlite3_str *text=sqlite3_str_new(db);sqlite3_str_appendall(text,"These dates contain no vacation working days.");sqlite3_stmt *q=NULL;
 if(sqlite3_prepare_v2(db,"SELECT day,name FROM statutory_holidays WHERE day BETWEEN ?1 AND ?2 ORDER BY day",-1,&q,NULL)!=SQLITE_OK){sqlite3_free(sqlite3_str_finish(text));send_response(fd,500,"application/json","{\"error\":\"Cannot read stat holidays.\"}");return;}
 sqlite3_bind_text(q,1,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,ends,-1,SQLITE_TRANSIENT);int rc;
 while((rc=sqlite3_step(q))==SQLITE_ROW)sqlite3_str_appendf(text," %s is %s.",col_text(q,0),col_text(q,1));
 sqlite3_finalize(q);char *message=sqlite3_str_finish(text);if(rc!=SQLITE_DONE){sqlite3_free(message);send_response(fd,500,"application/json","{\"error\":\"Cannot read stat holidays.\"}");return;}
 sqlite3_str *out=sqlite3_str_new(db);sqlite3_str_appendall(out,"{\"error\":");json_text(out,message?message:"These dates contain no vacation working days.");sqlite3_free(message);sqlite3_str_appendchar(out,1,'}');char *reply=sqlite3_str_finish(out);send_response(fd,422,"application/json",reply?reply:"{\"error\":\"No vacation working days.\"}");sqlite3_free(reply);
}
static int timeoff_request_calendar(sqlite3_str *out,int employee,const char *kind,const char *starts,const char *ends,int requested){
 if(strcmp(kind,"vacation")){sqlite3_str_appendf(out,",\"days\":%d",requested);return 1;}
 int estimated=0,days=timeoff_day_count(employee,starts,ends,&estimated);if(days<0)return 0;
 sqlite3_str_appendf(out,",\"days\":%d,\"estimate\":%s",days,estimated?"true":"false");return timeoff_holiday_fields(out,starts,ends);
}
static void timeoff_suggest_return(int employee,const char *ends,char returned[16]){
 returned[0]=0;
 for(int offset=1;offset<=21;offset++){
  sqlite3_stmt*next=NULL;char day[16]="";
  if(sqlite3_prepare_v2(db,"SELECT date(?1, '+'||?2||' days')",-1,&next,NULL)==SQLITE_OK){sqlite3_bind_text(next,1,ends,-1,SQLITE_TRANSIENT);sqlite3_bind_int(next,2,offset);if(sqlite3_step(next)==SQLITE_ROW)snprintf(day,sizeof day,"%s",col_text(next,0));}sqlite3_finalize(next);
  if(*day){int estimated=0;if(timeoff_day_count(employee,day,day,&estimated)>0){snprintf(returned,16,"%s",day);break;}}
 }
}
static void timeoff_day_preview(int fd,int employee,const char*starts,const char*ends){
 if(!date_valid(starts)||!date_valid(ends)||strcmp(starts,ends)>0){send_response(fd,422,"application/json","{\"error\":\"Choose valid dates.\"}");return;}
 sqlite3_stmt*q=NULL;int valid=0;
 if(sqlite3_prepare_v2(db,"SELECT julianday(?2)-julianday(?1) BETWEEN 0 AND 365",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,ends,-1,SQLITE_TRANSIENT);valid=sqlite3_step(q)==SQLITE_ROW&&sqlite3_column_int(q,0);}sqlite3_finalize(q);
 if(!valid){send_response(fd,422,"application/json","{\"error\":\"Request cannot exceed one year.\"}");return;}
 int estimate=0,days=timeoff_day_count(employee,starts,ends,&estimate);
 if(days==0){timeoff_no_vacation_days(fd,starts,ends);return;}
 sqlite3_stmt*shift_q=NULL;int has_shifts=0;
 if(sqlite3_prepare_v2(db,"SELECT EXISTS(SELECT 1 FROM schedule_shifts WHERE employee_id=?1 AND work_date BETWEEN ?2 AND ?3)",-1,&shift_q,NULL)==SQLITE_OK){sqlite3_bind_int(shift_q,1,employee);sqlite3_bind_text(shift_q,2,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(shift_q,3,ends,-1,SQLITE_TRANSIENT);if(sqlite3_step(shift_q)==SQLITE_ROW)has_shifts=sqlite3_column_int(shift_q,0);}sqlite3_finalize(shift_q);
 char returned[16]="";timeoff_suggest_return(employee,ends,returned);
 if(days<0){send_response(fd,500,"application/json","{\"error\":\"Cannot calculate requested days.\"}");return;}
 sqlite3_str *out=sqlite3_str_new(db);sqlite3_str_appendf(out,"{\"days\":%d,\"estimate\":%s,\"scheduled_shifts\":%s,\"suggested_return\":\"%s\"",days,estimate?"true":"false",has_shifts?"true":"false",returned);
 if(!timeoff_holiday_fields(out,starts,ends)){sqlite3_free(sqlite3_str_finish(out));send_response(fd,500,"application/json","{\"error\":\"Cannot read stat holidays.\"}");return;}sqlite3_str_appendchar(out,1,'}');send_str(fd,"application/json",out);
}
static int timeoff_token_employee(const char *token){char hash[256];if(!token_hash(token,REVIEW_LINK_SALT,hash,sizeof hash))return 0;sqlite3_stmt*q=NULL;int id=0;
 if(sqlite3_prepare_v2(db,"SELECT l.employee_id FROM timeoff_links l JOIN employees e ON e.id=l.employee_id WHERE l.token_hash=?1 AND l.expires_at>CAST(strftime('%s','now') AS INTEGER) AND e.active=1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,hash,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW)id=sqlite3_column_int(q,0);}sqlite3_finalize(q);return id;
}
static void timeoff_request_link(int fd,const char*body){char email[256]="";form_value(body,"email",email,sizeof email);
 if(email_address_ok(email)){
 sqlite3_stmt*q=NULL;int employee=0;char name[192]="",base[270]="";
 if(sqlite3_prepare_v2(db,"SELECT id,name FROM employees WHERE lower(email)=lower(?1) AND active=1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,email,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW){employee=sqlite3_column_int(q,0);snprintf(name,sizeof name,"%s",col_text(q,1));}}sqlite3_finalize(q);
 note_link_base(base,sizeof base);
 if(employee&&*base){char token[65],hash[256];if(random_hex(token,32)&&token_hash(token,REVIEW_LINK_SALT,hash,sizeof hash)&&sql("BEGIN IMMEDIATE")){
 int ok=sqlite3_prepare_v2(db,"INSERT INTO timeoff_links(employee_id,token_hash,expires_at,last_sent_at) VALUES(?1,?2,CAST(strftime('%s','now') AS INTEGER)+86400,CAST(strftime('%s','now') AS INTEGER)) ON CONFLICT(employee_id) DO UPDATE SET token_hash=excluded.token_hash,expires_at=excluded.expires_at,last_sent_at=excluded.last_sent_at WHERE timeoff_links.last_sent_at<=CAST(strftime('%s','now') AS INTEGER)-900",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,employee);sqlite3_bind_text(q,2,hash,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE&&sqlite3_changes(db)==1;}sqlite3_finalize(q);
 if(ok){char message[800],key[160];snprintf(message,sizeof message,"Hello %s,\n\nUse this private link to request vacation and view your vacation history:\n\n%s/time-off/%s\n\nThe link expires in 24 hours. Do not forward it.",name,base,token);snprintf(key,sizeof key,"timeoff_link:%d:%s",employee,token);ok=(email_enqueue(db,email,"Your vacation request link",message,key,NULL)>=0);}if(ok)sql("COMMIT");else sql("ROLLBACK");
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
 if(sqlite3_prepare_v2(db,"SELECT email,name FROM employees WHERE id=?1 AND active=1 AND email LIKE '%@%'",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int64(q,1,employee);if(sqlite3_step(q)==SQLITE_ROW){snprintf(email,sizeof email,"%s",col_text(q,0));snprintf(name,sizeof name,"%s",col_text(q,1));}}sqlite3_finalize(q);
 if(!email_address_ok(email)){send_response(fd,409,"application/json","{\"error\":\"Employee must be active and have a valid email address.\"}");return;}
 note_link_base(base,sizeof base);if(!base[0]){send_response(fd,409,"application/json","{\"error\":\"Configure the public HTTPS address first.\"}");return;}
 char token[65],hash[256];if(!random_hex(token,32)||!token_hash(token,REVIEW_LINK_SALT,hash,sizeof hash)||!sql("BEGIN IMMEDIATE")){send_response(fd,500,"application/json","{\"error\":\"Could not create link.\"}");return;}
 int ok=sqlite3_prepare_v2(db,"INSERT INTO timeoff_links(employee_id,token_hash,expires_at,last_sent_at) VALUES(?1,?2,CAST(strftime('%s','now') AS INTEGER)+86400,CAST(strftime('%s','now') AS INTEGER)) ON CONFLICT(employee_id) DO UPDATE SET token_hash=excluded.token_hash,expires_at=excluded.expires_at,last_sent_at=excluded.last_sent_at WHERE timeoff_links.last_sent_at<=CAST(strftime('%s','now') AS INTEGER)-900",-1,&q,NULL)==SQLITE_OK;
 int changed=0;if(ok){sqlite3_bind_int64(q,1,employee);sqlite3_bind_text(q,2,hash,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;changed=sqlite3_changes(db);}sqlite3_finalize(q);
 if(ok&&!changed){sql("ROLLBACK");send_response(fd,200,"application/json","{\"status\":\"already_recent\"}");return;}
 if(ok){char message[900],key[160];snprintf(message,sizeof message,"Hello %s,\n\nUse this private link to request vacation and view your vacation history:\n\n%s/time-off/%s\n\nThe link expires in 24 hours. Do not forward it.",name,base,token);snprintf(key,sizeof key,"timeoff_link:%ld:%s",employee,token);ok=(email_enqueue(db,email,"Your vacation request link",message,key,NULL)>=0);}
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
static void timeoff_list(int fd,int employee){sqlite3_stmt*q=NULL;sqlite3_str*o=sqlite3_str_new(db);sqlite3_str_appendall(o,"{\"requests\":[");int n=0,failed=0;
 if(sqlite3_prepare_v2(db,"SELECT id,kind,starts,ends,note,CASE WHEN cancelled_at IS NOT NULL THEN 'cancelled' ELSE status END,coalesce(decision_note,''),requested_at,coalesce(requested_days,0),coalesce(return_date,''),EXISTS(SELECT 1 FROM timeoff_audit a WHERE a.request_id=timeoff_requests.id AND a.action='recorded_approved') FROM timeoff_requests WHERE employee_id=?1 ORDER BY id DESC LIMIT 250",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,employee);while(sqlite3_step(q)==SQLITE_ROW){if(n++)sqlite3_str_appendchar(o,1,',');sqlite3_str_appendf(o,"{\"id\":%d,\"kind\":",sqlite3_column_int(q,0));json_text(o,col_text(q,1));sqlite3_str_appendall(o,",\"starts\":");json_text(o,col_text(q,2));sqlite3_str_appendall(o,",\"ends\":");json_text(o,col_text(q,3));sqlite3_str_appendall(o,",\"note\":");json_text(o,col_text(q,4));sqlite3_str_appendall(o,",\"status\":");json_text(o,col_text(q,5));sqlite3_str_appendall(o,",\"decision_note\":");json_text(o,col_text(q,6));sqlite3_str_appendall(o,",\"requested_at\":");json_text(o,col_text(q,7));sqlite3_str_appendf(o,",\"requested_days\":%d,\"return_date\":",sqlite3_column_int(q,8));json_text(o,col_text(q,9));sqlite3_str_appendf(o,",\"manager_recorded\":%s",sqlite3_column_int(q,10)?"true":"false");if(!timeoff_request_calendar(o,employee,col_text(q,1),col_text(q,2),col_text(q,3),sqlite3_column_int(q,8))){failed=1;break;}sqlite3_str_appendchar(o,1,'}');}}sqlite3_finalize(q);if(failed){sqlite3_free(sqlite3_str_finish(o));send_response(fd,500,"application/json","{\"error\":\"Cannot calculate vacation calendar.\"}");return;}sqlite3_str_appendall(o,"]}");send_str(fd,"application/json",o);
}
static void timeoff_submit(int fd,int employee,int manager,const char*body){char kind[24]="",starts[24]="",ends[24]="",note[501]="",daysbuf[16]="",return_date[24]="";if(!form_value(body,"requested_days",daysbuf,sizeof daysbuf)||(form_has(body,"return_date")&&!form_value(body,"return_date",return_date,sizeof return_date))||!form_value(body,"kind",kind,sizeof kind)||!form_value(body,"starts",starts,sizeof starts)||!form_value(body,"ends",ends,sizeof ends)||(form_has(body,"note")&&!form_value_multiline(body,"note",note,sizeof note))){send_response(fd,422,"application/json","{\"error\":\"Check the form fields and keep the note within 500 characters.\"}");return;}
 if(strcmp(kind,"vacation")||!date_valid(starts)||!date_valid(ends)||strcmp(starts,ends)>0||strpbrk(note,"\r")){send_response(fd,422,"application/json","{\"error\":\"Only vacation can be requested here. Check the dates.\"}");return;}
 if(!*return_date)timeoff_suggest_return(employee,ends,return_date);
 if(!date_valid(return_date)||strcmp(return_date,ends)<=0||!daysbuf[0]||strspn(daysbuf,"0123456789")!=strlen(daysbuf)) {send_response(fd,422,"application/json","{\"error\":\"Confirm the number of days and a return date after the requested dates.\"}");return;}
 /* Reject stale, unbounded, or overlapping requests before writing anything. */
 sqlite3_stmt *validation=NULL;int date_ok=0;
 if(sqlite3_prepare_v2(db,"SELECT (?3>0 OR date(?1)>=date('now','-3 days')) AND julianday(?2)-julianday(?1) BETWEEN 0 AND 365",-1,&validation,NULL)==SQLITE_OK){sqlite3_bind_text(validation,1,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(validation,2,ends,-1,SQLITE_TRANSIENT);sqlite3_bind_int(validation,3,manager);date_ok=sqlite3_step(validation)==SQLITE_ROW&&sqlite3_column_int(validation,0);}
 sqlite3_finalize(validation);
 if(!date_ok){send_response(fd,422,"application/json","{\"error\":\"Choose valid dates within one year. Employee requests must start recently or in the future.\"}");return;}
 int estimated=0,calculated=timeoff_day_count(employee,starts,ends,&estimated);
 if(calculated==0){timeoff_no_vacation_days(fd,starts,ends);return;}
 if(calculated<0||strlen(daysbuf)>3||atoi(daysbuf)!=calculated){send_response(fd,422,"application/json","{\"error\":\"Requested days do not match the calculated total. Check the dates and schedule with your manager.\"}");return;}
 if(!begin_change(fd))return;
 sqlite3_stmt *overlap=NULL;int conflict=1;
 if(sqlite3_prepare_v2(db,"SELECT EXISTS(SELECT 1 FROM timeoff_requests WHERE employee_id=?1 AND status IN ('pending','approved') AND cancelled_at IS NULL AND starts<=?3 AND ends>=?2)",-1,&overlap,NULL)==SQLITE_OK){sqlite3_bind_int(overlap,1,employee);sqlite3_bind_text(overlap,2,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(overlap,3,ends,-1,SQLITE_TRANSIENT);if(sqlite3_step(overlap)==SQLITE_ROW)conflict=sqlite3_column_int(overlap,0);}
 sqlite3_finalize(overlap);
 if(conflict){sql("ROLLBACK");send_response(fd,409,"application/json","{\"error\":\"These dates overlap an existing pending or approved request.\"}");return;}
 sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(db,"INSERT INTO timeoff_requests(employee_id,kind,starts,ends,note,requested_days,return_date,acknowledged_at,status,manager_id,decision_note,decided_at) VALUES(?1,?2,?3,?4,?5,?6,?7,CASE WHEN ?8>0 THEN NULL ELSE strftime('%Y-%m-%dT%H:%M:%fZ','now') END,CASE WHEN ?8>0 THEN 'approved' ELSE 'pending' END,NULLIF(?8,0),CASE WHEN ?8>0 THEN ?5 ELSE '' END,CASE WHEN ?8>0 THEN strftime('%Y-%m-%dT%H:%M:%fZ','now') ELSE NULL END)",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,employee);sqlite3_bind_text(q,2,kind,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,4,ends,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,5,note,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,6,calculated);sqlite3_bind_text(q,7,return_date,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,8,manager);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);sqlite3_int64 id=sqlite3_last_insert_rowid(db);
 if(ok&&sqlite3_prepare_v2(db,"INSERT INTO timeoff_audit(request_id,actor_id,action) VALUES(?1,?2,?3)",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int64(q,1,id);sqlite3_bind_int(q,2,manager?manager:employee);sqlite3_bind_text(q,3,manager?"recorded_approved":"requested",-1,SQLITE_STATIC);ok=sqlite3_step(q)==SQLITE_DONE;}else ok=0;sqlite3_finalize(q);
 /* Manager entries are already approved elsewhere: audit and commit without emails. */
 if(manager){if(ok)ok=sql("COMMIT");else sql("ROLLBACK");send_response(fd,ok?200:500,"application/json",ok?"{\"status\":\"Approved vacation recorded.\"}":"{\"error\":\"Vacation could not be recorded. Try again.\"}");return;}
 char name[192]="",email[256]="";if(ok&&sqlite3_prepare_v2(db,"SELECT name,coalesce(email,'') FROM employees WHERE id=?1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,employee);if(sqlite3_step(q)==SQLITE_ROW){snprintf(name,sizeof name,"%s",col_text(q,0));snprintf(email,sizeof email,"%s",col_text(q,1));}}sqlite3_finalize(q);
 char message[1000],subject[200],key[100];snprintf(subject,sizeof subject,"Vacation requested: %s to %s",starts,ends);snprintf(message,sizeof message,"%s requested %s time off from %s to %s (%d working %s, returns %s).\n\nNotes: %s\n\nStatus: Pending. Review under Vacation.",name,kind,starts,ends,calculated,calculated==1?"day":"days",return_date,*note?note:"None");
 if(ok&&email_address_ok(email)){char employee_message[900],employee_subject[200];snprintf(employee_subject,sizeof employee_subject,"Vacation request received: %s to %s",starts,ends);snprintf(employee_message,sizeof employee_message,"Hello %s,\n\nYour %s request for %s to %s was sent to your manager.\n\nStatus: Pending. You will receive a message when a decision is made.",name,kind,starts,ends);char days_label[40];snprintf(days_label,sizeof days_label,"%d working %s",calculated,calculated==1?"day":"days");const struct email_field ef[]={{"{{days_label}}",days_label},{"{{employee_name}}",name},{"{{request_type}}",kind},{"{{period_start}}",starts},{"{{period_end}}",ends},{"{{days}}",daysbuf},{"{{return_date}}",return_date}};
 if(template_render(db,"timeoff_received_employee",ef,7,employee_subject,sizeof employee_subject,employee_message,sizeof employee_message)>0){snprintf(key,sizeof key,"timeoff_ack:%lld",(long long)id);ok=(email_enqueue(db,email,employee_subject,employee_message,key,NULL)>=0);}}
 if(ok){sqlite3_stmt*m=NULL;if(sqlite3_prepare_v2(db,"SELECT e.email FROM employees e JOIN employee_roles r ON r.employee_id=e.id AND r.role='manager' WHERE e.active=1 AND e.email LIKE '%@%'",-1,&m,NULL)==SQLITE_OK){while(sqlite3_step(m)==SQLITE_ROW){const char*addr=col_text(m,0);if(!email_address_ok(addr))continue;snprintf(key,sizeof key,"timeoff_manager:%lld:%s",(long long)id,addr);char ms[200],mb[1000];snprintf(ms,sizeof ms,"%s",subject);snprintf(mb,sizeof mb,"%s",message);
 const struct email_field mf[]={{"{{employee_name}}",name},{"{{request_type}}",kind},{"{{period_start}}",starts},{"{{period_end}}",ends},{"{{days}}",daysbuf},{"{{return_date}}",return_date},{"{{note}}",*note?note:"None"}};
 if(template_render(db,"timeoff_received_manager",mf,7,ms,sizeof ms,mb,sizeof mb)>0&&!(email_enqueue(db,addr,ms,mb,key,NULL)>=0))ok=0;}}sqlite3_finalize(m);}
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");send_response(fd,ok?200:500,"application/json",ok?"{\"status\":\"Vacation request submitted. Your manager will review it.\"}":"{\"error\":\"Request could not be saved. Try again.\"}");
}
/* No new schema: creation source is recorded in timeoff_audit. */
static void timeoff_manager_entry(int fd,int manager,const char *body,int preview){
 char text[24]="",starts[24]="",ends[24]="";
 if(manager<=0){send_response(fd,401,"application/json","{\"error\":\"Sign in again.\"}");return;}
 if(!form_value(body,"employee_id",text,sizeof text)){send_response(fd,422,"application/json","{\"error\":\"Select an employee.\"}");return;}
 int employee=ar_employee(text),found=0;sqlite3_stmt *q=NULL;
 if(employee&&sqlite3_prepare_v2(db,"SELECT 1 FROM employees WHERE id=?1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,employee);found=sqlite3_step(q)==SQLITE_ROW;}sqlite3_finalize(q);
 if(!found){send_response(fd,422,"application/json","{\"error\":\"Select an existing employee.\"}");return;}
 if(preview){if(!form_value(body,"starts",starts,sizeof starts)||!form_value(body,"ends",ends,sizeof ends)){send_response(fd,422,"application/json","{\"error\":\"Choose valid dates.\"}");return;}timeoff_day_preview(fd,employee,starts,ends);return;}
 timeoff_submit(fd,employee,manager,body);
}
static void timeoff_manager_list(int fd,const char *path){
 char view[16]="all",employee_text[24]="",year[8]="",offset_text[24]="0";const char *query=strchr(path,'?');
 if(query){const char *keys[]={"view","employee_id","year","offset"};char *values[]={view,employee_text,year,offset_text};size_t sizes[]={sizeof view,sizeof employee_text,sizeof year,sizeof offset_text};for(int i=0;i<4;i++)if(form_has(query+1,keys[i])&&!op_query_param(path,keys[i],values[i],sizes[i])){send_response(fd,400,"application/json","{\"error\":\"Invalid vacation filter.\"}");return;}}
 int employee=*employee_text?ar_employee(employee_text):0;char *tail=NULL;long offset=strtol(offset_text,&tail,10);
 if((strcmp(view,"all")&&strcmp(view,"waiting")&&strcmp(view,"upcoming")&&strcmp(view,"past"))||(*employee_text&&!employee)||(*year&&(strlen(year)!=4||strspn(year,"0123456789")!=4))||!offset_text[0]||strspn(offset_text,"0123456789")!=strlen(offset_text)||!tail||*tail||offset<0||offset>2147483647){send_response(fd,400,"application/json","{\"error\":\"Choose valid vacation filters.\"}");return;}
 char first[16]="",last[16]="";if(*year){snprintf(first,sizeof first,"%s-01-01",year);snprintf(last,sizeof last,"%s-12-31",year);}
 const char *bucket=!strcmp(view,"waiting")?"t.status='pending' AND t.cancelled_at IS NULL":!strcmp(view,"upcoming")?"(t.status<>'pending' OR t.cancelled_at IS NOT NULL) AND t.ends>=date('now','localtime')":!strcmp(view,"past")?"(t.status<>'pending' OR t.cancelled_at IS NOT NULL) AND t.ends<date('now','localtime')":"1";
 const char *order=!strcmp(view,"past")?"t.starts DESC,e.name COLLATE NOCASE,e.id,t.id DESC":!strcmp(view,"all")?"(t.status='pending' AND t.cancelled_at IS NULL) DESC,t.starts DESC,e.name COLLATE NOCASE,e.id,t.id DESC":"t.starts,e.name COLLATE NOCASE,e.id,t.id";
 char *where=sqlite3_mprintf("(%s) AND (?1=0 OR t.employee_id=?1) AND (?2='' OR (t.starts<=?3 AND t.ends>=?2))",bucket);
 char *count_sql=where?sqlite3_mprintf("SELECT count(*) FROM timeoff_requests t WHERE %s",where):NULL;
 sqlite3_stmt *q=NULL;int total=0,ok=count_sql&&sqlite3_prepare_v2(db,count_sql,-1,&q,NULL)==SQLITE_OK;sqlite3_free(count_sql);
 if(ok){sqlite3_bind_int(q,1,employee);sqlite3_bind_text(q,2,first,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,last,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_ROW;if(ok)total=sqlite3_column_int(q,0);}sqlite3_finalize(q);q=NULL;
 char *list_sql=ok?sqlite3_mprintf("SELECT t.id,e.name,t.kind,t.starts,t.ends,t.note,CASE WHEN t.cancelled_at IS NOT NULL THEN 'cancelled' ELSE t.status END,coalesce(t.decision_note,''),coalesce(t.requested_days,0),coalesce(t.return_date,''),t.id IN (SELECT request_id FROM timeoff_audit WHERE action='recorded_approved'),e.id,t.ends<date('now','localtime') FROM timeoff_requests t JOIN employees e ON e.id=t.employee_id WHERE %s ORDER BY %s LIMIT 50 OFFSET ?4",where,order):NULL;sqlite3_free(where);
 ok=ok&&list_sql&&sqlite3_prepare_v2(db,list_sql,-1,&q,NULL)==SQLITE_OK;sqlite3_free(list_sql);
 if(!ok){sqlite3_finalize(q);send_response(fd,500,"application/json","{\"error\":\"Cannot load vacation records.\"}");return;}
 sqlite3_bind_int(q,1,employee);sqlite3_bind_text(q,2,first,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,last,-1,SQLITE_TRANSIENT);sqlite3_bind_int64(q,4,offset);
 sqlite3_str *out=sqlite3_str_new(db);sqlite3_str_appendall(out,"{\"requests\":[");int n=0,rc;
 while((rc=sqlite3_step(q))==SQLITE_ROW){
  if(n++){sqlite3_str_appendchar(out,1,',');}sqlite3_str_appendf(out,"{\"id\":%d,\"employee_id\":%d,\"name\":",sqlite3_column_int(q,0),sqlite3_column_int(q,11));json_text(out,col_text(q,1));
  const char *labels[]={"kind","starts","ends","note","status","decision_note"};for(int i=0;i<6;i++){sqlite3_str_appendf(out,",\"%s\":",labels[i]);json_text(out,col_text(q,i+2));}
  sqlite3_str_appendf(out,",\"requested_days\":%d,\"return_date\":",sqlite3_column_int(q,8));json_text(out,col_text(q,9));sqlite3_str_appendf(out,",\"manager_recorded\":%s",sqlite3_column_int(q,10)?"true":"false");
  char number[65];employee_number(db,sqlite3_column_int(q,11),number,sizeof number);sqlite3_str_appendall(out,",\"display_id\":");json_text(out,number);
  sqlite3_str_appendf(out,",\"dates_passed\":%s",sqlite3_column_int(q,12)?"true":"false");
  if(!timeoff_request_calendar(out,sqlite3_column_int(q,11),col_text(q,2),col_text(q,3),col_text(q,4),sqlite3_column_int(q,8))){ok=0;break;}sqlite3_str_appendchar(out,1,'}');
 }
 sqlite3_finalize(q);q=NULL;
 if(!ok||rc!=SQLITE_DONE){sqlite3_free(sqlite3_str_finish(out));send_response(fd,500,"application/json","{\"error\":\"Cannot calculate vacation records.\"}");return;}
 sqlite3_str_appendf(out,"],\"total\":%d,\"offset\":%ld,\"limit\":50,\"has_more\":%s,\"years\":[",total,offset,offset+n<total?"true":"false");
 ok=sqlite3_prepare_v2(db,"SELECT substr(starts,1,4) AS year FROM timeoff_requests UNION SELECT substr(ends,1,4) FROM timeoff_requests ORDER BY year DESC",-1,&q,NULL)==SQLITE_OK;n=0;
 if(ok){while((rc=sqlite3_step(q))==SQLITE_ROW){if(n++)sqlite3_str_appendchar(out,1,',');json_text(out,col_text(q,0));}}sqlite3_finalize(q);
 if(!ok||rc!=SQLITE_DONE){sqlite3_free(sqlite3_str_finish(out));send_response(fd,500,"application/json","{\"error\":\"Cannot load vacation years.\"}");return;}
 sqlite3_str_appendall(out,"]}");send_str(fd,"application/json",out);
}
static void timeoff_manager_decide(int fd,int manager,const char*body){char idtxt[24]="",action[24]="",note[501]="";form_value(body,"id",idtxt,sizeof idtxt);form_value(body,"action",action,sizeof action);form_value(body,"note",note,sizeof note);int id=ar_employee(idtxt);
 if(!id||(strcmp(action,"approved")&&strcmp(action,"denied")&&strcmp(action,"cancelled"))){send_response(fd,422,"application/json","{\"error\":\"Choose Approve, Deny or Cancel.\"}");return;}
 if(!begin_change(fd))return;
 sqlite3_stmt*q=NULL;int cancelling=!strcmp(action,"cancelled");
 const char *update_sql=cancelling?
  "UPDATE timeoff_requests SET cancelled_at=strftime('%Y-%m-%dT%H:%M:%fZ','now'),manager_id=?2,decision_note=?3 WHERE id=?4 AND status='approved' AND cancelled_at IS NULL":
  "UPDATE timeoff_requests SET status=?1,manager_id=?2,decision_note=?3,decided_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=?4 AND status='pending'";
 int ok=sqlite3_prepare_v2(db,update_sql,-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,action,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,2,manager);sqlite3_bind_text(q,3,note,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,4,id);ok=sqlite3_step(q)==SQLITE_DONE&&sqlite3_changes(db)==1;}sqlite3_finalize(q);q=NULL;
 if(ok&&sqlite3_prepare_v2(db,"INSERT INTO timeoff_audit(request_id,actor_id,action) VALUES(?1,?2,?3)",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,id);sqlite3_bind_int(q,2,manager);sqlite3_bind_text(q,3,action,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}else ok=0;sqlite3_finalize(q);q=NULL;
 char email[256]="",name[192]="",starts[16]="",ends[16]="",mgr[192]="",kind[24]="";
 if(ok&&sqlite3_prepare_v2(db,"SELECT e.email,e.name,t.starts,t.ends,(SELECT name FROM employees WHERE id=?2),t.kind FROM timeoff_requests t JOIN employees e ON e.id=t.employee_id WHERE t.id=?1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,id);sqlite3_bind_int(q,2,manager);if(sqlite3_step(q)==SQLITE_ROW){snprintf(email,sizeof email,"%s",col_text(q,0));snprintf(name,sizeof name,"%s",col_text(q,1));snprintf(starts,sizeof starts,"%s",col_text(q,2));snprintf(ends,sizeof ends,"%s",col_text(q,3));snprintf(mgr,sizeof mgr,"%s",col_text(q,4));snprintf(kind,sizeof kind,"%s",col_text(q,5));}}sqlite3_finalize(q);q=NULL;
 const char *type=!strcmp(kind,"vacation")?"Vacation":!strcmp(kind,"personal")?"Personal leave":!strcmp(kind,"unpaid")?"Unpaid leave":"Leave";
 if(ok&&email_address_ok(email)){char subject[180],message[900],key[100];snprintf(subject,sizeof subject,"%s request %s: %s to %s",type,action,starts,ends);snprintf(message,sizeof message,"Hello %s,\n\nYour %s request for %s to %s was %s by %s.\n\nManager note: %s\n\nYou can view your request history through the Vacation page.",name,type,starts,ends,action,mgr,*note?note:"None");const struct email_field df[]={{"{{request_type}}",type},{"{{employee_name}}",name},{"{{period_start}}",starts},{"{{period_end}}",ends},{"{{decision}}",action},{"{{manager_name}}",mgr},{"{{note}}",*note?note:"None"}};
 const char *template_id=cancelling?"timeoff_cancelled_employee":"timeoff_decision_employee";
 if(template_render(db,template_id,df,7,subject,sizeof subject,message,sizeof message)>0){snprintf(key,sizeof key,"timeoff_decision:%d:%s",id,action);ok=(email_enqueue(db,email,subject,message,key,NULL)>=0);}}
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");send_response(fd,ok?200:409,"application/json",ok?"{\"status\":\"Decision saved.\"}":"{\"error\":\"This request has already been decided or could not be saved.\"}");
}
#define TIMEOFF_CALENDAR_JS "function defaultVacationEnd(form,previous){const start=form.elements.starts.value,end=form.elements.ends;if(start&&(!end.value||end.value===previous||end.value<start))end.value=start;return start}function vacationDays(x){const d=x.days??x.requested_days??0;if(x.kind&&x.kind!=='vacation')return d+' '+(d===1?'day':'days');const n=x.stat_days||0;return d+' vacation '+(d===1?'day':'days')+(n?' + '+n+' stat '+(n===1?'holiday':'holidays'):'')}function holidayDetails(x){return (x.holidays||[]).map(h=>'Stat holiday: '+h.name+' ('+h.date+'). Not counted as vacation.').join(' ')}function originalDaysNote(x){return x.kind==='vacation'&&x.requested_days>0&&x.days!==x.requested_days?'Calendar changed: originally '+x.requested_days+' vacation '+(x.requested_days===1?'day':'days')+'.':''}"
#define TIMEOFF_FIELDS "<input type='hidden' name='kind' value='vacation'><div class='grid'><label>Starting date<input type='date' name='starts' required></label><label>Ending date<input type='date' name='ends' required></label></div><p id='dayshint' class='muted' aria-live='polite'>Select dates to calculate working days.</p><input type='hidden' name='requested_days'><div class='grid'><label>Return to work (suggested)<input type='date' name='return_date'></label></div><label>Notes (optional)<textarea name='note' maxlength='500' placeholder='Optional details'></textarea></label>"
static void timeoff_public_page(int fd){send_response(fd,200,"text/html; charset=utf-8",UI_HEAD_LOGIN("Vacation")
 "<script>document.body.classList.add('timeoff-public')</script><h1>Vacation</h1><p>Use your work email to request vacation and review earlier requests.</p>"
 "<section><h2>Get your vacation link</h2><form id='access'><label>Work email<input type='email' id='email' autocomplete='email' required maxlength='250' placeholder='you@company.com'></label><div class='actions'><button>Send private link</button></div></form><p id='message' role='status' aria-live='polite'></p><p class='muted'>Your link expires in 24 hours. Requests are reviewed by your manager.</p></section>"
 "<script>const access=document.getElementById('access');access.onsubmit=async e=>{e.preventDefault();let b=access.querySelector('button');b.disabled=true;try{let r=await tc.request('/time-off/link',{method:'POST',body:new URLSearchParams({email:document.getElementById('email').value})});let j=await tc.json(r);if(!r.ok)throw Error(j.error||'Link request failed');tc.ok(document.getElementById('message'),'Check your email now. The link works for 24 hours.');access.hidden=true}catch(e){tc.err(document.getElementById('message'),e)}finally{b.disabled=false}}</script>" UI_FOOT);}
static void timeoff_employee_page(int fd){send_response(fd,200,"text/html; charset=utf-8",UI_HEAD_LOGIN("Your vacation")
 "<script>document.body.classList.add('timeoff-employee')</script><h1>Your vacation</h1><p>Request vacation and review your vacation history.</p>"
 "<section><h2>Request vacation</h2><p id='vacationUsage' role='status' aria-live='polite'></p><p class='muted'>Calendar-year usage counts approved vacation, including future approved dates. This is informational; you may request more than your allowance.</p><form id='newrequest'>" TIMEOFF_FIELDS "<div class='actions'><button>Request vacation</button></div></form><p id='status' role='status' aria-live='polite'></p></section>"
 "<section><div class='section-head'><h2>Vacation history</h2></div><div id='history' aria-live='polite'><p class='muted'>Loading requests...</p></div></section>"
 "<script>" TIMEOFF_CALENDAR_JS "let base=location.pathname;const api=tc.api;"
 "function requestType(k){return {vacation:'Vacation',personal:'Earlier personal leave',unpaid:'Earlier unpaid leave',other:'Earlier other leave'}[k]||k}function flagStatus(s){let kind={pending:'check',approved:'ok',denied:'problem',cancelled:'muted'}[s]||'muted';let f=document.createElement('span');f.className='flag flag-'+kind;f.textContent=s==='pending'?'Waiting':s.charAt(0).toUpperCase()+s.slice(1);return f}"
 "async function load(){let j=await api(base+'/list'),h=document.getElementById('history');h.replaceChildren();if(!j.requests.length){let p=document.createElement('p');p.className='muted';p.textContent='No vacation requests yet.';h.append(p);return}let list=document.createElement('div');list.className='timeoff-history';for(let x of j.requests){let item=document.createElement('article');item.className='timeoff-history-item';let top=document.createElement('div');top.className='timeoff-history-head';top.append(flagStatus(x.status));let dates=document.createElement('strong');dates.textContent=x.starts+' to '+x.ends+' · '+vacationDays(x);top.append(dates);item.append(top);let detail=document.createElement('p');detail.className='muted';detail.textContent=requestType(x.kind)+(x.return_date?' · Returns '+x.return_date:'')+(x.manager_recorded?' · Recorded by manager':'');item.append(detail);for(const text of [holidayDetails(x),originalDaysNote(x)])if(text){let calendar=document.createElement('p');calendar.className='muted';calendar.textContent=text;item.append(calendar)}if(x.manager_recorded&&x.note&&x.note!==x.decision_note){let recorded=document.createElement('p');recorded.textContent='Original manager note: '+x.note;item.append(recorded)}if(x.decision_note){let note=document.createElement('p');note.textContent='Manager note: '+x.decision_note;item.append(note)}list.append(item)}h.append(list);if(j.requests.some(x=>x.status==='pending')){let historySection=h.closest('section'),formSection=newrequest.closest('section');if(historySection&&formSection)formSection.before(historySection)}}"
 "let vacationLoad=0;async function loadVacation(){const request=++vacationLoad;let years=[newrequest.elements.starts.value.slice(0,4),newrequest.elements.ends.value.slice(0,4)].filter(Boolean);years=[...new Set(years)];if(!years.length)years=[''];try{let rows=await Promise.all(years.map(y=>api(base+'/vacation'+(y?'?year='+encodeURIComponent(y):''))));if(request!==vacationLoad)return;document.getElementById('vacationUsage').textContent=rows.map(x=>x.year+': Vacation: '+x.used+' of '+(x.allowed===null?'unspecified':x.allowed)+' days used.').join(' ')}catch(e){tc.err(document.getElementById('vacationUsage'),e)}}"
 "let dayCount=null,calendarSerial=0,previousStart='';async function recalc(){const serial=++calendarSerial;let f=new FormData(newrequest),s=f.get('starts'),e=f.get('ends'),hint=document.getElementById('dayshint');loadVacation();dayCount=null;if(!s||!e||s>e){hint.textContent='Select valid starting and ending dates.';return}try{let j=await api(base+'/days?starts='+encodeURIComponent(s)+'&ends='+encodeURIComponent(e));if(serial!==calendarSerial)return;dayCount=j.days;newrequest.elements.requested_days.value=j.days;if(j.suggested_return)newrequest.elements.return_date.value=j.suggested_return;hint.textContent=vacationDays(j)+'. '+holidayDetails(j)+(j.estimate?' Weekday estimate; no fixed weekly schedule.':'')}catch(err){if(serial===calendarSerial)hint.textContent=err.message}}newrequest.elements.starts.addEventListener('change',()=>{previousStart=defaultVacationEnd(newrequest,previousStart);recalc()});newrequest.elements.ends.addEventListener('change',recalc);"
 "newrequest.onsubmit=async e=>{e.preventDefault();let b=newrequest.querySelector('button');try{let f=new FormData(newrequest);if(f.get('starts')>f.get('ends'))throw Error('Ending date must be on or after the starting date.');if(dayCount===null||Number(f.get('requested_days'))!==dayCount)throw Error('The day total does not match the calculated working days.');b.disabled=true;await api(base+'/request',{method:'POST',body:new URLSearchParams(f)});++calendarSerial;previousStart='';newrequest.reset();dayCount=null;document.getElementById('dayshint').textContent='Select dates to calculate working days.';tc.ok(document.getElementById('status'),'Vacation request submitted. Your manager will review it.');await load();await loadVacation()}catch(e){tc.err(document.getElementById('status'),e)}finally{b.disabled=false}};loadVacation();load().catch(e=>tc.err(document.getElementById('status'),e))</script>" UI_FOOT);}
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
  "AND t.starts<=?2 AND t.ends>=?1 ORDER BY e.name,e.id,t.starts,t.id";
 if(sqlite3_prepare_v2(db,query,-1,&q,NULL)!=SQLITE_OK){send_response(fd,500,"application/json","{\"error\":\"Cannot prepare vacation report.\"}");return;}
 sqlite3_bind_text(q,1,first,-1,SQLITE_TRANSIENT);
 sqlite3_bind_text(q,2,last,-1,SQLITE_TRANSIENT);
 sqlite3_str *out=sqlite3_str_new(db);
 int rc,failed=0;
 sqlite3_str_appendall(out,"Employee ID,Employee,Annual allowance (days),Approved days,Remaining days\r\n");
 sqlite3_stmt *employees=NULL;
 const char *annual_query="SELECT e.id,e.name FROM employees e WHERE e.vacation_days IS NOT NULL OR EXISTS(SELECT 1 FROM timeoff_requests t WHERE t.employee_id=e.id AND t.kind='vacation' AND t.status='approved' AND t.cancelled_at IS NULL AND t.starts<=?2 AND t.ends>=?1) ORDER BY e.name,e.id";
 if(sqlite3_prepare_v2(db,annual_query,-1,&employees,NULL)!=SQLITE_OK){sqlite3_finalize(q);sqlite3_str_finish(out);send_response(fd,500,"application/json","{\"error\":\"Cannot read annual allowances.\"}");return;}
 sqlite3_bind_text(employees,1,first,-1,SQLITE_TRANSIENT);sqlite3_bind_text(employees,2,last,-1,SQLITE_TRANSIENT);
 while((rc=sqlite3_step(employees))==SQLITE_ROW){
  int employee=sqlite3_column_int(employees,0),annual=0,specified=0;double allowed=0;
  if(!vacation_year_usage(db,employee,atoi(year),&annual,&allowed,&specified)){failed=1;break;}
  char num[65],limit[32]="",remaining[32]="";employee_number(db,employee,num,sizeof num);op_csv(out,num);
  sqlite3_str_appendchar(out,1,',');op_csv(out,col_text(employees,1));
  if(specified){snprintf(limit,sizeof limit,"%g",allowed);snprintf(remaining,sizeof remaining,"%g",allowed-annual);}
  sqlite3_str_appendchar(out,1,',');op_csv(out,limit);
  snprintf(num,sizeof num,"%d",annual);sqlite3_str_appendchar(out,1,',');op_csv(out,num);
  sqlite3_str_appendchar(out,1,',');op_csv(out,remaining);sqlite3_str_appendall(out,"\r\n");
 }
 sqlite3_finalize(employees);
 if(failed||rc!=SQLITE_DONE){sqlite3_finalize(q);sqlite3_str_finish(out);send_response(fd,500,"application/json","{\"error\":\"Cannot calculate annual vacation.\"}");return;}
 sqlite3_str_appendall(out,"\r\nEmployee ID,Employee,Start,End,Days,Remaining days,Manager note\r\n");
 int previous_employee=0,cumulative=0;char covered[16]="";
 while((rc=sqlite3_step(q))==SQLITE_ROW){
  int employee=sqlite3_column_int(q,1),estimate=0;
  const char *starts=col_text(q,5),*ends=col_text(q,6);
  int days=timeoff_day_count(employee,starts,ends,&estimate);
  if(days<0){failed=1;break;}
  if(employee!=previous_employee){previous_employee=employee;cumulative=0;covered[0]=0;}
  int overlap=0,overlap_estimate=0;
  if(*covered&&strcmp(starts,covered)<=0){
   overlap=timeoff_day_count(employee,starts,strcmp(ends,covered)<0?ends:covered,&overlap_estimate);
   if(overlap<0){failed=1;break;}
  }
  days-=overlap;cumulative+=days;
  if(!*covered||strcmp(ends,covered)>0)snprintf(covered,sizeof covered,"%s",ends);
  int annual=0,specified=0;double allowed=0;
  if(!vacation_year_usage(db,employee,atoi(year),&annual,&allowed,&specified)){failed=1;break;}
  char number[65],remaining[32]="";employee_number(db,employee,number,sizeof number);op_csv(out,number);
  sqlite3_str_appendchar(out,1,',');op_csv(out,col_text(q,2));
  sqlite3_str_appendchar(out,1,',');op_csv(out,starts);
  sqlite3_str_appendchar(out,1,',');op_csv(out,ends);
  snprintf(number,sizeof number,"%d",days);sqlite3_str_appendchar(out,1,',');op_csv(out,number);
  if(specified)snprintf(remaining,sizeof remaining,"%g",allowed-cumulative);
  sqlite3_str_appendchar(out,1,',');op_csv(out,remaining);
  sqlite3_str_appendchar(out,1,',');
  if(estimate||overlap){char note[750];snprintf(note,sizeof note,"%s%s%s%s%s",estimate?"Estimated days (no schedule)":"",estimate&&overlap?"; ":"",overlap?"Overlapping days already counted":"",*col_text(q,10)?": ":"",col_text(q,10));op_csv(out,note);}
  else op_csv(out,col_text(q,10));
  sqlite3_str_appendall(out,"\r\n");
 }
 sqlite3_finalize(q);
 if(failed||rc!=SQLITE_DONE){sqlite3_str_finish(out);send_response(fd,500,"application/json","{\"error\":\"Cannot calculate vacation details.\"}");return;}
 op_export_send(fd,"text/csv; charset=utf-8",filename,out);
}
static void timeoff_manager_page(int fd){send_response(fd,200,"text/html; charset=utf-8",UI_HEAD("Vacation","time-off")
 "<h1>Vacation</h1><p>Review requests or record vacation approved elsewhere. For other missed attendance, use <a href='/punch-corrections'>Recorded absences</a>.</p>"
 "<section id='waitingSection'><div class='section-head'><h2 id='waitingTitle'>Waiting for approval</h2><div class='actions'><button type='button' id='openVacation' class='primary' disabled>Record vacation</button></div></div><div id='waitingRequests'><p class='muted'>Loading requests...</p></div><div id='waitingPager'></div><p id='status' role='status' aria-live='polite'></p><p id='recordStatus' role='status' aria-live='polite'></p></section>"
 "<section id='historySection'><h2>Vacation history</h2><div class='grid'><label>Employee<select id='filterEmployee'><option value=''>All employees</option></select></label><label>Year<select id='filterYear'><option value=''>All years</option></select></label></div><h3>Upcoming</h3><div id='upcomingRequests'><p class='muted'>Loading...</p></div><div id='upcomingPager'></div><h3>Past</h3><div id='pastRequests'><p class='muted'>Loading...</p></div><div id='pastPager'></div></section>"
 "<section><h2>Annual vacation report</h2><form method='get' action='/api/v1/admin/time-off/annual.csv'><label>Calendar year<input id='vacation-year' name='year' type='number' min='2000' max='2100' required value='2026'></label><div class='actions'><button type='submit' id='vacation-download'>Download Excel-compatible CSV</button></div></form><p class='muted'>Approved vacation, including future bookings; counts use current schedules and holidays.</p></section>"
 "<dialog id='vacationDialog' aria-labelledby='vacationDialogTitle'><div class='section-head'><h2 id='vacationDialogTitle'>Record vacation</h2><div class='actions'><button class='secondary' type='button' id='closeVacation'>Close</button></div></div><p class='muted'>Already-approved vacation, including past dates; no email is sent and the allowance is for reference.</p><form id='recordTimeoff'><label>Employee<select name='employee_id' id='leaveEmployee' required><option value=''>Select an employee</option></select></label>" TIMEOFF_FIELDS "<p id='recordError' role='status' aria-live='polite'></p><div class='actions'><button type='submit' id='recordLeave'>Record vacation</button></div></form></dialog>"
 "<script>" TIMEOFF_CALENDAR_JS
 "const api=tc.api,vacationYear=document.getElementById('vacation-year'),filterEmployee=document.getElementById('filterEmployee'),filterYear=document.getElementById('filterYear');vacationYear.value=String(new Date().getFullYear());"
 "const views={waiting:{offset:0,serial:0},upcoming:{offset:0,serial:0},past:{offset:0,serial:0}};"
 "function statusFlag(s){return tc.flag(s==='pending'?'Waiting':s.charAt(0).toUpperCase()+s.slice(1),{pending:'check',approved:'ok',denied:'problem',cancelled:'muted'}[s]||'muted')}"
 "function fillYears(years){const selected=filterYear.value;filterYear.replaceChildren(new Option('All years',''),...years.map(y=>new Option(y,y)));filterYear.value=years.includes(selected)?selected:''}"
 "function showRows(target,records,view){target.replaceChildren();if(!records.length){const p=document.createElement('p');p.className='muted';p.textContent=view==='waiting'?'No requests waiting for approval.':'No vacation records match these filters.';target.append(p);return}const table=document.createElement('table'),thead=document.createElement('thead'),head=document.createElement('tr');for(const value of ['Employee','Dates','Days','Status','Action']){const th=document.createElement('th');th.scope='col';th.textContent=value;head.append(th)}thead.append(head);table.append(thead);const tbody=document.createElement('tbody');for(const x of records){const row=document.createElement('tr');row.dataset.requestId=x.id;for(const value of [x.name+' (#'+x.display_id+')',x.starts===x.ends?x.starts:x.starts+' to '+x.ends,(x.days??x.requested_days??0)+((x.stat_days||0)?' + '+x.stat_days+' stat':'')]){const td=document.createElement('td');td.textContent=value;row.append(td)}const status=document.createElement('td');status.append(statusFlag(x.status));if(x.status==='pending'&&x.dates_passed){status.append(tc.flag('Dates have passed','check'))}row.append(status);const detailLines=[(x.stat_days||0)?vacationDays(x):'',x.return_date?'Return to work: '+x.return_date:'',x.manager_recorded?'Recorded by manager':'',holidayDetails(x),originalDaysNote(x),x.kind!=='vacation'?({personal:'Earlier personal leave',unpaid:'Earlier unpaid leave',other:'Earlier other leave'}[x.kind]||x.kind):'',x.note,x.decision_note&&x.decision_note!==x.note?'Decision: '+x.decision_note:''].filter(Boolean);if(detailLines.length){const details=document.createElement('details');details.className='vacation-details';const summary=document.createElement('summary');summary.textContent='Details';details.append(summary);for(const text of detailLines){const p=document.createElement('p');p.textContent=text;details.append(p)}row.children[1].append(details)}const actions=document.createElement('td');for(const action of (x.status==='approved'?['cancelled']:x.status==='pending'?['approved','denied']:[])){const button=document.createElement('button');button.type='button';button.className='secondary';button.textContent=action==='approved'?'Approve':action==='cancelled'?'Cancel':'Deny';button.onclick=async()=>{const note=await tc.reason({title:button.textContent+' request?',label:'Manager note (optional)',ok:button.textContent,danger:action!=='approved'});if(note===null)return;button.disabled=true;try{await api('/api/v1/admin/time-off/decide',{method:'POST',body:new URLSearchParams({id:x.id,action,note})});tc.ok(document.getElementById('status'),'Decision saved.');window.dispatchEvent(new Event('timeclock:issues-changed'));await loadAll()}catch(e){tc.err(document.getElementById('status'),e);button.disabled=false}};actions.append(button)}row.append(actions);tbody.append(row)}table.append(tbody);const wrap=document.createElement('div');wrap.className='timeoff-table-scroll';wrap.tabIndex=0;wrap.setAttribute('aria-label',view+' vacation, scroll horizontally for more columns');wrap.append(table);target.append(wrap)}"
 "function showPager(target,data,view){target.replaceChildren();if(data.total<=data.limit)return;const actions=document.createElement('div');actions.className='actions';for(const [label,offset,disabled] of [['Previous',Math.max(0,data.offset-data.limit),data.offset===0],['Next',data.offset+data.limit,!data.has_more]]){const b=document.createElement('button');b.type='button';b.className='secondary';b.textContent=label;b.disabled=disabled;b.setAttribute('aria-label',label+' '+view+' page');b.onclick=()=>{views[view].offset=offset;loadView(view)};actions.append(b)}const count=document.createElement('span');count.className='muted';count.textContent=(data.total?data.offset+1:0)+' to '+Math.min(data.offset+data.limit,data.total)+' of '+data.total;actions.append(count);target.append(actions)}"
 "async function loadView(view){const state=views[view],serial=++state.serial,target=document.getElementById(view+'Requests'),pager=document.getElementById(view+'Pager'),loading=document.createElement('p');loading.className='muted';loading.textContent='Loading...';target.replaceChildren(loading);pager.replaceChildren();const params=new URLSearchParams({view,offset:state.offset});if(view!=='waiting'){params.set('employee_id',filterEmployee.value);params.set('year',filterYear.value)}try{const data=await api('/api/v1/admin/time-off?'+params);if(serial!==state.serial)return;if(data.total&&state.offset>=data.total){state.offset=Math.floor((data.total-1)/data.limit)*data.limit;return loadView(view)}showRows(target,data.requests,view);showPager(pager,data,view);fillYears(data.years);if(view==='waiting')document.getElementById('waitingTitle').textContent='Waiting for approval'+(data.total?' ('+data.total+')':'')}catch(e){if(serial!==state.serial)return;const error=document.createElement('p');target.replaceChildren(error);tc.err(error,e)}}"
 "async function loadHistory(){await Promise.all([loadView('upcoming'),loadView('past')])}async function loadAll(){await Promise.all([loadView('waiting'),loadHistory()])}"
 "for(const control of [filterEmployee,filterYear])control.addEventListener('change',()=>{views.upcoming.offset=0;views.past.offset=0;loadHistory()});"
 "const dialog=document.getElementById('vacationDialog'),entry=document.getElementById('recordTimeoff'),entryError=document.getElementById('recordError'),entryStatus=document.getElementById('recordStatus'),entryButton=document.getElementById('recordLeave'),entryHint=document.getElementById('dayshint'),openButton=document.getElementById('openVacation');let previewSerial=0,dayCount=null,previousStart='';"
 "function resetEntry(){++previewSerial;dayCount=null;previousStart='';entry.reset();entryError.textContent='';entryHint.textContent='Select dates to calculate working days.';entryButton.disabled=false}"
 "openButton.onclick=()=>{resetEntry();entry.elements.employee_id.value=filterEmployee.value;dialog.showModal()};document.getElementById('closeVacation').onclick=()=>dialog.close();dialog.addEventListener('close',()=>{++previewSerial});"
 "async function recalcEntry(){const serial=++previewSerial;dayCount=null;entry.elements.requested_days.value='';entry.elements.return_date.value='';const data=new FormData(entry);if(!data.get('employee_id')||!data.get('starts')||!data.get('ends')||data.get('starts')>data.get('ends')){entryHint.textContent='Select an employee and valid dates.';return}entryHint.textContent='Calculating working days...';try{const result=await api('/api/v1/admin/time-off/preview',{method:'POST',body:new URLSearchParams(data)});if(serial!==previewSerial)return;dayCount=result.days;entry.elements.requested_days.value=result.days;entry.elements.return_date.value=result.suggested_return||'';entryHint.textContent=vacationDays(result)+'. '+holidayDetails(result)+(result.estimate?' Weekday estimate; no fixed weekly schedule.':'')}catch(e){if(serial===previewSerial)entryHint.textContent=e.message}}"
 "entry.elements.starts.addEventListener('change',()=>{previousStart=defaultVacationEnd(entry,previousStart);recalcEntry()});for(const name of ['employee_id','ends'])entry.elements[name].addEventListener('change',recalcEntry);"
 "entry.onsubmit=async e=>{e.preventDefault();try{const data=new FormData(entry);if(dayCount===null||Number(data.get('requested_days'))!==dayCount)throw Error('Check the calculated working days.');entryButton.disabled=true;await api('/api/v1/admin/time-off/record',{method:'POST',body:new URLSearchParams(data)});resetEntry();dialog.close();tc.ok(entryStatus,'Vacation recorded. No email sent.');window.dispatchEvent(new Event('timeclock:issues-changed'));await loadAll()}catch(e){tc.err(entryError,e)}finally{entryButton.disabled=false}};"
 "(async()=>{try{const data=await api('/api/v1/admin/employees');for(const e of [...data.employees].sort((a,b)=>a.name.localeCompare(b.name)||a.id-b.id)){const name=e.name+' (#'+e.display_id+')'+(e.active?'':' - inactive');entry.elements.employee_id.append(new Option(name,e.id));filterEmployee.append(new Option(name,e.id))}openButton.disabled=false}catch(e){tc.err(document.getElementById('status'),e)}})();loadAll();"
 "</script>" UI_FOOT);}
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
 if(!strcmp(method,"POST")&&!strcmp(suffix,"/request")){const char*b=strstr(request,"\r\n\r\n");if(b)timeoff_submit(fd,employee,0,b+4);else send_response(fd,400,"application/json","{\"error\":\"Request incomplete.\"}");return;}
 send_response(fd,404,"text/plain","Not found");
}
