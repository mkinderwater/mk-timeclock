/* Current-baseline manager accounts and passwordless, one-period employee reviews. */
static int ar_employee(const char *s){char *e=NULL;long v=strtol(s,&e,10);return *s&&!*e&&v>0&&v<=2147483647?(int)v:0;}
static int ar_manager(int employee){
 sqlite3_stmt*q=NULL;int ok=0;
 if(sqlite3_prepare_v2(db,"SELECT 1 FROM employees e JOIN employee_roles r ON r.employee_id=e.id AND r.role='manager' WHERE e.id=?1 AND e.active=1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,employee);ok=sqlite3_step(q)==SQLITE_ROW;}sqlite3_finalize(q);return ok;
}
static int ar_hash_password(const char *pw,char hash[256]){
 if(strlen(pw)<12||strlen(pw)>160)return 0;
 char rnd[25],salt[64];if(!random_hex(rnd,12))return 0;
 snprintf(salt,sizeof salt,"$6$rounds=100000$%s$",rnd);
 const char *v=crypt(pw,salt);if(!v||strlen(v)>=256||strncmp(v,"$6$",3))return 0;
 snprintf(hash,256,"%s",v);return 1;
}
static void accounts_list(int fd){
 sqlite3_stmt*q=NULL;sqlite3_str*out=sqlite3_str_new(db);sqlite3_str_appendall(out,"{\"accounts\":[");int n=0;
 if(sqlite3_prepare_v2(db,"SELECT e.id,e.name,coalesce(e.email,''),coalesce(u.disabled,1),u.password_hash IS NOT NULL FROM employees e JOIN employee_roles r ON r.employee_id=e.id AND r.role='manager' LEFT JOIN user_accounts u ON u.employee_id=e.id WHERE e.active=1 ORDER BY e.name",-1,&q,NULL)==SQLITE_OK){while(sqlite3_step(q)==SQLITE_ROW){if(n++)sqlite3_str_appendchar(out,1,',');sqlite3_str_appendf(out,"{\"id\":%d,\"name\":",sqlite3_column_int(q,0));json_text(out,col_text(q,1));sqlite3_str_appendall(out,",\"email\":");json_text(out,col_text(q,2));sqlite3_str_appendf(out,",\"locked\":%s,\"has_login\":%s}",sqlite3_column_int(q,3)?"true":"false",sqlite3_column_int(q,4)?"true":"false");}}sqlite3_finalize(q);sqlite3_str_appendall(out,"]}");send_str(fd,"application/json",out);
}
static void accounts_self_password(int fd,int who,const char*body){
 char current[256]="",next[256]="",hash[256]="";
 if(!form_value(body,"current",current,sizeof current)||!form_value(body,"next",next,sizeof next)||!ar_hash_password(next,hash)){send_response(fd,400,"application/json","{\"error\":\"Enter your current password and a new password of at least 12 characters.\"}");return;}
 sqlite3_stmt*q=NULL;char stored[256]="";
 if(sqlite3_prepare_v2(db,"SELECT password_hash FROM user_accounts WHERE employee_id=?1 AND disabled=0",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,who);if(sqlite3_step(q)==SQLITE_ROW)snprintf(stored,sizeof stored,"%s",col_text(q,0));}sqlite3_finalize(q);
 const char*v=*stored?crypt(current,stored):NULL;if(!v||strcmp(v,stored)){send_response(fd,403,"application/json","{\"error\":\"Current password is incorrect.\"}");return;}
 int ok=sql("BEGIN IMMEDIATE");if(ok)ok=sqlite3_prepare_v2(db,"UPDATE user_accounts SET password_hash=?1 WHERE employee_id=?2 AND disabled=0",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,hash,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,2,who);ok=sqlite3_step(q)==SQLITE_DONE&&sqlite3_changes(db)==1;}sqlite3_finalize(q);q=NULL;
 if(ok)ok=sqlite3_prepare_v2(db,"DELETE FROM login_sessions WHERE employee_id=?1",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,who);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 if(ok)ok=sqlite3_prepare_v2(db,"INSERT INTO account_change_audit(manager_id,target_id,action) VALUES(?1,?1,'self password change')",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,who);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");send_response(fd,ok?200:500,"application/json",ok?"{\"status\":\"changed; sign in again\"}":"{\"error\":\"Password not changed. Try again.\"}");
}
static void accounts_arm(int fd,int who,const char*body){
 char employee[24]="",action[20]="",pw[256]="",hash[256]="";
 if(!form_value(body,"employee",employee,sizeof employee)||!form_value(body,"action",action,sizeof action)){send_response(fd,400,"application/json","{\"error\":\"Fill in all required fields.\"}");return;}
 int id=ar_employee(employee);
 if(!id||!ar_manager(id)|| (strcmp(action,"set")&&strcmp(action,"lock")&&strcmp(action,"unlock"))){send_response(fd,422,"application/json","{\"error\":\"Select an active manager account\"}");return;}
 if(!strcmp(action,"set")&&(!form_value(body,"password",pw,sizeof pw)||!ar_hash_password(pw,hash))){send_response(fd,422,"application/json","{\"error\":\"Passwords need at least 12 characters.\"}");return;}
 if(id==who&&!strcmp(action,"lock")){send_response(fd,409,"application/json","{\"error\":\"Cannot lock your own account\"}");return;}
 if(!approval_begin(fd))return;
 sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(db,"INSERT INTO account_authorization(id,requested_by,employee_id,action,proposed_hash,armed_at) VALUES(1,?1,?2,?3,?4,CAST(strftime('%s','now') AS INTEGER)) ON CONFLICT(id) DO UPDATE SET requested_by=excluded.requested_by,employee_id=excluded.employee_id,action=excluded.action,proposed_hash=excluded.proposed_hash,armed_at=excluded.armed_at",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,who);sqlite3_bind_int(q,2,id);sqlite3_bind_text(q,3,action,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,4,hash,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 approval_finish(fd,ok);
}
static void accounts_status(int fd,int who){
 sqlite3_stmt*q=NULL;int owner=0,target=0;sqlite3_int64 armed=0,scanned=0;char action[24]="",hash[256]="",uid[48]="";
 if(sqlite3_prepare_v2(db,"SELECT a.requested_by,a.employee_id,a.action,coalesce(a.proposed_hash,''),a.armed_at,s.uid,s.scanned_at FROM account_authorization a JOIN manager_scan s ON s.id=1 WHERE a.id=1",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW){owner=sqlite3_column_int(q,0);target=sqlite3_column_int(q,1);snprintf(action,sizeof action,"%s",col_text(q,2));snprintf(hash,sizeof hash,"%s",col_text(q,3));armed=sqlite3_column_int64(q,4);snprintf(uid,sizeof uid,"%s",col_text(q,5));scanned=sqlite3_column_int64(q,6);}sqlite3_finalize(q);
 if(owner!=who||armed==0){send_response(fd,409,"application/json","{\"error\":\"This approval is no longer valid. Start again.\"}");return;}
 if(time(NULL)-armed>30){sql("DELETE FROM account_authorization WHERE id=1");sql("UPDATE manager_scan SET armed_at=0,uid='',scanned_at=0,request_token='' WHERE id=1");send_response(fd,200,"application/json","{\"status\":\"expired\"}");return;}
 if(!*uid||scanned<armed){send_response(fd,200,"application/json","{\"status\":\"waiting\"}");return;}
 int authorized=0;
 if(sqlite3_prepare_v2(db,"SELECT 1 FROM manager_cards c JOIN employees e ON e.id=c.manager_id JOIN employee_roles r ON r.employee_id=e.id AND r.role='manager' JOIN user_accounts u ON u.employee_id=e.id WHERE c.uid=?1 COLLATE NOCASE AND c.active=1 AND e.active=1 AND u.disabled=0",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,uid,-1,SQLITE_TRANSIENT);authorized=sqlite3_step(q)==SQLITE_ROW;}sqlite3_finalize(q);
 if(!authorized){sql("UPDATE manager_scan SET uid='',scanned_at=0 WHERE id=1");send_response(fd,200,"application/json","{\"status\":\"waiting\"}");return;}
 if(!begin_change(fd))return;
 int ok=ar_manager(target);
 if(ok&&!strcmp(action,"lock")){
  int count=0;
  if(sqlite3_prepare_v2(db,"SELECT count(*) FROM employees e JOIN employee_roles r ON r.employee_id=e.id AND r.role='manager' JOIN user_accounts u ON u.employee_id=e.id WHERE e.active=1 AND u.disabled=0 AND u.password_hash IS NOT NULL",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW)count=sqlite3_column_int(q,0);
  sqlite3_finalize(q);q=NULL;
  if(count<=1)ok=0;
 }
 if(ok){const char*stmt=!strcmp(action,"set")?"INSERT INTO user_accounts(employee_id,password_hash,disabled) VALUES(?1,?2,0) ON CONFLICT(employee_id) DO UPDATE SET password_hash=excluded.password_hash,disabled=0":!strcmp(action,"lock")?"UPDATE user_accounts SET disabled=1 WHERE employee_id=?1":"UPDATE user_accounts SET disabled=0 WHERE employee_id=?1 AND password_hash IS NOT NULL";
  ok=sqlite3_prepare_v2(db,stmt,-1,&q,NULL)==SQLITE_OK;if(ok){sqlite3_bind_int(q,1,target);if(!strcmp(action,"set"))sqlite3_bind_text(q,2,hash,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE&&sqlite3_changes(db)==1;}sqlite3_finalize(q);q=NULL;
 }
 if(ok)ok=sqlite3_prepare_v2(db,"DELETE FROM login_sessions WHERE employee_id=?1",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,target);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 if(ok)ok=sqlite3_prepare_v2(db,"INSERT INTO account_change_audit(manager_id,target_id,action) VALUES(?1,?2,?3)",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,who);sqlite3_bind_int(q,2,target);sqlite3_bind_text(q,3,action,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);q=NULL;
 if(ok)ok=sql("DELETE FROM account_authorization WHERE id=1")&&sql("UPDATE manager_scan SET armed_at=0,uid='',scanned_at=0,request_token='' WHERE id=1");
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");send_response(fd,ok?200:409,"application/json",ok?"{\"status\":\"saved\"}":"{\"error\":\"Not saved. Try again.\"}");
}
static void accounts_page(int fd){send_response(fd,200,"text/html; charset=utf-8",UI_HEAD("Manager accounts","accounts")
 "<h1>Manager accounts</h1><p>Sign-in accounts for employees marked Manager. Changes require a manager card.</p><section><h2>Manager sign-in</h2><label>Manager <select id='who'></select></label><label>Action <select id='action'><option value='set'>Create or reset password</option><option value='lock'>Lock account</option><option value='unlock'>Unlock account</option></select></label><label id='pwbox'>New password (12+ characters)<input id='password' type='password' minlength='12' autocomplete='new-password'></label><div class='actions'><button id='apply'>Apply (manager card required)</button></div><p id='status' role='status'></p></section>"
 "<section><h2>Change your password</h2><form id='own'><label>Current password<input name='current' type='password' required></label><label>New password (12+ characters)<input name='next' type='password' minlength='12' required></label><div class='actions'><button>Change password</button></div></form><p id='ownstate' role='status'></p></section>"
 "<script>const own=document.getElementById('own'),ownstate=document.getElementById('ownstate'),pwbox=document.getElementById('pwbox');const w=document.getElementById('who'),a=document.getElementById('action'),pw=document.getElementById('password'),status=document.getElementById('status');const api=tc.api;async function load(){let x=await api('/api/v1/admin/accounts');w.replaceChildren();for(let e of x.accounts){let o=document.createElement('option');o.value=e.id;o.textContent=e.name+' ('+(e.locked?'locked':e.has_login?'can sign in':'no sign-in')+')';w.append(o)}}a.onchange=()=>pwbox.hidden=a.value!=='set';document.getElementById('apply').onclick=async()=>{try{let p=new URLSearchParams({employee:w.value,action:a.value,password:pw.value});let j=await api('/api/v1/admin/accounts/arm',{method:'POST',body:p});status.textContent=tc.CARD;await tc.card('/api/v1/admin/accounts/status',j.token);tc.ok(status,{set:'Password set.',lock:'Account locked.',unlock:'Account unlocked.'}[a.value]||'Saved.');pw.value='';await load()}catch(e){tc.err(status,e)}};own.onsubmit=async e=>{e.preventDefault();try{await api('/api/v1/admin/accounts/change-password',{method:'POST',body:new URLSearchParams(new FormData(own))});own.reset();tc.ok(ownstate,'Password changed. Sign in again.');setTimeout(()=>location.assign('/login'),1500)}catch(e){tc.err(ownstate,e)}};load().catch(e=>tc.err(status,e))</script>" UI_FOOT);}

/* Hashes validate employee access; reusable tokens and queued URLs are stored locally. */
static void review_choices(int fd){
 sqlite3_stmt*q=NULL;sqlite3_str*out=sqlite3_str_new(db);sqlite3_str_appendall(out,"{\"employees\":[");int n=0;
 if(sqlite3_prepare_v2(db,"SELECT id,name,coalesce(email,'') FROM employees WHERE active=1 ORDER BY name",-1,&q,NULL)==SQLITE_OK){while(sqlite3_step(q)==SQLITE_ROW){if(n++)sqlite3_str_appendchar(out,1,',');sqlite3_str_appendf(out,"{\"id\":%d,\"name\":",sqlite3_column_int(q,0));json_text(out,col_text(q,1));sqlite3_str_appendall(out,",\"email\":");json_text(out,col_text(q,2));sqlite3_str_appendchar(out,1,'}');}}sqlite3_finalize(q);
 sqlite3_str_appendall(out,"],\"periods\":[");n=0;
 if(sqlite3_prepare_v2(db,"SELECT id,starts,ends,status FROM pay_periods ORDER BY starts DESC LIMIT 60",-1,&q,NULL)==SQLITE_OK){while(sqlite3_step(q)==SQLITE_ROW){if(n++)sqlite3_str_appendchar(out,1,',');sqlite3_str_appendf(out,"{\"id\":%d,\"label\":",sqlite3_column_int(q,0));char label[90];snprintf(label,sizeof label,"%s to %s%s",col_text(q,1),col_text(q,2),strcmp(col_text(q,3),"closed")?"":" (closed)");json_text(out,label);sqlite3_str_appendchar(out,1,'}');}}sqlite3_finalize(q);sqlite3_str_appendall(out,"]}");send_str(fd,"application/json",out);
}
static int review_ids(const char*body,int*eid,int*pid){char e[24]="",p[24]="";if(!form_value(body,"employee",e,sizeof e)||!form_value(body,"period",p,sizeof p))return 0;*eid=ar_employee(e);*pid=ar_employee(p);return *eid&&*pid;}
/* Shared link creation: callers own the transaction; never call for a valid issued link. */
static int review_link_store(int employee,int period,int manager,char token[65]){
 char hash[256];if(!random_hex(token,32)||!token_hash(token,REVIEW_LINK_SALT,hash,sizeof hash))return 0;
 sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(db,"INSERT INTO review_links(employee_id,period_id,token_hash,token,expires_at,created_by,created_at,revoked) VALUES(?1,?2,?3,?5,CAST(strftime('%s','now') AS INTEGER)+1209600,?4,CAST(strftime('%s','now') AS INTEGER),0) ON CONFLICT(employee_id,period_id) DO UPDATE SET token_hash=excluded.token_hash,token=excluded.token,expires_at=excluded.expires_at,created_by=excluded.created_by,created_at=excluded.created_at,revoked=0",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,employee);sqlite3_bind_int(q,2,period);sqlite3_bind_text(q,3,hash,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,4,manager);sqlite3_bind_text(q,5,token,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);return ok;
}
/* 1 valid row (possibly hash-only), 0 absent/expired/revoked, -1 database error. */
static int review_link_read(int employee,int period,char token[65],long long*expires){
 sqlite3_stmt*q=NULL;token[0]=0;
 if(sqlite3_prepare_v2(db,"SELECT token,expires_at FROM review_links WHERE employee_id=?1 AND period_id=?2 AND revoked=0 AND expires_at>CAST(strftime('%s','now') AS INTEGER)",-1,&q,NULL)!=SQLITE_OK)return -1;
 sqlite3_bind_int(q,1,employee);sqlite3_bind_int(q,2,period);int rc=sqlite3_step(q),result=rc==SQLITE_ROW?1:rc==SQLITE_DONE?0:-1;
 if(result==1){snprintf(token,65,"%s",col_text(q,0));if(expires)*expires=sqlite3_column_int64(q,1);}sqlite3_finalize(q);return result;
}
/* Extend only for a newly queued period email, within its transaction. Daily
   snapshots and duplicate/copy/view requests never prolong a bearer link. */
static int review_link_extend(int employee,int period,const char*token){
 return db_run(db,"UPDATE review_links SET expires_at=max(expires_at,CAST(strftime('%s','now') AS INTEGER)+1209600) WHERE employee_id=?1 AND period_id=?2 AND token=?3 AND token<>'' AND revoked=0 AND expires_at>CAST(strftime('%s','now') AS INTEGER)","iit",employee,period,token)==1;
}
/* One email body for automatic delivery and manager sends. */
static int timesheet_email(int employee,const char*name,const char*starts,const char*ends,const char*link,char*subject,size_t subject_cap,char*message,size_t message_cap){
 char missing_note[150]="";int problems=0;sqlite3_stmt*q=NULL;
 if(sqlite3_prepare_v2(db,"SELECT count(*) FROM effective_shift_pairs x WHERE employee_id=?1 AND date(in_at,'localtime') BETWEEN ?2 AND ?3 AND state<>'complete' AND NOT " REVIEW_ACTIVE_SQL("x","(SELECT status FROM pay_periods WHERE starts=?2 AND ends=?3)") "",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,employee);sqlite3_bind_text(q,2,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,ends,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW)problems=sqlite3_column_int(q,0);}sqlite3_finalize(q);
 if(problems)snprintf(missing_note,sizeof missing_note,"%d punch %s manager correction.\n",problems,problems==1?"problem needs":"problems need");
 snprintf(subject,subject_cap,"Your timesheet for %s to %s",starts,ends);
 snprintf(message,message_cap,"%s\n\nHello %s,\n\nYour timesheet for %s to %s is available at the link above.\n%s\nContact your manager if there is an issue.",link,name,starts,ends,missing_note);
 const struct email_field fields[]={{"{{employee_name}}",name},{"{{period_start}}",starts},{"{{period_end}}",ends},{"{{review_link}}",link},{"{{missing_punch_note}}",missing_note}};
 return template_render(db,"auto_review_employee",fields,5,subject,subject_cap,message,message_cap)==1;
}
static void review_generate(int fd,int who,const char*body,int email_now){
 int employee=0,period=0;if(!review_ids(body,&employee,&period)){send_response(fd,400,"application/json","{\"error\":\"Select an employee and a pay period.\"}");return;}
 sqlite3_stmt*q=NULL;int valid=0;
 if(sqlite3_prepare_v2(db,"SELECT 1 FROM employees e CROSS JOIN pay_periods p WHERE e.id=?1 AND e.active=1 AND p.id=?2",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,employee);sqlite3_bind_int(q,2,period);valid=sqlite3_step(q)==SQLITE_ROW;}sqlite3_finalize(q);
 if(!valid){send_response(fd,404,"application/json","{\"error\":\"Employee or pay period not found. Reload the page.\"}");return;}
 char token[65]="";
 char recipient[256]="",employee_name[192]="",period_start[16]="",period_end[16]="",base[270]="";
 if(email_now){
  if(sqlite3_prepare_v2(db,"SELECT coalesce(email,''),name FROM employees WHERE id=?1 AND active=1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,employee);if(sqlite3_step(q)==SQLITE_ROW)snprintf(recipient,sizeof recipient,"%s",col_text(q,0));snprintf(employee_name,sizeof employee_name,"%s",col_text(q,1));}sqlite3_finalize(q);q=NULL;
  if(sqlite3_prepare_v2(db,"SELECT starts,ends FROM pay_periods WHERE id=?1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,period);if(sqlite3_step(q)==SQLITE_ROW){snprintf(period_start,sizeof period_start,"%s",col_text(q,0));snprintf(period_end,sizeof period_end,"%s",col_text(q,1));}}sqlite3_finalize(q);q=NULL;
  note_link_base(base,sizeof base);
  if(!email_address_ok(recipient)){send_response(fd,422,"application/json","{\"error\":\"This employee has no email address. Add one under Employees.\"}");return;}
  if(!base[0]){send_response(fd,422,"application/json","{\"error\":\"No hostname is set for links. Install a certificate under HTTPS certificate.\"}");return;}
  int reminder_template=template_state(db,"auto_review_employee");
  if(reminder_template==-2){reply_failed(fd);return;}
  if(reminder_template!=1){send_response(fd,422,"application/json","{\"error\":\"Timesheet link email is turned off in Email templates.\"}");return;}
 }
 long long expires=0;int reused=review_link_read(employee,period,token,&expires);
 if(reused<0){reply_failed(fd);return;}
 if(reused&&!token[0]){send_response(fd,409,"application/json","{\"error\":\"This existing link cannot be copied. It remains valid. Revoke it first if you need a replacement.\"}");return;}
 int queued=-1;char delivery[32]="";
 int ok=sql("BEGIN IMMEDIATE");if(ok&&!reused)ok=review_link_store(employee,period,who,token);
 if(ok)ok=sqlite3_prepare_v2(db,"INSERT INTO review_link_audit(manager_id,employee_id,period_id,action) VALUES(?1,?2,?3,'link accessed')",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,who);sqlite3_bind_int(q,2,employee);sqlite3_bind_int(q,3,period);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 if(ok&&email_now){
  char link[390],message[32768],subject[32768];
  snprintf(link,sizeof link,"%s/review/%s",base,token);
  if(!timesheet_email(employee,employee_name,period_start,period_end,link,subject,sizeof subject,message,sizeof message)){sql("ROLLBACK");send_response(fd,422,"application/json","{\"error\":\"Timesheet email could not be rendered. Check the template length and fields.\"}");return;}
  char key[400];snprintf(key,sizeof key,"pay_review:%d:%d:%.64s:%s",employee,period,token,recipient);
  /* A terminal failure may be retried explicitly. Pending/sent copies remain deduplicated. */
  ok=sqlite3_prepare_v2(db,"UPDATE email_queue SET status='pending',attempts=0,last_error='',subject=?2,body=?3,next_attempt=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE event_id=?1 AND status='failed'",-1,&q,NULL)==SQLITE_OK;
  if(ok){sqlite3_bind_text(q,1,key,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,subject,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,message,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;if(ok)queued=sqlite3_changes(db)>0;}sqlite3_finalize(q);q=NULL;
  if(ok&&!queued){queued=email_enqueue(db,recipient,subject,message,key,NULL);ok=queued>=0;}
  if(ok&&queued)ok=review_link_extend(employee,period,token);
  if(ok){ok=sqlite3_prepare_v2(db,"SELECT status FROM email_queue WHERE event_id=?1",-1,&q,NULL)==SQLITE_OK;if(ok){sqlite3_bind_text(q,1,key,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_ROW;if(ok){snprintf(delivery,sizeof delivery,"%s",col_text(q,0));}}sqlite3_finalize(q);q=NULL;}

 }
 if(ok&&email_now&&queued)ok=review_link_read(employee,period,token,&expires)==1;
 if(ok){ok=sql("COMMIT");}
 if(!ok){sql("ROLLBACK");}
 if(!ok){send_response(fd,500,"application/json","{\"error\":\"The link was not created. Try again.\"}");return;}
 sqlite3_str*out=sqlite3_str_new(db);sqlite3_str_appendf(out,"{\"path\":\"/review/%s\",\"expires_in_days\":%lld",token,(reused||(email_now&&queued))?(expires-(long long)time(NULL)+86399)/86400:14LL);
 if(email_now){sqlite3_str_appendf(out,",\"queued\":%s,\"email_status\":",queued?"true":"false");json_text(out,queued?"queued":!strcmp(delivery,"sent")?"already_sent":"already_queued");sqlite3_str_appendall(out,",\"recipient\":");json_text(out,recipient);}
 sqlite3_str_appendchar(out,1,'}');send_str(fd,"application/json",out);
}
static void review_revoke(int fd,int who,const char*body){
 int eid=0,pid=0;if(!review_ids(body,&eid,&pid)){reply_incomplete(fd);return;}
 if(!sql("BEGIN IMMEDIATE")){reply_failed(fd);return;}
 sqlite3_stmt*q=NULL;int changed=0,ok=sqlite3_prepare_v2(db,"UPDATE review_links SET revoked=1 WHERE employee_id=?1 AND period_id=?2 AND revoked=0 AND expires_at>CAST(strftime('%s','now') AS INTEGER)",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,eid);sqlite3_bind_int(q,2,pid);ok=sqlite3_step(q)==SQLITE_DONE;if(ok)changed=sqlite3_changes(db);}sqlite3_finalize(q);q=NULL;
 if(ok&&!changed){sql("ROLLBACK");send_response(fd,404,"application/json","{\"error\":\"There is no active link for this employee and period.\"}");return;}
 if(ok)ok=sqlite3_prepare_v2(db,"INSERT INTO review_link_audit(manager_id,employee_id,period_id,action) VALUES(?1,?2,?3,'revoked')",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,who);sqlite3_bind_int(q,2,eid);sqlite3_bind_int(q,3,pid);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");
 if(ok)send_response(fd,200,"application/json","{\"status\":\"revoked\"}");else reply_failed(fd);
}
static int review_validate(const char*token,int*employee,int*period){
 char hash[256];if(!token_hash(token,REVIEW_LINK_SALT,hash,sizeof hash))return 0;sqlite3_stmt*q=NULL;int ok=0;
 if(sqlite3_prepare_v2(db,"SELECT l.employee_id,l.period_id FROM review_links l JOIN employees e ON e.id=l.employee_id WHERE l.token_hash=?1 AND l.revoked=0 AND l.expires_at>CAST(strftime('%s','now') AS INTEGER) AND e.active=1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,hash,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW){*employee=sqlite3_column_int(q,0);*period=sqlite3_column_int(q,1);ok=1;}}sqlite3_finalize(q);return ok;
}
/* Employee-facing page for an unusable review link. */
static void review_invalid(int fd){
 send_response(fd,404,"text/html; charset=utf-8",UI_HEAD_LOGIN("Link not valid")"<h1>Link not valid</h1><p class='banner banner-problem' role='alert'><span class='flag flag-problem'>Problem</span><span>This link has expired or been replaced.</span></p><p>Ask your manager for a new timesheet link.</p>" UI_FOOT);
}
/* Shared context rules for employee display and automatic mail eligibility.
   Only approved leave, recorded absences and configured holidays are included. */
static const char *lite_review_context_sql=
 "SELECT day,reason,'absence' FROM attendance_absences WHERE employee_id=?1 AND cancelled_at IS NULL AND day BETWEEN ?2 AND ?3 "
 "UNION ALL SELECT h.day,'Stat holiday: '||h.name,'holiday' FROM statutory_holidays h WHERE h.day BETWEEN ?2 AND ?3 "
 "UNION ALL SELECT d.day,'Approved time off: '||t.kind,'timeoff:'||t.kind "
 "FROM (WITH RECURSIVE dates(day) AS (SELECT ?2 UNION ALL SELECT date(day,'+1 day') FROM dates WHERE day<?3) SELECT day FROM dates) d "
 "JOIN timeoff_requests t ON t.employee_id=?1 AND t.status='approved' AND t.cancelled_at IS NULL AND d.day BETWEEN t.starts AND t.ends "
 "WHERE (t.kind<>'vacation' OR NOT EXISTS(SELECT 1 FROM statutory_holidays h WHERE h.day=d.day)) AND "
 "CASE WHEN EXISTS(SELECT 1 FROM schedule_profiles sp WHERE sp.employee_id=?1 AND sp.schedule_mode='recurring_weekly') THEN "
 "((SELECT weekday_mask FROM schedule_profiles WHERE employee_id=?1) & (1 << ((CAST(strftime('%w',d.day) AS INTEGER)+6)%7)))<>0 "
 "WHEN EXISTS(SELECT 1 FROM schedule_shifts ss JOIN schedule_profiles sp ON sp.employee_id=ss.employee_id AND sp.schedule_mode='dated_shifts' WHERE ss.employee_id=?1 AND ss.work_date=d.day) THEN 1 "
 "ELSE CAST(strftime('%w',d.day) AS INTEGER) BETWEEN 1 AND 5 END ORDER BY 1,2";
static int lite_review_has_context(int employee,const char *starts,const char *ends){
 sqlite3_stmt*q=NULL;int found=0;
 if(sqlite3_prepare_v2(db,lite_review_context_sql,-1,&q,NULL)==SQLITE_OK){
  sqlite3_bind_int(q,1,employee);sqlite3_bind_text(q,2,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,ends,-1,SQLITE_TRANSIENT);
  while(sqlite3_step(q)==SQLITE_ROW){if(strcmp(col_text(q,2),"holiday")){found=1;break;}}
 }
 sqlite3_finalize(q);return found;
}
/* Automatic link delivery timing. Employee links themselves are always read-only.
   after_period: the link goes out N days after the period ends at the set time.
   last_scheduled: the link goes out on the employee's last scheduled workday in the period (stat holidays and
   approved time off skipped), X company-configured hours before the scheduled finish;
   Every employee uses the company timing rule, with the schedule/leave fallback.
   Employees with no scheduled workday in the period, and vacation-only employees, use after_period. */
struct review_timing{int last_scheduled;char day[11];int finish;int hours_before_finish;time_t send_at;};
static int hhmm_minutes(const char*t){int h=0,m=0;return t&&sscanf(t,"%d:%d",&h,&m)==2&&h>=0&&h<24&&m>=0&&m<60?h*60+m:-1;}
/* Resolve the final scheduled finish in the clock's zone before subtracting elapsed hours. */
static time_t review_local_at(const char*day,int minute){
 struct tm value={0};int year=0,month=0,date=0;
 if(sscanf(day,"%d-%d-%d",&year,&month,&date)!=3)return (time_t)-1;
 value.tm_year=year-1900;value.tm_mon=month-1;value.tm_mday=date;
 value.tm_hour=minute/60;value.tm_min=minute%60;value.tm_isdst=-1;
 return mktime(&value);
}
static int review_timing_for(int employee,const char*starts,const char*ends,struct review_timing*t){
 memset(t,0,sizeof *t);snprintf(t->day,sizeof t->day,"%s",ends);t->finish=-1;t->hours_before_finish=1;t->send_at=(time_t)-1;
 sqlite3_stmt*q=NULL;char mode[24]="after_period";int vacation_only=0;
 if(sqlite3_prepare_v2(db,"SELECT coalesce((SELECT mode FROM review_send_settings WHERE id=1),'after_period'),e.attendance_mode='vacation_only',coalesce((SELECT hours_before_finish FROM review_send_settings WHERE id=1),1) FROM employees e WHERE e.id=?1",-1,&q,NULL)!=SQLITE_OK){sqlite3_finalize(q);return 0;}
 sqlite3_bind_int(q,1,employee);if(sqlite3_step(q)==SQLITE_ROW){snprintf(mode,sizeof mode,"%s",col_text(q,0));vacation_only=sqlite3_column_int(q,1);t->hours_before_finish=sqlite3_column_int(q,2);}sqlite3_finalize(q);q=NULL;
 if(strcmp(mode,"last_scheduled")||vacation_only)return 1;
 /* Latest workday first. A dated shift wins over the weekly pattern only for dated-shift employees. */
 static const char*last_day_sql=
  "WITH RECURSIVE d(day) AS (SELECT date(?3) UNION ALL SELECT date(day,'-1 day') FROM d WHERE day>?2) "
  "SELECT d.day,CASE sp.schedule_mode WHEN 'recurring_weekly' THEN sp.start_time ELSE (SELECT ss.start_time FROM schedule_shifts ss WHERE ss.employee_id=?1 AND ss.work_date=d.day ORDER BY ss.start_time DESC LIMIT 1) END,"
  "CASE sp.schedule_mode WHEN 'recurring_weekly' THEN sp.end_time ELSE (SELECT ss.end_time FROM schedule_shifts ss WHERE ss.employee_id=?1 AND ss.work_date=d.day ORDER BY ss.start_time DESC LIMIT 1) END "
  "FROM d JOIN schedule_profiles sp ON sp.employee_id=?1 "
  "WHERE NOT EXISTS(SELECT 1 FROM statutory_holidays h WHERE h.day=d.day) "
  "AND NOT EXISTS(SELECT 1 FROM timeoff_requests t WHERE t.employee_id=?1 AND t.status='approved' AND t.cancelled_at IS NULL AND d.day BETWEEN t.starts AND t.ends) "
  "AND ((sp.schedule_mode='recurring_weekly' AND (sp.weekday_mask & (1 << ((CAST(strftime('%w',d.day) AS INTEGER)+6)%7)))<>0) "
  "OR (sp.schedule_mode='dated_shifts' AND EXISTS(SELECT 1 FROM schedule_shifts ss WHERE ss.employee_id=?1 AND ss.work_date=d.day))) "
  "ORDER BY d.day DESC LIMIT 1";
 if(sqlite3_prepare_v2(db,last_day_sql,-1,&q,NULL)!=SQLITE_OK){sqlite3_finalize(q);return 0;}
 sqlite3_bind_int(q,1,employee);sqlite3_bind_text(q,2,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,ends,-1,SQLITE_TRANSIENT);
 int rc=sqlite3_step(q);
 if(rc==SQLITE_ROW){int st=hhmm_minutes(col_text(q,1)),en=hhmm_minutes(col_text(q,2));
  t->last_scheduled=1;snprintf(t->day,sizeof t->day,"%s",col_text(q,0));t->finish=en<0?-1:(st>=0&&en<=st?en+1440:en);
  if(t->finish>=0){time_t finish=review_local_at(t->day,t->finish);if(finish!=(time_t)-1)t->send_at=finish-t->hours_before_finish*3600;}}
 sqlite3_finalize(q);return rc==SQLITE_ROW||rc==SQLITE_DONE;
}
/* Automatic email is due at this fixed time. Valid links are readable independently. */
static int review_timing_open(int employee,const struct review_timing*t,const char*today,int now){
 (void)employee;
 if(!t->last_scheduled)return strcmp(today,t->day)>=0;
 time_t current=review_local_at(today,now);
 return t->send_at!=(time_t)-1&&current!=(time_t)-1&&current>=t->send_at;
}
/* Present the exact clock-local send date and minute, including overnight finishes. */
static int review_send_time(const struct review_timing*t,char day[11],int*minute){
 snprintf(day,11,"%s",t->day);*minute=-1;
 if(!t->last_scheduled||t->send_at==(time_t)-1)return 0;
 struct tm local;if(!localtime_r(&t->send_at,&local))return 0;
 strftime(day,11,"%Y-%m-%d",&local);*minute=local.tm_hour*60+local.tm_min;return 1;
}
static void review_detail(int fd,int employee,int period){
 sqlite3_stmt*q=NULL;sqlite3_str*out=sqlite3_str_new(db);int ok=0;char starts[16]="",ends[16]="",status[24]="";
 if(sqlite3_prepare_v2(db,"SELECT starts,ends,status FROM pay_periods WHERE id=?1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,period);if(sqlite3_step(q)==SQLITE_ROW){snprintf(starts,sizeof starts,"%s",col_text(q,0));snprintf(ends,sizeof ends,"%s",col_text(q,1));snprintf(status,sizeof status,"%s",col_text(q,2));ok=1;}}sqlite3_finalize(q);
 if(!ok){sqlite3_str_finish(out);send_response(fd,404,"application/json","{\"error\":\"Pay period not found. Reload the page.\"}");return;}
 int count=0;long long secs=0;rec_employee_totals(employee,starts,ends,&count,&secs);
 int vacation_only=!rec_review_required(employee,starts,ends);
 sqlite3_str_appendf(out,"{\"id\":%d,\"starts\":",period);json_text(out,starts);sqlite3_str_appendall(out,",\"ends\":");json_text(out,ends);sqlite3_str_appendall(out,",\"status\":");json_text(out,status);
 sqlite3_str_appendf(out,",\"vacation_only\":%s,\"punches\":%d,\"seconds\":%lld,\"entries\":[",vacation_only?"true":"false",count,secs);
 int n=0;
 if(sqlite3_prepare_v2(db,"SELECT x.in_at,x.out_at,x.seconds,x.state,x.manual,"
 " coalesce((SELECT c.reason FROM punch_corrections c WHERE c.event_id=x.in_id OR c.manual_id=x.in_id ORDER BY c.id DESC LIMIT 1),ni.note,''),coalesce((SELECT c.late_minutes FROM punch_corrections c WHERE c.event_id=x.in_id OR c.manual_id=x.in_id),ai.late_minutes,0),"
 " EXISTS(SELECT 1 FROM punch_corrections c WHERE c.event_id=x.in_id AND c.action='change'),"
 " coalesce((SELECT c.reason FROM punch_corrections c WHERE c.event_id=x.out_id OR c.manual_id=x.out_id ORDER BY c.id DESC LIMIT 1),no.note,''),"
 " EXISTS(SELECT 1 FROM punch_corrections c WHERE c.event_id=x.out_id AND c.action='change'),"
 " coalesce((SELECT p.manual FROM effective_punches p WHERE p.id=x.out_id),0),coalesce((SELECT c.early_minutes FROM punch_corrections c WHERE c.event_id=x.out_id OR c.manual_id=x.out_id),ao.early_minutes,0)"
 ",date(x.in_at,'localtime'),CAST(strftime('%H',x.in_at,'localtime') AS INTEGER)*60+CAST(strftime('%M',x.in_at,'localtime') AS INTEGER),"
 " CASE WHEN x.out_at IS NULL THEN NULL ELSE CAST(round((julianday(x.out_at,'localtime')-julianday(date(x.in_at,'localtime')))*1440) AS INTEGER) END"
 "," REVIEW_ACTIVE_SQL("x","(SELECT status FROM pay_periods WHERE id=?4)") " FROM effective_shift_pairs x LEFT JOIN swipe_notes ni ON ni.event_id=x.in_id LEFT JOIN attendance_events ai ON ai.event_id=x.in_id LEFT JOIN swipe_notes no ON no.event_id=x.out_id LEFT JOIN attendance_events ao ON ao.event_id=x.out_id WHERE x.employee_id=?1 AND date(in_at,'localtime') BETWEEN ?2 AND ?3 ORDER BY in_at,in_id LIMIT 500",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,employee);sqlite3_bind_text(q,2,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,ends,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,4,period);while(sqlite3_step(q)==SQLITE_ROW){if(n++)sqlite3_str_appendchar(out,1,',');sqlite3_str_appendall(out,"{\"in_at\":");json_text(out,col_text(q,0));sqlite3_str_appendall(out,",\"out_at\":");if(sqlite3_column_type(q,1)==SQLITE_NULL)sqlite3_str_appendall(out,"null");else json_text(out,col_text(q,1));sqlite3_str_appendf(out,",\"seconds\":%lld,\"state\":",sqlite3_column_int64(q,2));json_text(out,col_text(q,3));sqlite3_str_appendf(out,",\"active\":%s",sqlite3_column_int(q,15)?"true":"false");sqlite3_str_appendf(out,",\"manual\":%s,\"out_manual\":%s,\"in_corrected\":%s,\"out_corrected\":%s,\"late_minutes\":%d,\"early_minutes\":%d,\"in_note\":",sqlite3_column_int(q,4)?"true":"false",sqlite3_column_int(q,10)?"true":"false",sqlite3_column_int(q,7)?"true":"false",sqlite3_column_int(q,9)?"true":"false",sqlite3_column_int(q,6),sqlite3_column_int(q,11));json_text(out,col_text(q,5));sqlite3_str_appendall(out,",\"out_note\":");json_text(out,col_text(q,8));sqlite3_str_appendall(out,",\"day\":");json_text(out,col_text(q,12));sqlite3_str_appendf(out,",\"in_min\":%d,\"out_min\":",sqlite3_column_int(q,13));if(sqlite3_column_type(q,14)==SQLITE_NULL)sqlite3_str_appendall(out,"null");else sqlite3_str_appendf(out,"%d",sqlite3_column_int(q,14));sqlite3_str_appendchar(out,1,'}');}}sqlite3_finalize(q);
 sqlite3_str_appendall(out,"],\"context\":[");
 int context_count=0;

 if(sqlite3_prepare_v2(db,lite_review_context_sql,-1,&q,NULL)==SQLITE_OK){
  sqlite3_bind_int(q,1,employee);sqlite3_bind_text(q,2,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,ends,-1,SQLITE_TRANSIENT);
  while(sqlite3_step(q)==SQLITE_ROW){if(context_count++)sqlite3_str_appendchar(out,1,',');sqlite3_str_appendall(out,"{\"day\":");json_text(out,col_text(q,0));sqlite3_str_appendall(out,",\"label\":");json_text(out,col_text(q,1));sqlite3_str_appendall(out,",\"type\":");json_text(out,col_text(q,2));sqlite3_str_appendchar(out,1,'}');}
 }sqlite3_finalize(q);
 sqlite3_str_appendall(out,"]}");send_str(fd,"application/json",out);
}
static void review_route(int fd,const char*method,const char*path){
 const char*t=path+8;if(strlen(t)<64){review_invalid(fd);return;}
 char token[65];memcpy(token,t,64);token[64]=0;
 const char*suffix=t+64;int e=0,p=0;if(!review_validate(token,&e,&p)){review_invalid(fd);return;}
 if(strcmp(method,"GET")){send_response(fd,403,"application/json","{\"error\":\"This timesheet is read-only. Contact your manager if there is an issue.\"}");return;}
 if(!strcmp(method,"GET")&&!*suffix){send_response(fd,200,"text/html; charset=utf-8",UI_HEAD_LOGIN("Your timesheet")
 "<script>document.body.classList.add('review-employee')</script><h1>Your timesheet</h1><section><h2 id=\"summary\">Loading...</h2><p id=\"periodline\" class=\"muted\"></p><div class=\"review-progress\" id=\"periodbar\" hidden><span></span></div><p id=\"reviewstate\">Contact your manager if there is an issue.</p><p id=\"revbanner\" role=\"alert\" hidden></p><div id=\"reviewlegend\"></div><table><thead><tr><th>Day</th><th>IN / OUT</th><th>Hours</th></tr></thead><tbody id=\"entries\"></tbody></table><p id=\"status\" role=\"status\"></p></section>"
 "<script>const base=location.pathname,info=document.getElementById('summary'),msg=document.getElementById('status');const api=tc.api;function shortDate(x){return x}function clock(t){return tc.time(t)}async function load(){let p=await api(base+'/detail'),rows=document.getElementById('entries'),bad=0;let ctx=p.context||[];if(p.vacation_only){let labels={'timeoff:vacation':['vacation day','vacation days'],'timeoff:personal':['personal day','personal days'],'timeoff:unpaid':['unpaid leave day','unpaid leave days'],'timeoff:other':['other time off','other time off'],'absence:Sick':['sick day','sick days'],'absence:Medical appointment':['medical appointment','medical appointments'],'absence:Personal day':['personal day','personal days'],'absence:Family emergency':['family emergency day','family emergency days'],'absence:Bereavement':['bereavement day','bereavement days'],'absence:Unpaid leave':['unpaid leave day','unpaid leave days'],'absence:Unexcused absence':['unexcused absence','unexcused absences'],'absence:Other':['other absence','other absences'],'holiday':['stat holiday','stat holidays']};let tally=new Map();for(let x of ctx){let key=x.type==='absence'?'absence:'+x.label:x.type;if(!labels[key])continue;let name=labels[key][0];if(!tally.has(name))tally.set(name,{days:new Set(),forms:labels[key]});tally.get(name).days.add(x.day)}let parts=[];for(let v of tally.values()){let n=v.days.size;if(n)parts.push(n+' '+v.forms[n===1?0:1])}info.textContent=shortDate(p.starts)+' to '+shortDate(p.ends)+(parts.length?': '+parts.join(', '):': No leave or holidays recorded');}else info.textContent=(p.seconds/3600).toFixed(2)+' hours recorded';let nice=d=>tc.date(d,'short'),dayMs=86400000,span=Math.round((Date.parse(p.ends+'T12:00:00')-Date.parse(p.starts+'T12:00:00'))/dayMs)+1,today=tc.date(new Date()),done=Math.max(0,Math.min(span,Math.round((Date.parse(today+'T12:00:00')-Date.parse(p.starts+'T12:00:00'))/dayMs)+1)),bar=document.getElementById('periodbar');document.getElementById('periodline').textContent=nice(p.starts)+' to '+nice(p.ends)+(done>0&&done<span?', day '+done+' of '+span:done>=span?', period ended':'');bar.hidden=p.vacation_only;bar.firstElementChild.style.width=(done/span*100)+'%';bar.setAttribute('role','img');bar.setAttribute('aria-label','Day '+done+' of '+span);let edges=[];for(let x of p.entries){edges.push(x.in_min);if(x.out_min!==null)edges.push(Math.min(1440,x.out_min))}let [from,to]=tc.range(edges,480,1020);let head=document.querySelector('thead th:nth-child(2)');if(head){let axis=head.querySelector('.strip-axis');if(axis)axis.remove();if(p.entries.length)head.append(tc.axis(from,to))}let lg=document.getElementById('reviewlegend');lg.hidden=!p.entries.length;if(p.entries.length)lg.replaceChildren(tc.legend([['work','Worked'],['open','In now'],['late','Late'],['long','Long shift'],['miss','Missing punch']]));rows.replaceChildren();for(let x of p.entries){let day=tc.date(x.in_at),problem=x.state!=='complete'&&!x.active,tr=document.createElement('tr');tr.dataset.day=day;let interval=x.state==='missing_in'?'(missing IN)':clock(x.in_at)+' to '+(x.out_at?clock(x.out_at)+(tc.date(x.out_at)!==day?' (next day)':''):x.active?'Clocked in, no OUT yet':'(missing OUT)');for(let v of [tc.date(day,'day'),interval,(x.seconds/3600).toFixed(2)+' h']){let td=document.createElement('td');td.textContent=v;tr.append(td)}if(problem){bad++;tr.className='row-problem';tr.children[1].append(' ',tc.flag('Missing punch','problem'))}if(x.state==='complete'&&x.seconds>72000){tr.children[1].append(' ',tc.flag('Long shift','check'))}if(x.manual||x.out_manual)tr.children[1].append(' ',tc.flag('Manager-added punch','check'));if(x.in_corrected||x.out_corrected)tr.children[1].append(' ',tc.flag('Manager correction','check'));if(x.early_minutes>0)tr.children[1].append(' ',tc.flag('Left early: '+x.early_minutes+' min','check'));if(x.late_minutes>0)tr.children[1].append(' ',tc.flag('Late: '+x.late_minutes+' min','check'));for(let note of [x.in_note,x.out_note])if(note)tr.children[1].append(' Reason: '+note);let marks=[],label=interval;if(x.late_minutes>0)marks.push({s:x.in_min-x.late_minutes,e:x.in_min,c:'late',t:'Late '+x.late_minutes+' min'});if(x.state==='complete')marks.push({s:x.in_min,e:x.out_min,c:x.seconds>72000?'long':'work',t:interval});else if(x.active)marks.push({s:x.in_min,e:x.in_min+45,c:'open',t:'Clocked in, no OUT yet'});else if(x.state==='missing_out')marks.push({s:x.in_min,e:x.in_min+45,c:'miss',t:'No OUT recorded'});else marks.push({s:x.in_min-45,e:x.in_min,c:'miss',t:'No IN recorded'});let holder=document.createElement('div');holder.append(tc.strip({from,to,label,marks}));tr.children[1].append(holder);tr.children[1].className='day-strip';rows.append(tr)}for(let x of p.context||[]){let tr=document.createElement('tr');tr.className='row-context';tr.dataset.day=x.day;for(let value of [tc.date(x.day,'day'),x.label,p.vacation_only?'': '0.00 h']){let td=document.createElement('td');td.textContent=value;tr.append(td)}if(x.type==='holiday'){let note=document.createElement('div');note.className='muted';note.textContent='Not counted as vacation';tr.children[1].append(note)}rows.append(tr)}Array.from(rows.children).sort((a,b)=>{return (a.dataset.day||'').localeCompare(b.dataset.day||'')}).forEach(x=>rows.append(x));if(!p.entries.length&&!(p.context||[]).length){let tr=document.createElement('tr'),td=document.createElement('td');td.colSpan=3;td.textContent='No punches recorded.';tr.append(td);rows.append(tr)}tc.banner(document.getElementById('revbanner'),'problem','Problem',bad?bad+(bad===1?' incomplete shift':' incomplete shifts')+'.':'');}load().catch(e=>tc.err(msg,e))</script>" UI_FOOT);return;}
 if(!strcmp(method,"GET")&&!strcmp(suffix,"/detail")){review_detail(fd,e,p);return;}
 send_response(fd,404,"text/plain","Not found");
}
/* Automatic reviews use the existing review/email tables. One event per employee/period; a valid
   manager-created link is never replaced. Each employee is due by review_timing_for(): after the period
   ends, or on their last scheduled workday in the period (so current periods are checked too). */
static void review_auto_due(void){
 static time_t last_checked=0;
 time_t now=time(NULL);if(last_checked&&now-last_checked<60)return;last_checked=now;struct tm local;localtime_r(&now,&local);char date[11];strftime(date,sizeof date,"%Y-%m-%d",&local);
 sqlite3_stmt*q=NULL;int enabled=0,days=1;char base[270]="",send_time[8]="00:00";
 if(sqlite3_prepare_v2(db,"SELECT enabled,days_after,send_time FROM review_send_settings WHERE id=1",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW){enabled=sqlite3_column_int(q,0);days=sqlite3_column_int(q,1);snprintf(send_time,sizeof send_time,"%s",sqlite3_column_text(q,2));}sqlite3_finalize(q);q=NULL;
 if(!enabled)return;
 /* Disabling the editable employee template disables automatic review mail. */
 int auto_template=template_state(db,"auto_review_employee");
 if(auto_template==-2){email_preparation_failure("period-preparation",0,"Period emails","Could not read the timesheet email template. Check the database.");return;}
 email_preparation_clear("period-preparation");
 if(auto_template!=1)return;
 note_link_base(base,sizeof base);
 int system_manager=0;
 if(sqlite3_prepare_v2(db,"SELECT e.id FROM employees e JOIN employee_roles r ON r.employee_id=e.id AND r.role='manager' WHERE e.active=1 ORDER BY e.id LIMIT 1",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW){system_manager=sqlite3_column_int(q,0);}
 sqlite3_finalize(q);q=NULL;

 sqlite3_stmt*periods=NULL;
 if(sqlite3_prepare_v2(db,"SELECT id,starts,ends FROM pay_periods WHERE starts<=?1 AND ends>=date(?1,'-14 days') ORDER BY ends",-1,&periods,NULL)!=SQLITE_OK)return;
 sqlite3_bind_text(periods,1,date,-1,SQLITE_TRANSIENT);
 while(sqlite3_step(periods)==SQLITE_ROW){
 int period=sqlite3_column_int(periods,0);char starts[16],ends[16];
 snprintf(starts,sizeof starts,"%s",col_text(periods,1));snprintf(ends,sizeof ends,"%s",col_text(periods,2));
 sqlite3_stmt *due_query=NULL;char due_date[11]="";
 if(sqlite3_prepare_v2(db,"SELECT date(?1,'+'||?2||' days')",-1,&due_query,NULL)==SQLITE_OK){sqlite3_bind_text(due_query,1,ends,-1,SQLITE_TRANSIENT);sqlite3_bind_int(due_query,2,days);if(sqlite3_step(due_query)==SQLITE_ROW)snprintf(due_date,sizeof due_date,"%s",sqlite3_column_text(due_query,0));}sqlite3_finalize(due_query);
 int period_due=*due_date&&send_schedule_due(&local,due_date,send_time);
 sqlite3_stmt*employees=NULL;
 if(sqlite3_prepare_v2(db,"SELECT id,name,email,attendance_mode FROM employees WHERE active=1 AND auto_review_enabled=1 AND instr(coalesce(email,''),'@')>1 ORDER BY id",-1,&employees,NULL)!=SQLITE_OK)continue;
 int all_ok=1;
 while(sqlite3_step(employees)==SQLITE_ROW){
  int employee=sqlite3_column_int(employees,0);char name[192],email[256],token[65];snprintf(name,sizeof name,"%s",col_text(employees,1));snprintf(email,sizeof email,"%s",col_text(employees,2));
  if(strpbrk(email,"\r\n"))continue;
  int current_count=0;long long current_seconds=0;
  rec_employee_totals(employee,starts,ends,&current_count,&current_seconds);
  if(rec_review_required(employee,starts,ends)){if(current_count<=0)continue;}else if(!lite_review_has_context(employee,starts,ends))continue;
  sqlite3_stmt*check=NULL; /* Read-only timesheets require no employee signoff. */
  char event[100];snprintf(event,sizeof event,"auto-review:%d:%d",period,employee);
  int exists=0;
  if(sqlite3_prepare_v2(db,"SELECT 1 FROM email_queue WHERE event_id=?1",-1,&check,NULL)==SQLITE_OK){sqlite3_bind_text(check,1,event,-1,SQLITE_TRANSIENT);exists=sqlite3_step(check)==SQLITE_ROW;}sqlite3_finalize(check);check=NULL;
  if(exists){email_preparation_clear(event);continue;}
  struct review_timing timing;if(!review_timing_for(employee,starts,ends,&timing))continue;
  if(timing.last_scheduled?!review_timing_open(employee,&timing,date,local.tm_hour*60+local.tm_min):!period_due)continue;
  int has_link=review_link_read(employee,period,token,NULL);
  if(has_link<0){email_preparation_failure(event,employee,ends,"Could not read the timesheet link. Check the database.");all_ok=0;continue;}
  if(has_link&&!token[0]){email_preparation_failure(event,employee,ends,"Existing link cannot be emailed. Revoke it under Employee emails and links to issue a replacement.");all_ok=0;continue;}
  if(!base[0]||!system_manager){email_preparation_failure(event,employee,ends,!base[0]?"No hostname is set for links. Configure HTTPS certificate.":"No active manager is available to issue links.");all_ok=0;continue;}
  if(!sql("BEGIN IMMEDIATE")){email_preparation_failure(event,employee,ends,"Database unavailable. Email will be retried automatically.");all_ok=0;continue;}
  const char*prepare_error="Could not issue or renew the timesheet link. Check the database.";
  int ok=has_link||review_link_store(employee,period,system_manager,token);
  char subject[32768],message[32768],link[390];
  snprintf(link,sizeof link,"%s/review/%s",base,token);
  if(ok){prepare_error="Timesheet email could not be rendered. Check Email templates.";ok=timesheet_email(employee,name,starts,ends,link,subject,sizeof subject,message,sizeof message);}
  if(!ok)fprintf(stderr,"Timesheet email could not be prepared for employee %d, period %d\n",employee,period);

  if(ok){prepare_error="Timesheet email could not enter the queue. Check storage and database access.";ok=email_enqueue(db,email,subject,message,event,NULL)>0;}
  if(ok)ok=review_link_extend(employee,period,token);
  if(ok)ok=email_preparation_clear(event);
  if(ok)ok=sqlite3_prepare_v2(db,"INSERT INTO review_link_audit(manager_id,employee_id,period_id,action) VALUES(?3,?1,?2,'automatically emailed')",-1,&q,NULL)==SQLITE_OK;
  if(ok){sqlite3_bind_int(q,1,employee);sqlite3_bind_int(q,2,period);sqlite3_bind_int(q,3,system_manager);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);q=NULL;
  if(ok){ok=sql("COMMIT");}
 if(!ok){sql("ROLLBACK");}
  if(!ok){email_preparation_failure(event,employee,ends,prepare_error);all_ok=0;continue;}

 }
 sqlite3_finalize(employees);
 /* One summary after the period, including all notices queued on earlier workdays. */
 int period_count=0;sqlite3_stmt*total=NULL;
 if(all_ok&&period_due&&sqlite3_prepare_v2(db,"SELECT count(*) FROM email_queue WHERE event_id GLOB 'auto-review:'||?1||':*'",-1,&total,NULL)==SQLITE_OK){sqlite3_bind_int(total,1,period);if(sqlite3_step(total)==SQLITE_ROW)period_count=sqlite3_column_int(total,0);}
 sqlite3_finalize(total);
 if(all_ok&&period_due&&period_count>0){
  char payroll_email[260]="";
  sqlite3_stmt*n=NULL;
  if(sqlite3_prepare_v2(db,"SELECT recipient FROM payroll_settings WHERE id=1",-1,&n,NULL)==SQLITE_OK&&sqlite3_step(n)==SQLITE_ROW)
   snprintf(payroll_email,sizeof payroll_email,"%s",col_text(n,0));
  sqlite3_finalize(n);
  if(email_address_ok(payroll_email)){
   char count_str[32],event[90],subject[300],message[1200];
   snprintf(count_str,sizeof count_str,"%d",period_count);
   snprintf(event,sizeof event,"auto-review-summary:%d",period);
   snprintf(subject,sizeof subject,"Timeclock: Pay reviews issued for %s to %s",starts,ends);
   snprintf(message,sizeof message,"Timesheet notices queued for period %s to %s.\nEmployees emailed: %s\nCheck the email queue for delivery failures.",starts,ends,count_str);
   const struct email_field fields[]={{"{{payday}}",""},{"{{period_start}}",starts},{"{{period_end}}",ends},{"{{sent_count}}",count_str}};
   /* A missing or disabled template means no summary; an unrenderable one sends the default text. */
   int rendered=template_render(db,"review_dispatch_payroll",fields,4,subject,sizeof subject,message,sizeof message);
   if(rendered<0)email_preparation_failure(event,0,ends,"Timesheet delivery summary could not be rendered. Check Email templates.");
   else if(rendered>0){if(email_enqueue(db,payroll_email,subject,message,event,NULL)<0)email_preparation_failure(event,0,ends,"Timesheet delivery summary could not enter the email queue. Check the database.");else email_preparation_clear(event);}
  }
 }

 } /* next eligible pay period */
 sqlite3_finalize(periods);
}
/* When each employee's timesheet link goes out for the current pay period. Read-only. */
static void review_plan(int fd){
 sqlite3_stmt*q=NULL;int period=0,days=1,enabled=0;char starts[16]="",ends[16]="",send_time[8]="00:00",mode[24]="after_period",today[11]="";int now=0;
 if(sqlite3_prepare_v2(db,"SELECT p.id,p.starts,p.ends,date('now','localtime'),CAST(strftime('%H','now','localtime') AS INTEGER)*60+CAST(strftime('%M','now','localtime') AS INTEGER) FROM pay_periods p WHERE p.starts<=date('now','localtime') ORDER BY p.starts DESC LIMIT 1",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW){period=sqlite3_column_int(q,0);snprintf(starts,sizeof starts,"%s",col_text(q,1));snprintf(ends,sizeof ends,"%s",col_text(q,2));snprintf(today,sizeof today,"%s",col_text(q,3));now=sqlite3_column_int(q,4);}
 sqlite3_finalize(q);q=NULL;
 if(sqlite3_prepare_v2(db,"SELECT days_after,send_time,mode,enabled FROM review_send_settings WHERE id=1",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW){days=sqlite3_column_int(q,0);snprintf(send_time,sizeof send_time,"%s",col_text(q,1));snprintf(mode,sizeof mode,"%s",col_text(q,2));enabled=sqlite3_column_int(q,3);}
 sqlite3_finalize(q);q=NULL;
 if(!period){send_response(fd,200,"application/json","{\"period\":null,\"people\":[]}");return;}
 char after[11]="";if(sqlite3_prepare_v2(db,"SELECT date(?1,'+'||?2||' days')",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,ends,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,2,days);if(sqlite3_step(q)==SQLITE_ROW)snprintf(after,sizeof after,"%s",col_text(q,0));}sqlite3_finalize(q);q=NULL;
 sqlite3_str*o=sqlite3_str_new(db);
 sqlite3_str_appendf(o,"{\"period\":{\"id\":%d,\"starts\":\"%s\",\"ends\":\"%s\"},\"company_mode\":\"%s\",\"send_time\":\"%s\",\"people\":[",period,starts,ends,mode,send_time);
 int first=1;
 if(sqlite3_prepare_v2(db,"SELECT e.id,e.name,e.auto_review_enabled,instr(coalesce(e.email,''),'@')>1,"
  "coalesce((SELECT status FROM email_queue WHERE event_id='auto-review:'||?1||':'||e.id),''),"
  "EXISTS(SELECT 1 FROM review_links l WHERE l.employee_id=e.id AND l.period_id=?1 AND l.revoked=0 AND l.token='' AND l.expires_at>CAST(strftime('%s','now') AS INTEGER)) "
  "FROM employees e WHERE e.active=1 ORDER BY e.name COLLATE NOCASE,e.id",-1,&q,NULL)==SQLITE_OK){
  sqlite3_bind_int(q,1,period);
  while(sqlite3_step(q)==SQLITE_ROW){
   int id=sqlite3_column_int(q,0);struct review_timing t;if(!review_timing_for(id,starts,ends,&t))continue;
   /* Same eligibility as the sender: a timesheet with nothing on it is not sent. */
   int count=0;long long secs=0;rec_employee_totals(id,starts,ends,&count,&secs);
   int has_content=rec_review_required(id,starts,ends)?count>0:lite_review_has_context(id,starts,ends);
   int due=t.last_scheduled?review_timing_open(id,&t,today,now):(strcmp(today,after)>0||(!strcmp(today,after)&&now>=hhmm_minutes(send_time)));
   const char*queued=col_text(q,4);const char*status=*queued?(!strcmp(queued,"sent")?"sent":!strcmp(queued,"failed")?"failed":"queued"):sqlite3_column_int(q,5)?"link_unavailable":(!enabled||!sqlite3_column_int(q,2))?"off":template_state(db,"auto_review_employee")!=1?"template_off":!sqlite3_column_int(q,3)?"no_email":!has_content?(due?"nothing":"waiting"):due?"due":"waiting";
   sqlite3_str_appendf(o,"%s{\"id\":%d,\"name\":",first?"":",",id);json_text(o,col_text(q,1));
   char send_day[11];int send_minute=-1;review_send_time(&t,send_day,&send_minute);
   sqlite3_str_appendf(o,",\"timing\":\"%s\",\"date\":\"%s\",\"finish\":%d,\"send_minute\":%d,\"status\":\"%s\"}",t.last_scheduled?"last_scheduled":"after_period",t.last_scheduled?send_day:after,t.finish,send_minute,status);
   first=0;
  }}
 sqlite3_finalize(q);sqlite3_str_appendall(o,"]}");send_str(fd,"application/json",o);
}
static void review_auto_settings(int fd,const char*body){
 if(!body){sqlite3_stmt*q=NULL;int enabled=1,days=1,hours=1;char send_time[8]="00:00",mode[24]="after_period";if(sqlite3_prepare_v2(db,"SELECT enabled,days_after,send_time,mode,hours_before_finish FROM review_send_settings WHERE id=1",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW){enabled=sqlite3_column_int(q,0);days=sqlite3_column_int(q,1);snprintf(send_time,sizeof send_time,"%s",sqlite3_column_text(q,2));snprintf(mode,sizeof mode,"%s",sqlite3_column_text(q,3));hours=sqlite3_column_int(q,4);}sqlite3_finalize(q);char b[200];snprintf(b,sizeof b,"{\"enabled\":%s,\"days\":%d,\"send_time\":\"%s\",\"mode\":\"%s\",\"hours_before_finish\":%d}",enabled?"true":"false",days,send_time,mode,hours);send_response(fd,200,"application/json",b);return;}
 char en[8]="",ds[8]="",send_time[8]="00:00";if(!form_value(body,"enabled",en,sizeof en)||!form_value(body,"days",ds,sizeof ds)|| (strcmp(en,"0")&&strcmp(en,"1"))){send_response(fd,400,"application/json","{\"error\":\"Check the review settings.\"}");return;}
 if((form_has(body,"send_time")&&!form_value(body,"send_time",send_time,sizeof send_time))||!time_valid(send_time)){send_response(fd,400,"application/json","{\"error\":\"Choose a valid send time.\"}");return;}
 char*end=NULL;long days=strtol(ds,&end,10);if(!*ds||*end||days<0||days>7){send_response(fd,400,"application/json","{\"error\":\"Choose 0 to 7 days after a period ends.\"}");return;}
 char mode[24]="";int has_mode=form_has(body,"mode");if(has_mode&&(!form_value(body,"mode",mode,sizeof mode)||(strcmp(mode,"after_period")&&strcmp(mode,"last_scheduled")))){send_response(fd,400,"application/json","{\"error\":\"Choose when timesheet links are sent.\"}");return;}
 char hours_text[16]="";int has_hours=form_has(body,"hours_before_finish");long hours=1;
 if(has_hours){char*tail=NULL;int valid=form_value(body,"hours_before_finish",hours_text,sizeof hours_text);
  if(valid){for(const char*c=hours_text;*c;c++)if(*c<'0'||*c>'9')valid=0;hours=strtol(hours_text,&tail,10);}
  if(!valid||!hours_text[0]||!tail||*tail||hours<0||hours>24){send_response(fd,400,"application/json","{\"error\":\"Choose 0 to 24 whole hours before the final scheduled finish.\"}");return;}}
 sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(db,"UPDATE review_send_settings SET enabled=?1,days_after=?2,send_time=?3,mode=coalesce(?4,mode),hours_before_finish=coalesce(?5,hours_before_finish) WHERE id=1",-1,&q,NULL)==SQLITE_OK;if(ok){sqlite3_bind_int(q,1,en[0]=='1');sqlite3_bind_int(q,2,(int)days);sqlite3_bind_text(q,3,send_time,-1,SQLITE_TRANSIENT);if(has_mode)sqlite3_bind_text(q,4,mode,-1,SQLITE_TRANSIENT);else sqlite3_bind_null(q,4);if(has_hours)sqlite3_bind_int(q,5,(int)hours);else sqlite3_bind_null(q,5);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);send_response(fd,ok?200:500,"application/json",ok?"{\"status\":\"saved\"}":"{\"error\":\"Could not save review settings.\"}");
}
static void review_links_page(int fd){send_response(fd,200,"text/html; charset=utf-8",UI_HEAD("Employee emails and links","review-links") CLOCK_RECORD_WARNING
 "<h1>Employee emails and links</h1><section><form id='dailyForm'><h2>Daily attendance emails</h2><label><input id='dailyEnabled' type='checkbox'> Email a daily attendance snapshot</label><label>Send at (clock local time)<input id='dailyTime' type='time' value='19:00' required></label><p class='muted'>One snapshot at this time for employees with recorded punches today. Includes completed hours and a pay-period link. Running shifts say Still clocked in. Shifts starting after this time wait for a completed interval before emailing that day. Vacation-only employees are excluded. Choose a time after closing.</p><div class='actions'><button id='dailySave'>Save daily emails</button></div><p id='dailyStatus' role='status'></p></form></section><section><form id='autoForm'><h2>Pay-period timesheet emails</h2><label><input type='checkbox' id='autoEnabled' checked> Automatically email timesheet links</label><label>When to send<select id='autoMode'><option value='after_period'>After the period ends</option><option value='last_scheduled'>On each employee&#39;s last scheduled day</option></select></label><p class='muted' id='autoModeHelp'></p><label id='autoHoursLabel'>Send <input type='number' min='0' max='24' step='1' id='autoHours' value='1'> hours before the final scheduled shift ends</label><p class='muted'>Applies to everyone using last scheduled day timing. Zero sends at the scheduled finish. Employees can view their recorded attendance and contact their manager about issues.</p><label id='autoDaysLabel'>Send timesheet links <input type='number' min='0' max='7' id='autoDays' value='1'> days after a period ends</label><label>Send at (clock local time)<input type='time' id='autoTime' value='00:00' required></label><p class='muted'>Automatic links queue at or after this local time; missed runs catch up. Email link sends immediately. Payroll receives one summary at the after-period send time.</p><div class='actions'><button id='autoSave'>Save settings</button></div><p id='autoStatus' role='status'></p></form></section><section><div class='today-head'><h2>This period</h2><p id='planPeriod' class='today-summary'></p></div><table class='plan-table'><thead><tr><th>Employee</th><th>Timing</th><th>Link goes out</th><th>Status</th></tr></thead><tbody id='planRows'><tr><td colspan='4'>Loading...</td></tr></tbody></table></section><p>Each link lets one employee view their recorded timesheet. Links last 14 days from issue or a newly queued pay-period email. Daily emails and copying do not extend expiry. Copy and email reuse a valid link. Revoke it to issue a replacement.</p><section><label>Employee <select id='employee'></select></label><p id='empflag' role='status'></p><label>Pay period <select id='period'></select></label><div class='actions'><button id='generate'>Copy link</button><button id='send'>Email link</button><button class='danger' id='revoke'>Revoke link</button></div><p id='message' role='status'></p><label>Timesheet link<input id='linkout' readonly></label><p class='muted'>Anyone with the link can view this timesheet. Send it only to the employee.</p></section>"
 "<script>(async()=>{const status=document.getElementById('dailyStatus'),enabled=document.getElementById('dailyEnabled'),time=document.getElementById('dailyTime');try{const cfg=await tc.api('/api/v1/admin/daily-attendance/settings');enabled.checked=cfg.enabled;time.value=cfg.send_time}catch(e){tc.err(status,e)}document.getElementById('dailyForm').onsubmit=async ev=>{ev.preventDefault();try{await tc.api('/api/v1/admin/daily-attendance/settings',{method:'POST',body:new URLSearchParams({enabled:enabled.checked?1:0,send_time:time.value})});tc.ok(status,'Daily attendance email settings saved.')}catch(e){tc.err(status,e)}}})();</script>"
 "<script>const generate=document.getElementById('generate'),revoke=document.getElementById('revoke');const e=document.getElementById('employee'),p=document.getElementById('period'),msg=document.getElementById('message'),res=document.getElementById('linkout');let emps=[];const api=tc.api;"
 "function showEmp(){res.value='';let x=emps.find(v=>String(v.id)===e.value),f=document.getElementById('empflag');f.replaceChildren();if(x&&!x.email)f.append(tc.flag('No email','check'),' Email link is not available. Add an email under Employees, or send the link another way.')}"
 "const MODE_HELP={after_period:'Each link goes out the set number of days after the period ends.',last_scheduled:'Each link goes out the chosen number of hours before the final scheduled shift ends. The link is a read-only record. Employees contact their manager about issues. Employees without a scheduled day in the period, and vacation-only employees, use the days after the period ends.'};function modeHelp(){let m=document.getElementById('autoMode').value;document.getElementById('autoHoursLabel').hidden=m!=='last_scheduled';document.getElementById('autoModeHelp').textContent=MODE_HELP[m];document.getElementById('autoDaysLabel').firstChild.textContent=m==='last_scheduled'?'Without a scheduled day, send ':'Send timesheet links '}function niceDay(d){return tc.date(d,'day')}async function loadPlan(){let j=await api('/api/v1/admin/review/plan'),rows=document.getElementById('planRows');rows.replaceChildren();if(!j.period){let tr=document.createElement('tr'),td=document.createElement('td');td.colSpan=4;td.textContent='No current pay period.';tr.append(td);rows.append(tr);return}document.getElementById('planPeriod').textContent=niceDay(j.period.starts)+' to '+niceDay(j.period.ends);for(let x of j.people){let tr=document.createElement('tr'),timing=x.timing==='last_scheduled'?'Last scheduled day':'After the period ends';let when=niceDay(x.date)+', '+tc.hm(x.timing==='last_scheduled'?x.send_minute:Number(j.send_time.slice(0,2))*60+Number(j.send_time.slice(3,5)));let st={sent:tc.flag('Sent','ok'),queued:tc.flag('Queued','muted'),failed:tc.flag('Delivery failed','problem'),template_off:tc.flag('Email template off','muted'),link_unavailable:tc.flag('Existing link cannot be emailed','check'),off:tc.flag('Automatic email off','muted'),no_email:tc.flag('No email','check'),waiting:tc.flag('Waiting','muted'),due:tc.flag('Due','check'),nothing:tc.flag('No attendance recorded','muted')}[x.status];if(x.status==='failed')tr.className='row-problem';else if(x.status==='no_email')tr.className='row-check';for(let v of [x.name,timing,when,st]){let td=document.createElement('td');td.append(v);tr.append(td)}rows.append(tr)}}async function load(){let cfg=await api('/api/v1/admin/review/auto-settings');document.getElementById('autoEnabled').checked=cfg.enabled;document.getElementById('autoMode').value=cfg.mode||'after_period';modeHelp();document.getElementById('autoDays').value=cfg.days;document.getElementById('autoTime').value=cfg.send_time||'00:00';document.getElementById('autoHours').value=cfg.hours_before_finish??1;loadPlan().catch(x=>tc.err(document.getElementById('autoStatus'),x));let d=await api('/api/v1/admin/review/choices');emps=d.employees;for(let x of d.employees){let o=document.createElement('option');o.value=x.id;o.textContent=x.name+(x.email?' ('+x.email+')':' (no email)');e.append(o)}for(let x of d.periods){let o=document.createElement('option');o.value=x.id;o.textContent=x.label;p.append(o)}showEmp()}e.onchange=showEmp;document.getElementById('autoMode').onchange=modeHelp;p.onchange=()=>{res.value='';};"
 "async function issue(sendNow){try{let j=await api('/api/v1/admin/review/generate',{method:'POST',body:new URLSearchParams({employee:e.value,period:p.value,send:sendNow?'1':'0'})});let h=await api('/api/v1/admin/note-link');if(!h.effective)throw Error('No hostname is set for links. Install a certificate under HTTPS certificate.');res.value=h.effective+j.path;if(!sendNow){try{await navigator.clipboard.writeText(res.value)}catch(err){tc.ok(msg,'Select and copy the link below.');return}}tc.ok(msg,sendNow?(j.queued?'Email queued for '+j.recipient+'.':(j.email_status==='already_sent'?'Already sent to ':'Already queued for ')+j.recipient+'. See Email queue.'):'Link copied.');await loadPlan()}catch(x){tc.err(msg,x)}}"
 "document.getElementById('autoForm').onsubmit=async ev=>{ev.preventDefault();let st=document.getElementById('autoStatus');try{await api('/api/v1/admin/review/auto-settings',{method:'POST',body:new URLSearchParams({enabled:document.getElementById('autoEnabled').checked?'1':'0',mode:document.getElementById('autoMode').value,days:document.getElementById('autoDays').value,send_time:document.getElementById('autoTime').value,hours_before_finish:document.getElementById('autoHours').value})});tc.ok(st,'Timesheet email settings saved.');await loadPlan()}catch(e){tc.err(st,e)}};generate.onclick=()=>issue(false);document.getElementById('send').onclick=()=>issue(true);revoke.onclick=async()=>{try{if(!await tc.ask({title:'Revoke the link for this employee and period?',ok:'Revoke link',danger:true}))return;await api('/api/v1/admin/review/revoke',{method:'POST',body:new URLSearchParams({employee:e.value,period:p.value})});res.value='';tc.ok(msg,'Link revoked.')}catch(x){tc.err(msg,x)}};load().catch(x=>tc.err(msg,x))</script>" UI_FOOT);}
