/* Preparation failures are separate from deliverable messages. No placeholder mail
   may reach the SMTP worker. Retain the last failure until queued or dismissed. */
static char email_preparation_storage_error[256],email_preparation_storage_event[160];
static void email_preparation_failure(const char*event,int employee,const char*context,const char*reason){
 fprintf(stderr,"Email not queued (%s): %s\n",event,reason);
 if(db_run(db,"INSERT INTO email_preparation_failures(event_id,employee_id,context,last_error,updated_at) VALUES(?1,?2,?3,?4,CAST(strftime('%s','now') AS INTEGER)) ON CONFLICT(event_id) DO UPDATE SET employee_id=excluded.employee_id,context=excluded.context,last_error=excluded.last_error,updated_at=excluded.updated_at,resolved_at=0,resolved_by=0 WHERE email_preparation_failures.resolved_at<>0 OR email_preparation_failures.employee_id<>excluded.employee_id OR email_preparation_failures.context<>excluded.context OR email_preparation_failures.last_error<>excluded.last_error","titt",event,employee,context,reason)<0){
  snprintf(email_preparation_storage_event,sizeof email_preparation_storage_event,"%s",event);
  snprintf(email_preparation_storage_error,sizeof email_preparation_storage_error,"Could not save email preparation failures. Check storage and database access.");
 }else if(!strcmp(event,email_preparation_storage_event))email_preparation_storage_error[0]=0;
}
static int email_preparation_clear(const char*event){
 int result=db_run(db,"UPDATE email_preparation_failures SET resolved_at=CAST(strftime('%s','now') AS INTEGER),resolved_by=0 WHERE event_id=?1 AND resolved_at=0","t",event)>=0;
 if(result&&!strcmp(event,email_preparation_storage_event))email_preparation_storage_error[0]=0;
 return result;
}
static void email_preparation_json(sqlite3_str*out){
 sqlite3_str_appendall(out,",\"preparation_error\":");json_text(out,email_preparation_storage_error);
 sqlite3_str_appendall(out,",\"preparation_failures\":[");
 sqlite3_stmt*q=db_prepare(db,"SELECT f.event_id,coalesce(e.name,'All employees'),f.context,f.last_error,datetime(f.updated_at,'unixepoch')||'Z' FROM email_preparation_failures f LEFT JOIN employees e ON e.id=f.employee_id WHERE f.resolved_at=0 ORDER BY f.updated_at DESC,f.event_id LIMIT 200","");
 int n=0;if(q)while(sqlite3_step(q)==SQLITE_ROW){if(n++)sqlite3_str_appendchar(out,1,',');sqlite3_str_appendchar(out,1,'{');const char*keys[]={"event_id","employee","context","last_error","updated_at"};for(int i=0;i<5;i++){if(i)sqlite3_str_appendchar(out,1,',');json_text(out,keys[i]);sqlite3_str_appendchar(out,1,':');json_text(out,col_text(q,i));}sqlite3_str_appendchar(out,1,'}');}sqlite3_finalize(q);sqlite3_str_appendchar(out,1,']');
}
static void email_preparation_dismiss(int fd,int who,const char*body){
 char event[160]="";if(!form_value(body,"event_id",event,sizeof event)||!event[0]){reply_incomplete(fd);return;}
 int changed=db_run(db,"UPDATE email_preparation_failures SET resolved_at=CAST(strftime('%s','now') AS INTEGER),resolved_by=?2 WHERE event_id=?1 AND resolved_at=0","ti",event,who);
 if(changed<0){reply_failed(fd);return;}send_response(fd,200,"application/json","{\"status\":\"dismissed\"}");
}
