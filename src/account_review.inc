/* Current-baseline manager accounts and passwordless, one-period employee reviews. */
static const char *ar_text(sqlite3_stmt*q,int c){const unsigned char*p=sqlite3_column_text(q,c);return p?(const char*)p:"";}
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
 if(sqlite3_prepare_v2(db,"SELECT e.id,e.name,coalesce(e.email,''),coalesce(u.disabled,1),u.password_hash IS NOT NULL FROM employees e JOIN employee_roles r ON r.employee_id=e.id AND r.role='manager' LEFT JOIN user_accounts u ON u.employee_id=e.id WHERE e.active=1 ORDER BY e.name",-1,&q,NULL)==SQLITE_OK){while(sqlite3_step(q)==SQLITE_ROW){if(n++)sqlite3_str_appendchar(out,1,',');sqlite3_str_appendf(out,"{\"id\":%d,\"name\":",sqlite3_column_int(q,0));str_json(out,ar_text(q,1));sqlite3_str_appendall(out,",\"email\":");str_json(out,ar_text(q,2));sqlite3_str_appendf(out,",\"locked\":%s,\"has_login\":%s}",sqlite3_column_int(q,3)?"true":"false",sqlite3_column_int(q,4)?"true":"false");}}sqlite3_finalize(q);sqlite3_str_appendall(out,"]}");send_str(fd,"application/json",out);
}
static void accounts_self_password(int fd,int who,const char*body){
 char current[256]="",next[256]="",hash[256]="";
 if(!form_value(body,"current",current,sizeof current)||!form_value(body,"next",next,sizeof next)||!ar_hash_password(next,hash)){send_response(fd,400,"application/json","{\"error\":\"Enter your current password and a new password of at least 12 characters.\"}");return;}
 sqlite3_stmt*q=NULL;char stored[256]="";
 if(sqlite3_prepare_v2(db,"SELECT password_hash FROM user_accounts WHERE employee_id=?1 AND disabled=0",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,who);if(sqlite3_step(q)==SQLITE_ROW)snprintf(stored,sizeof stored,"%s",ar_text(q,0));}sqlite3_finalize(q);
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
 if(!sql("BEGIN IMMEDIATE")){send_response(fd,503,"application/json","{\"error\":\"The clock is busy. Try again.\"}");return;}
 if(authorization_pending()){sql("ROLLBACK");send_response(fd,409,"application/json","{\"error\":\"The card reader is waiting for another approval. Try again in 30 seconds.\"}");return;}
 sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(db,"INSERT INTO account_authorization(id,requested_by,employee_id,action,proposed_hash,armed_at) VALUES(1,?1,?2,?3,?4,CAST(strftime('%s','now') AS INTEGER)) ON CONFLICT(id) DO UPDATE SET requested_by=excluded.requested_by,employee_id=excluded.employee_id,action=excluded.action,proposed_hash=excluded.proposed_hash,armed_at=excluded.armed_at",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,who);sqlite3_bind_int(q,2,id);sqlite3_bind_text(q,3,action,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,4,hash,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 char token[33]="";if(ok)ok=sql("UPDATE manager_scan SET armed_at=CAST(strftime('%s','now') AS INTEGER),uid='',scanned_at=0,scan_mode='approval' WHERE id=1")&&approval_token_new(token);
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");char reply[128];snprintf(reply,sizeof reply,"{\"status\":\"waiting\",\"token\":\"%s\"}",token);send_response(fd,ok?200:500,"application/json",ok?reply:"{\"error\":\"The card reader is waiting for another approval. Try again in 30 seconds.\"}");
}
static void accounts_status(int fd,int who){
 sqlite3_stmt*q=NULL;int owner=0,target=0;sqlite3_int64 armed=0,scanned=0;char action[24]="",hash[256]="",uid[48]="";
 if(sqlite3_prepare_v2(db,"SELECT a.requested_by,a.employee_id,a.action,coalesce(a.proposed_hash,''),a.armed_at,s.uid,s.scanned_at FROM account_authorization a JOIN manager_scan s ON s.id=1 WHERE a.id=1",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW){owner=sqlite3_column_int(q,0);target=sqlite3_column_int(q,1);snprintf(action,sizeof action,"%s",ar_text(q,2));snprintf(hash,sizeof hash,"%s",ar_text(q,3));armed=sqlite3_column_int64(q,4);snprintf(uid,sizeof uid,"%s",ar_text(q,5));scanned=sqlite3_column_int64(q,6);}sqlite3_finalize(q);
 if(owner!=who||armed==0){send_response(fd,409,"application/json","{\"error\":\"This approval is no longer valid. Start again.\"}");return;}
 if(time(NULL)-armed>30){sql("DELETE FROM account_authorization WHERE id=1");sql("UPDATE manager_scan SET armed_at=0,uid='',scanned_at=0,request_token='' WHERE id=1");send_response(fd,200,"application/json","{\"status\":\"expired\"}");return;}
 if(!*uid||scanned<armed){send_response(fd,200,"application/json","{\"status\":\"waiting\"}");return;}
 int authorized=0;
 if(sqlite3_prepare_v2(db,"SELECT 1 FROM manager_cards c JOIN employees e ON e.id=c.manager_id JOIN employee_roles r ON r.employee_id=e.id AND r.role='manager' JOIN user_accounts u ON u.employee_id=e.id WHERE c.uid=?1 COLLATE NOCASE AND c.active=1 AND e.active=1 AND u.disabled=0",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,uid,-1,SQLITE_TRANSIENT);authorized=sqlite3_step(q)==SQLITE_ROW;}sqlite3_finalize(q);
 if(!authorized){sql("UPDATE manager_scan SET uid='',scanned_at=0 WHERE id=1");send_response(fd,200,"application/json","{\"status\":\"waiting\"}");return;}
 if(!sql("BEGIN IMMEDIATE")){send_response(fd,503,"application/json","{\"error\":\"The clock is busy. Try again.\"}");return;}
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
 "<script>const w=document.getElementById('who'),a=document.getElementById('action'),pw=document.getElementById('password'),status=document.getElementById('status');const api=tc.api;async function load(){let x=await api('/api/v1/admin/accounts');w.replaceChildren();for(let e of x.accounts){let o=document.createElement('option');o.value=e.id;o.textContent=e.name+' ('+(e.locked?'locked':e.has_login?'can sign in':'no sign-in')+')';w.append(o)}}a.onchange=()=>pwbox.hidden=a.value!=='set';document.getElementById('apply').onclick=async()=>{try{let p=new URLSearchParams({employee:w.value,action:a.value,password:pw.value});let j=await api('/api/v1/admin/accounts/arm',{method:'POST',body:p});status.textContent=tc.CARD;async function poll(){try{let x=await api('/api/v1/admin/accounts/status',{method:'POST',body:new URLSearchParams({token:j.token})});if(x.status==='waiting'){setTimeout(poll,700);return}if(x.status==='expired')throw Error(tc.TIMEOUT);tc.ok(status,{set:'Password set.',lock:'Account locked.',unlock:'Account unlocked.'}[a.value]||'Saved.');pw.value='';await load()}catch(e){tc.err(status,e)}}setTimeout(poll,700)}catch(e){tc.err(status,e)}};own.onsubmit=async e=>{e.preventDefault();try{let j=await api('/api/v1/admin/accounts/change-password',{method:'POST',body:new URLSearchParams(new FormData(own))});own.reset();tc.ok(ownstate,'Password changed. Sign in again.');setTimeout(()=>location.assign('/login'),1500)}catch(e){tc.err(ownstate,e)}};load().catch(e=>tc.err(status,e))</script>" UI_FOOT);}

/* SHA-512 crypt hashes keep bearer credentials out of the database and backups. */
static int review_hash(const char*token,char hash[256]){
 if(strlen(token)!=64)return 0;
 for(const char*p=token;*p;p++)if(!isxdigit((unsigned char)*p))return 0;
 const char*r=crypt(token,"$6$rounds=5000$payreview2026$");if(!r||strlen(r)>=256||strncmp(r,"$6$",3))return 0;
 snprintf(hash,256,"%s",r);return 1;
}
static void review_choices(int fd){
 sqlite3_stmt*q=NULL;sqlite3_str*out=sqlite3_str_new(db);sqlite3_str_appendall(out,"{\"employees\":[");int n=0;
 if(sqlite3_prepare_v2(db,"SELECT id,name,coalesce(email,'') FROM employees WHERE active=1 ORDER BY name",-1,&q,NULL)==SQLITE_OK){while(sqlite3_step(q)==SQLITE_ROW){if(n++)sqlite3_str_appendchar(out,1,',');sqlite3_str_appendf(out,"{\"id\":%d,\"name\":",sqlite3_column_int(q,0));str_json(out,ar_text(q,1));sqlite3_str_appendall(out,",\"email\":");str_json(out,ar_text(q,2));sqlite3_str_appendchar(out,1,'}');}}sqlite3_finalize(q);
 sqlite3_str_appendall(out,"],\"periods\":[");n=0;
 if(sqlite3_prepare_v2(db,"SELECT id,starts,ends,status FROM pay_periods ORDER BY starts DESC LIMIT 60",-1,&q,NULL)==SQLITE_OK){while(sqlite3_step(q)==SQLITE_ROW){if(n++)sqlite3_str_appendchar(out,1,',');sqlite3_str_appendf(out,"{\"id\":%d,\"label\":",sqlite3_column_int(q,0));char label[90];snprintf(label,sizeof label,"%s to %s%s",ar_text(q,1),ar_text(q,2),strcmp(ar_text(q,3),"closed")?"":" (closed)");str_json(out,label);sqlite3_str_appendchar(out,1,'}');}}sqlite3_finalize(q);sqlite3_str_appendall(out,"]}");send_str(fd,"application/json",out);
}
static int review_ids(const char*body,int*eid,int*pid){char e[24]="",p[24]="";if(!form_value(body,"employee",e,sizeof e)||!form_value(body,"period",p,sizeof p))return 0;*eid=ar_employee(e);*pid=ar_employee(p);return *eid&&*pid;}
static void review_generate(int fd,int who,const char*body,int email_now){
 int employee=0,period=0;if(!review_ids(body,&employee,&period)){send_response(fd,400,"application/json","{\"error\":\"Select an employee and a pay period.\"}");return;}
 sqlite3_stmt*q=NULL;int valid=0;
 if(sqlite3_prepare_v2(db,"SELECT 1 FROM employees e CROSS JOIN pay_periods p WHERE e.id=?1 AND e.active=1 AND p.id=?2",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,employee);sqlite3_bind_int(q,2,period);valid=sqlite3_step(q)==SQLITE_ROW;}sqlite3_finalize(q);
 if(!valid){send_response(fd,404,"application/json","{\"error\":\"Employee or pay period not found. Reload the page.\"}");return;}
 char token[65],hash[256];if(!random_hex(token,32)||!review_hash(token,hash)){send_response(fd,500,"application/json","{\"error\":\"Could not create a link. Try again.\"}");return;}
 char recipient[256]="",employee_name[192]="",period_start[16]="",period_end[16]="",base[270]="";
 if(email_now){
  if(sqlite3_prepare_v2(db,"SELECT coalesce(email,''),name FROM employees WHERE id=?1 AND active=1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,employee);if(sqlite3_step(q)==SQLITE_ROW)snprintf(recipient,sizeof recipient,"%s",ar_text(q,0));snprintf(employee_name,sizeof employee_name,"%s",ar_text(q,1));}sqlite3_finalize(q);q=NULL;
  if(sqlite3_prepare_v2(db,"SELECT starts,ends FROM pay_periods WHERE id=?1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,period);if(sqlite3_step(q)==SQLITE_ROW){snprintf(period_start,sizeof period_start,"%s",ar_text(q,0));snprintf(period_end,sizeof period_end,"%s",ar_text(q,1));}}sqlite3_finalize(q);q=NULL;
  note_link_base(base,sizeof base);
  if(!strchr(recipient,'@')||strpbrk(recipient,"\r\n")){send_response(fd,422,"application/json","{\"error\":\"This employee has no email address. Add one under Employees.\"}");return;}
  if(!base[0]){send_response(fd,422,"application/json","{\"error\":\"No hostname is set for links. Install a certificate under HTTPS certificate.\"}");return;}
  sqlite3_stmt*enabled_tpl=NULL;int reminder_enabled=0;
  if(sqlite3_prepare_v2(db,"SELECT enabled FROM email_templates WHERE id='review_reminder_employee'",-1,&enabled_tpl,NULL)==SQLITE_OK&&sqlite3_step(enabled_tpl)==SQLITE_ROW)reminder_enabled=sqlite3_column_int(enabled_tpl,0);
  sqlite3_finalize(enabled_tpl);
  if(!reminder_enabled){send_response(fd,422,"application/json","{\"error\":\"Timesheet reminder is turned off in Email templates.\"}");return;}
 }
 int ok=sql("BEGIN IMMEDIATE");if(ok)ok=sqlite3_prepare_v2(db,"INSERT INTO review_links(employee_id,period_id,token_hash,expires_at,created_by,created_at,revoked) VALUES(?1,?2,?3,CAST(strftime('%s','now') AS INTEGER)+1209600,?4,CAST(strftime('%s','now') AS INTEGER),0) ON CONFLICT(employee_id,period_id) DO UPDATE SET token_hash=excluded.token_hash,expires_at=excluded.expires_at,created_by=excluded.created_by,created_at=excluded.created_at,revoked=0",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,employee);sqlite3_bind_int(q,2,period);sqlite3_bind_text(q,3,hash,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,4,who);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 if(ok)ok=sqlite3_prepare_v2(db,"INSERT INTO review_link_audit(manager_id,employee_id,period_id,action) VALUES(?1,?2,?3,'issued or replaced')",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,who);sqlite3_bind_int(q,2,employee);sqlite3_bind_int(q,3,period);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);
 if(ok&&email_now){
  char link[390],message[1100],subject[180];
  snprintf(link,sizeof link,"%s/review/%s",base,token);
  snprintf(subject,sizeof subject,"Timeclock: Timesheet %s to %s",period_start,period_end);
  snprintf(message,sizeof message,"Hello %s,\n\nYour timesheet for %s to %s is ready. View the punches recorded by the timeclock here:\n\n%s\n\nThe link expires in 14 days. Do not forward it. Contact your manager if anything is wrong.",employee_name,period_start,period_end,link);
  /* A manager's manual email is the explicit reminder/resend path. */
  sqlite3_stmt*rt=NULL;
  if(sqlite3_prepare_v2(db,"SELECT subject,body FROM email_templates WHERE id='review_reminder_employee' AND enabled=1",-1,&rt,NULL)==SQLITE_OK){
   if(sqlite3_step(rt)==SQLITE_ROW){
    const struct email_field vars[]={{"{{employee_name}}",employee_name},{"{{period_start}}",period_start},{"{{period_end}}",period_end},{"{{review_link}}",link}};
    char *rs=email_render(ar_text(rt,0),vars,4),*rb=email_render(ar_text(rt,1),vars,4);
    if(rs&&rb&&strlen(rs)<sizeof subject&&strlen(rb)<sizeof message){snprintf(subject,sizeof subject,"%s",rs);snprintf(message,sizeof message,"%s",rb);}
    free(rs);free(rb);
   }
  }sqlite3_finalize(rt);
  ok=sqlite3_prepare_v2(db,"INSERT OR IGNORE INTO email_queue(recipient,subject,body,status,next_attempt,event_id) VALUES(?1,?2,?3,'pending',strftime('%Y-%m-%dT%H:%M:%fZ','now'),?4)",-1,&q,NULL)==SQLITE_OK;
  if(ok){char key[150];snprintf(key,sizeof key,"pay_review:%d:%d:%.64s",employee,period,token);sqlite3_bind_text(q,1,recipient,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,subject,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,message,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,4,key,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);q=NULL;
 }
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");
 if(!ok){send_response(fd,500,"application/json","{\"error\":\"The link was not created. Try again.\"}");return;}
 char reply[160];snprintf(reply,sizeof reply,"{\"path\":\"/review/%s\",\"expires_in_days\":14}",token);send_response(fd,200,"application/json",reply);
}
static void review_revoke(int fd,int who,const char*body){
 int eid=0,pid=0;if(!review_ids(body,&eid,&pid)){send_response(fd,400,"application/json","{\"error\":\"Request incomplete. Reload the page and try again.\"}");return;}
 sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(db,"UPDATE review_links SET revoked=1 WHERE employee_id=?1 AND period_id=?2",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,eid);sqlite3_bind_int(q,2,pid);ok=sqlite3_step(q)==SQLITE_DONE&&sqlite3_changes(db)==1;}sqlite3_finalize(q);
 if(ok){sqlite3_stmt*a=NULL;int audit=sqlite3_prepare_v2(db,"INSERT INTO review_link_audit(manager_id,employee_id,period_id,action) VALUES(?1,?2,?3,'revoked')",-1,&a,NULL)==SQLITE_OK;if(audit){sqlite3_bind_int(a,1,who);sqlite3_bind_int(a,2,eid);sqlite3_bind_int(a,3,pid);audit=sqlite3_step(a)==SQLITE_DONE;}sqlite3_finalize(a);if(!audit)ok=0;}
 send_response(fd,ok?200:404,"application/json",ok?"{\"status\":\"revoked\"}":"{\"error\":\"There is no active link for this employee and period.\"}");
}
static int review_validate(const char*token,int*employee,int*period){
 char hash[256];if(!review_hash(token,hash))return 0;sqlite3_stmt*q=NULL;int ok=0;
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
 "WHERE CAST(strftime('%w',d.day) AS INTEGER) BETWEEN 1 AND 5 ORDER BY 1,2";
static int lite_review_has_context(int employee,const char *starts,const char *ends){
 sqlite3_stmt*q=NULL;int found=0;
 if(sqlite3_prepare_v2(db,lite_review_context_sql,-1,&q,NULL)==SQLITE_OK){
  sqlite3_bind_int(q,1,employee);sqlite3_bind_text(q,2,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,ends,-1,SQLITE_TRANSIENT);
  while(sqlite3_step(q)==SQLITE_ROW){if(strcmp(ar_text(q,2),"holiday")){found=1;break;}}
 }
 sqlite3_finalize(q);return found;
}
static void review_detail(int fd,int employee,int period){
 sqlite3_stmt*q=NULL;sqlite3_str*out=sqlite3_str_new(db);int ok=0;char starts[16]="",ends[16]="",status[24]="";
 if(sqlite3_prepare_v2(db,"SELECT starts,ends,status FROM pay_periods WHERE id=?1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,period);if(sqlite3_step(q)==SQLITE_ROW){snprintf(starts,sizeof starts,"%s",ar_text(q,0));snprintf(ends,sizeof ends,"%s",ar_text(q,1));snprintf(status,sizeof status,"%s",ar_text(q,2));ok=1;}}sqlite3_finalize(q);
 if(!ok){sqlite3_str_finish(out);send_response(fd,404,"application/json","{\"error\":\"Pay period not found. Reload the page.\"}");return;}
 int count=0;long long secs=0;rec_employee_totals(employee,starts,ends,&count,&secs);
 int vacation_only=0;
 if(sqlite3_prepare_v2(db,"SELECT attendance_mode FROM employees WHERE id=?1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,employee);if(sqlite3_step(q)==SQLITE_ROW)vacation_only=!strcmp(ar_text(q,0),"vacation_only");}sqlite3_finalize(q);q=NULL;
 int signed_off=0;if(sqlite3_prepare_v2(db,"SELECT punch_count,approved_seconds,leave_fingerprint FROM period_signoffs WHERE period_id=?1 AND employee_id=?2 AND role='employee'",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,period);sqlite3_bind_int(q,2,employee);if(sqlite3_step(q)==SQLITE_ROW){char fp[17]="";signed_off=rec_leave_fingerprint(employee,starts,ends,fp)&&sqlite3_column_int(q,0)==count&&sqlite3_column_int64(q,1)==secs&&!strcmp(fp,ar_text(q,2));}}sqlite3_finalize(q);
 sqlite3_str_appendf(out,"{\"id\":%d,\"starts\":",period);str_json(out,starts);sqlite3_str_appendall(out,",\"ends\":");str_json(out,ends);sqlite3_str_appendall(out,",\"status\":");str_json(out,status);int ready=0;if(sqlite3_prepare_v2(db,"SELECT ?1<=date('now','localtime')",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,ends,-1,SQLITE_TRANSIENT);if(sqlite3_step(q)==SQLITE_ROW)ready=sqlite3_column_int(q,0);}sqlite3_finalize(q);q=NULL;
 sqlite3_str_appendf(out,",\"signed\":%s,\"ready\":%s,\"vacation_only\":%s,\"punches\":%d,\"seconds\":%lld,\"entries\":[",signed_off?"true":"false",ready?"true":"false",vacation_only?"true":"false",count,secs);
 int n=0;
 if(sqlite3_prepare_v2(db,"SELECT x.in_at,x.out_at,x.seconds,x.state,x.manual,"
 " coalesce((SELECT c.reason FROM punch_corrections c WHERE c.event_id=x.in_id OR c.manual_id=x.in_id ORDER BY c.id DESC LIMIT 1),ni.note,''),coalesce(ai.late_minutes,0),"
 " EXISTS(SELECT 1 FROM punch_corrections c WHERE c.event_id=x.in_id AND c.action='change'),"
 " coalesce((SELECT c.reason FROM punch_corrections c WHERE c.event_id=x.out_id OR c.manual_id=x.out_id ORDER BY c.id DESC LIMIT 1),no.note,''),"
 " EXISTS(SELECT 1 FROM punch_corrections c WHERE c.event_id=x.out_id AND c.action='change'),"
 " coalesce((SELECT p.manual FROM effective_punches p WHERE p.id=x.out_id),0)"
 " FROM effective_shift_pairs x LEFT JOIN swipe_notes ni ON ni.event_id=x.in_id LEFT JOIN attendance_events ai ON ai.event_id=x.in_id LEFT JOIN swipe_notes no ON no.event_id=x.out_id WHERE x.employee_id=?1 AND date(in_at,'localtime') BETWEEN ?2 AND ?3 ORDER BY in_at,in_id LIMIT 500",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,employee);sqlite3_bind_text(q,2,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,ends,-1,SQLITE_TRANSIENT);while(sqlite3_step(q)==SQLITE_ROW){if(n++)sqlite3_str_appendchar(out,1,',');sqlite3_str_appendall(out,"{\"in_at\":");str_json(out,ar_text(q,0));sqlite3_str_appendall(out,",\"out_at\":");if(sqlite3_column_type(q,1)==SQLITE_NULL)sqlite3_str_appendall(out,"null");else str_json(out,ar_text(q,1));sqlite3_str_appendf(out,",\"seconds\":%lld,\"state\":",sqlite3_column_int64(q,2));str_json(out,ar_text(q,3));sqlite3_str_appendf(out,",\"manual\":%s,\"out_manual\":%s,\"in_corrected\":%s,\"out_corrected\":%s,\"late_minutes\":%d,\"in_note\":",sqlite3_column_int(q,4)?"true":"false",sqlite3_column_int(q,10)?"true":"false",sqlite3_column_int(q,7)?"true":"false",sqlite3_column_int(q,9)?"true":"false",sqlite3_column_int(q,6));str_json(out,ar_text(q,5));sqlite3_str_appendall(out,",\"out_note\":");str_json(out,ar_text(q,8));sqlite3_str_appendchar(out,1,'}');}}sqlite3_finalize(q);
 sqlite3_str_appendall(out,"],\"context\":[");
 int context_count=0;

 if(sqlite3_prepare_v2(db,lite_review_context_sql,-1,&q,NULL)==SQLITE_OK){
  sqlite3_bind_int(q,1,employee);sqlite3_bind_text(q,2,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,ends,-1,SQLITE_TRANSIENT);
  while(sqlite3_step(q)==SQLITE_ROW){if(context_count++)sqlite3_str_appendchar(out,1,',');sqlite3_str_appendall(out,"{\"day\":");str_json(out,ar_text(q,0));sqlite3_str_appendall(out,",\"label\":");str_json(out,ar_text(q,1));sqlite3_str_appendall(out,",\"type\":");str_json(out,ar_text(q,2));sqlite3_str_appendchar(out,1,'}');}
 }sqlite3_finalize(q);
 sqlite3_str_appendall(out,"]}");send_str(fd,"application/json",out);
}
static void review_route(int fd,const char*method,const char*path,const char*request){
 const char*t=path+8;if(strlen(t)<64){review_invalid(fd);return;}
 char token[65];memcpy(token,t,64);token[64]=0;
 const char*suffix=t+64;int e=0,p=0;if(!review_validate(token,&e,&p)){review_invalid(fd);return;}
 if(!strcmp(method,"GET")&&!*suffix){send_response(fd,200,"text/html; charset=utf-8",UI_HEAD_LOGIN("Your timesheet")
 "<script>document.body.classList.add('review-employee')</script><h1>Your timesheet</h1><section><h2 id=\"summary\">Loading...</h2><p id=\"reviewstate\"></p><p id=\"revbanner\" role=\"alert\" hidden></p><table><thead><tr><th>Day</th><th>IN / OUT</th><th>Hours</th></tr></thead><tbody id=\"entries\"></tbody></table><div class=\"actions\"><button id=\"wrong\" type=\"button\" class=\"secondary\">Something is wrong</button><button id=\"approve\" type=\"button\" class=\"primary\">Approve timesheet</button></div><p id=\"status\" role=\"status\"></p></section>"
 "<script>const base=location.pathname,info=document.getElementById('summary'),msg=document.getElementById('status');const call=tc.api;function shortDate(x){return x}function clock(t){return new Date(t).toLocaleTimeString(undefined,{hour:'numeric',minute:'2-digit'})}async function load(){let p=await call(base+'/detail'),rows=document.getElementById('entries'),bad=0;let ctx=p.context||[];if(p.vacation_only){let labels={'timeoff:vacation':['vacation day','vacation days'],'timeoff:personal':['personal day','personal days'],'timeoff:unpaid':['unpaid leave day','unpaid leave days'],'timeoff:other':['other time off','other time off'],'absence:Sick':['sick day','sick days'],'absence:Medical appointment':['medical appointment','medical appointments'],'absence:Personal day':['personal day','personal days'],'absence:Family emergency':['family emergency day','family emergency days'],'absence:Bereavement':['bereavement day','bereavement days'],'absence:Unpaid leave':['unpaid leave day','unpaid leave days'],'absence:Unexcused absence':['unexcused absence','unexcused absences'],'absence:Other':['other absence','other absences'],'holiday':['stat holiday','stat holidays']};let tally=new Map();for(let x of ctx){let key=x.type==='absence'?'absence:'+x.label:x.type;if(!labels[key])continue;let name=labels[key][0];if(!tally.has(name))tally.set(name,{days:new Set(),forms:labels[key]});tally.get(name).days.add(x.day)}let parts=[];for(let v of tally.values()){let n=v.days.size;if(n)parts.push(n+' '+v.forms[n===1?0:1])}info.textContent=shortDate(p.starts)+' to '+shortDate(p.ends)+(parts.length?': '+parts.join(', '):': No leave or holidays recorded');}else info.textContent=(p.seconds/3600).toFixed(2)+' hours for '+shortDate(p.starts)+' to '+shortDate(p.ends);document.getElementById('reviewstate').textContent=p.signed?'You approved this timesheet.':(p.status==='closed'?'This period is closed.':(!p.ready?'Approval opens on the last day of this period.':'Review your recorded attendance and approve when correct.'));let approve=document.getElementById('approve');approve.hidden=p.signed||p.status==='closed'||!p.ready;rows.replaceChildren();for(let x of p.entries){let day=new Date(x.in_at).toLocaleDateString('en-CA'),problem=x.state!=='complete',tr=document.createElement('tr');tr.dataset.day=day;let interval=x.state==='missing_in'?'(missing IN)':clock(x.in_at)+' to '+(x.out_at?clock(x.out_at)+(new Date(x.out_at).toLocaleDateString('en-CA')!==day?' (next day)':''):'(missing OUT)');for(let v of [new Date(day+'T12:00:00').toLocaleDateString(undefined,{weekday:'short',month:'short',day:'numeric'}),interval,(x.seconds/3600).toFixed(2)+' h']){let td=document.createElement('td');td.textContent=v;tr.append(td)}if(problem){bad++;tr.className='row-problem';tr.children[1].append(' ',tc.flag('Missing punch','problem'))}if(x.manual||x.out_manual)tr.children[1].append(' ',tc.flag('Manager-added punch','check'));if(x.in_corrected||x.out_corrected)tr.children[1].append(' ',tc.flag('Manager correction','check'));if(x.late_minutes>0)tr.children[1].append(' ',tc.flag('Late: '+x.late_minutes+' min','check'));for(let note of [x.in_note,x.out_note])if(note)tr.children[1].append(' Reason: '+note);rows.append(tr)}for(let x of p.context||[]){let tr=document.createElement('tr');tr.className='row-context';tr.dataset.day=x.day;for(let value of [new Date(x.day+'T12:00:00').toLocaleDateString(undefined,{weekday:'short',month:'short',day:'numeric'}),x.label,p.vacation_only?'': '0.00 h']){let td=document.createElement('td');td.textContent=value;tr.append(td)}rows.append(tr)}Array.from(rows.children).sort((a,b)=>{return (a.dataset.day||'').localeCompare(b.dataset.day||'')}).forEach(x=>rows.append(x));if(!p.entries.length&&!(p.context||[]).length){let tr=document.createElement('tr'),td=document.createElement('td');td.colSpan=3;td.textContent='No punches recorded.';tr.append(td);rows.append(tr)}tc.banner(document.getElementById('revbanner'),'problem','Problem',bad?bad+(bad===1?' incomplete shift':' incomplete shifts')+'. Contact your manager.':'');approve.disabled=bad>0;}document.getElementById('approve').onclick=async()=>{if(!confirm('Approve this recorded timesheet? Your manager can still correct errors.'))return;try{await call(base+'/approve',{method:'POST'});tc.ok(msg,'Timesheet approved.');await load()}catch(e){tc.err(msg,e)}};document.getElementById('wrong').onclick=async()=>{let note=prompt('What looks wrong with your hours?');if(note===null)return;if(note.trim().length<5){tc.err(msg,Error('Describe what is wrong first.'));return}try{await call(base+'/issue',{method:'POST',body:new URLSearchParams({note:note.trim()})});tc.ok(msg,'Your concern has been recorded for manager review.')}catch(e){tc.err(msg,e)}};load().catch(e=>tc.err(msg,e))</script>" UI_FOOT);return;}
 if(!strcmp(method,"GET")&&!strcmp(suffix,"/detail")){review_detail(fd,e,p);return;}
 if(!strcmp(method,"POST")&&!strcmp(suffix,"/issue")){
  const char*raw=strstr(request,"\r\n\r\n");char note[481]="";
  if(!raw||!form_value_multiline(raw+4,"note",note,sizeof note)||strlen(note)<5){send_response(fd,422,"application/json","{\"error\":\"Describe what is wrong in at least 5 characters.\"}");return;}
  sqlite3_stmt*q=NULL;char name[192]="",starts[16]="",ends[16]="";
  if(sqlite3_prepare_v2(db,"SELECT name FROM employees WHERE id=?1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,e);if(sqlite3_step(q)==SQLITE_ROW)snprintf(name,sizeof name,"%s",ar_text(q,0));}sqlite3_finalize(q);q=NULL;
  if(sqlite3_prepare_v2(db,"SELECT starts,ends FROM pay_periods WHERE id=?1",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,p);if(sqlite3_step(q)==SQLITE_ROW){snprintf(starts,sizeof starts,"%s",ar_text(q,0));snprintf(ends,sizeof ends,"%s",ar_text(q,1));}}sqlite3_finalize(q);q=NULL;
  if(!*starts){send_response(fd,404,"application/json","{\"error\":\"Pay period not found.\"}");return;}
  char subject[200],msg[1000];snprintf(subject,sizeof subject,"Timesheet review needs attention: %s",name);
  snprintf(msg,sizeof msg,"%s reported an issue with their attendance for %s to %s.\n\nEmployee note: %s\n\nReview under Timesheets > Punches and corrections.",name,starts,ends,note);
  if(!sql("BEGIN IMMEDIATE")){send_response(fd,503,"application/json","{\"error\":\"The clock is busy. Try again.\"}");return;}
  int sent=0,ok=1;sqlite3_stmt*m=NULL;
  if(sqlite3_prepare_v2(db,"SELECT DISTINCT e.email FROM employees e JOIN employee_roles r ON r.employee_id=e.id AND r.role='manager' WHERE e.active=1 AND e.email LIKE '%@%'",-1,&m,NULL)==SQLITE_OK){
   while(sqlite3_step(m)==SQLITE_ROW){const char*address=ar_text(m,0);if(strpbrk(address,"\r\n"))continue;
    sqlite3_stmt*ins=NULL;char key[110];snprintf(key,sizeof key,"review-issue:%d:%d:%d",p,e,sent);
    if(sqlite3_prepare_v2(db,"INSERT OR IGNORE INTO email_queue(recipient,subject,body,status,next_attempt,event_id) VALUES(?1,?2,?3,'pending',strftime('%Y-%m-%dT%H:%M:%fZ','now'),?4)",-1,&ins,NULL)==SQLITE_OK){sqlite3_bind_text(ins,1,address,-1,SQLITE_TRANSIENT);sqlite3_bind_text(ins,2,subject,-1,SQLITE_TRANSIENT);sqlite3_bind_text(ins,3,msg,-1,SQLITE_TRANSIENT);sqlite3_bind_text(ins,4,key,-1,SQLITE_TRANSIENT);if(sqlite3_step(ins)==SQLITE_DONE)sent++;else ok=0;}else ok=0;sqlite3_finalize(ins);if(!ok)break;
   }
  }else ok=0;sqlite3_finalize(m);
  if(!sent)ok=0;
  if(ok)ok=sql("COMMIT");else sql("ROLLBACK");
  send_response(fd,ok?200:503,"application/json",ok?"{\"status\":\"queued\"}":"{\"error\":\"Could not notify a manager. Contact your manager directly.\"}");return;
 }
 if(!strcmp(method,"POST")&&!strcmp(suffix,"/approve")){
  sqlite3_stmt*q=NULL;char starts[16]="",ends[16]="";int allowed=0;
  if(sqlite3_prepare_v2(db,"SELECT starts,ends FROM pay_periods WHERE id=?1 AND status='open' AND ends<=date('now','localtime')",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,p);if(sqlite3_step(q)==SQLITE_ROW){snprintf(starts,sizeof starts,"%s",ar_text(q,0));snprintf(ends,sizeof ends,"%s",ar_text(q,1));allowed=1;}}sqlite3_finalize(q);q=NULL;
  if(!allowed){send_response(fd,409,"application/json","{\"error\":\"This pay period is not ready for approval.\"}");return;}
  if(!sql("BEGIN IMMEDIATE")){send_response(fd,503,"application/json","{\"error\":\"Try again.\"}");return;}
  int ok=1,count=0,open=0,seen=0;long long secs=0;
  ok=rec_employee_totals(e,starts,ends,&count,&secs);
  if(ok&&sqlite3_prepare_v2(db,"SELECT state FROM effective_shift_pairs WHERE employee_id=?1 AND date(in_at,'localtime') BETWEEN ?2 AND ?3 ORDER BY in_at,in_id",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,e);sqlite3_bind_text(q,2,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,ends,-1,SQLITE_TRANSIENT);while(sqlite3_step(q)==SQLITE_ROW){const char*k=ar_text(q,0);seen=1;if(strcmp(k,"complete"))ok=0;}}else ok=0;
  sqlite3_finalize(q);q=NULL;if(open)ok=0;
  /* Leave-only employees can approve their own records without punches. */
  if(ok&&sqlite3_prepare_v2(db,"INSERT INTO period_signoffs(period_id,employee_id,role,signed_by,punch_count,approved_seconds,leave_fingerprint) VALUES(?1,?2,'employee',?2,?3,?4,?5) ON CONFLICT(period_id,employee_id,role) DO UPDATE SET punch_count=excluded.punch_count,approved_seconds=excluded.approved_seconds,leave_fingerprint=excluded.leave_fingerprint,signed_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE period_signoffs.punch_count<>excluded.punch_count OR period_signoffs.approved_seconds<>excluded.approved_seconds OR period_signoffs.leave_fingerprint<>excluded.leave_fingerprint",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,p);sqlite3_bind_int(q,2,e);sqlite3_bind_int(q,3,count);sqlite3_bind_int64(q,4,secs);char fp[17]="";ok=rec_leave_fingerprint(e,starts,ends,fp);if(ok){sqlite3_bind_text(q,5,fp,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}}else ok=0;
  sqlite3_finalize(q);(void)seen;
  if(ok)ok=sql("COMMIT");else sql("ROLLBACK");
  send_response(fd,ok?200:409,"application/json",ok?"{\"status\":\"approved\"}":"{\"error\":\"Resolve missing punches before approving.\"}");return;
 }
 send_response(fd,404,"text/plain","Not found");
}
/* Automatic reviews use completed, open pay periods and the existing review/email tables.
   One event per employee/period. Do not replace a valid manager-created link. */
static void review_auto_due(void){
 static time_t last_checked=0;
 time_t now=time(NULL);if(last_checked&&now-last_checked<60)return;last_checked=now;struct tm local;localtime_r(&now,&local);char date[11];strftime(date,sizeof date,"%Y-%m-%d",&local);
 sqlite3_stmt*q=NULL;int enabled=0,days=1;char base[270]="",send_time[8]="00:00";
 if(sqlite3_prepare_v2(db,"SELECT enabled,days_after,send_time FROM review_send_settings WHERE id=1",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW){enabled=sqlite3_column_int(q,0);days=sqlite3_column_int(q,1);snprintf(send_time,sizeof send_time,"%s",sqlite3_column_text(q,2));}sqlite3_finalize(q);
 if(!enabled)return;
 /* Disabling the editable employee template disables automatic review mail. */
 int template_on=0;
 if(sqlite3_prepare_v2(db,"SELECT enabled FROM email_templates WHERE id='auto_review_employee'",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW)template_on=sqlite3_column_int(q,0);
 sqlite3_finalize(q);q=NULL;
 if(!template_on)return;
 note_link_base(base,sizeof base);if(!base[0])return; /* retry when HTTPS hostname is configured */
 int system_manager=0;
 if(sqlite3_prepare_v2(db,"SELECT e.id FROM employees e JOIN employee_roles r ON r.employee_id=e.id AND r.role='manager' WHERE e.active=1 ORDER BY e.id LIMIT 1",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW){system_manager=sqlite3_column_int(q,0);}
 sqlite3_finalize(q);
 if(!system_manager)return;
 sqlite3_stmt*periods=NULL;
 if(sqlite3_prepare_v2(db,"SELECT id,starts,ends FROM pay_periods WHERE status='open' AND date(ends,'+'||?2||' days')<=?1 AND ends>=date(?1,'-14 days') ORDER BY ends",-1,&periods,NULL)!=SQLITE_OK)return;
 sqlite3_bind_text(periods,1,date,-1,SQLITE_TRANSIENT);sqlite3_bind_int(periods,2,days);
 while(sqlite3_step(periods)==SQLITE_ROW){
 int period=sqlite3_column_int(periods,0);char starts[16],ends[16];
 snprintf(starts,sizeof starts,"%s",ar_text(periods,1));snprintf(ends,sizeof ends,"%s",ar_text(periods,2));
 sqlite3_stmt *due_query=NULL;char due_date[11]="";
 if(sqlite3_prepare_v2(db,"SELECT date(?1,'+'||?2||' days')",-1,&due_query,NULL)==SQLITE_OK){sqlite3_bind_text(due_query,1,ends,-1,SQLITE_TRANSIENT);sqlite3_bind_int(due_query,2,days);if(sqlite3_step(due_query)==SQLITE_ROW)snprintf(due_date,sizeof due_date,"%s",sqlite3_column_text(due_query,0));}sqlite3_finalize(due_query);
 if(!*due_date||!send_schedule_due(&local,due_date,send_time))continue;
 sqlite3_stmt*employees=NULL;
 if(sqlite3_prepare_v2(db,"SELECT id,name,email,attendance_mode FROM employees WHERE active=1 AND auto_review_enabled=1 AND instr(coalesce(email,''),'@')>1 ORDER BY id",-1,&employees,NULL)!=SQLITE_OK)continue;
 int all_ok=1,queued_count=0;
 while(sqlite3_step(employees)==SQLITE_ROW){
  int employee=sqlite3_column_int(employees,0);char name[192],email[256],token[65],hash[256];snprintf(name,sizeof name,"%s",ar_text(employees,1));snprintf(email,sizeof email,"%s",ar_text(employees,2));
  if(strpbrk(email,"\r\n"))continue;
  int current_count=0;long long current_seconds=0;
  rec_employee_totals(employee,starts,ends,&current_count,&current_seconds);
  if(!strcmp(ar_text(employees,3),"vacation_only")){if(!lite_review_has_context(employee,starts,ends))continue;}else if(current_count<=0)continue;
  sqlite3_stmt*check=NULL; /* Read-only timesheets require no employee signoff. */
  char event[100];snprintf(event,sizeof event,"auto-review:%d:%d",period,employee);
  int exists=0;
  if(sqlite3_prepare_v2(db,"SELECT 1 FROM email_queue WHERE event_id=?1",-1,&check,NULL)==SQLITE_OK){sqlite3_bind_text(check,1,event,-1,SQLITE_TRANSIENT);exists=sqlite3_step(check)==SQLITE_ROW;}sqlite3_finalize(check);check=NULL;
  if(exists)continue;
  /* Do not revoke a previously issued manual or automatic link for this period. */
  int has_link=0;
  if(sqlite3_prepare_v2(db,"SELECT 1 FROM review_links WHERE employee_id=?1 AND period_id=?2 AND revoked=0 AND expires_at>CAST(strftime('%s','now') AS INTEGER)",-1,&check,NULL)==SQLITE_OK){sqlite3_bind_int(check,1,employee);sqlite3_bind_int(check,2,period);has_link=sqlite3_step(check)==SQLITE_ROW;}sqlite3_finalize(check);
  if(has_link)continue;
  if(!random_hex(token,32)||!review_hash(token,hash)){all_ok=0;break;}
  if(!sql("BEGIN IMMEDIATE")){all_ok=0;break;}
  int ok=sqlite3_prepare_v2(db,"INSERT INTO review_links(employee_id,period_id,token_hash,expires_at,created_by,created_at,revoked) VALUES(?1,?2,?3,CAST(strftime('%s','now') AS INTEGER)+1209600,?4,CAST(strftime('%s','now') AS INTEGER),0) ON CONFLICT(employee_id,period_id) DO UPDATE SET token_hash=excluded.token_hash,expires_at=excluded.expires_at,created_by=excluded.created_by,created_at=excluded.created_at,revoked=0",-1,&q,NULL)==SQLITE_OK;
  if(ok){sqlite3_bind_int(q,1,employee);sqlite3_bind_int(q,2,period);sqlite3_bind_text(q,3,hash,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,4,system_manager);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);q=NULL;
  char subject[200],message[1100],link[390],deadline[50]="",missing_note[150]="";
  snprintf(link,sizeof link,"%s/review/%s",base,token);snprintf(subject,sizeof subject,"Timeclock: Timesheet %s to %s",starts,ends);
  /* Next business day after the notification, in the clock's local calendar. */
  sqlite3_stmt *due=NULL;
  if(sqlite3_prepare_v2(db,"SELECT date(?1,'+1 day',CASE strftime('%w',date(?1,'+1 day')) WHEN '6' THEN '+2 days' WHEN '0' THEN '+1 day' ELSE '+0 days' END)",-1,&due,NULL)==SQLITE_OK){
    sqlite3_bind_text(due,1,date,-1,SQLITE_TRANSIENT);
    if(sqlite3_step(due)==SQLITE_ROW)snprintf(deadline,sizeof deadline,"%s",ar_text(due,0));
  }
  sqlite3_finalize(due);
  if(!deadline[0])snprintf(deadline,sizeof deadline,"%s",date);
  sqlite3_stmt*scan=NULL;int open_punch=0,problems=0;
  if(sqlite3_prepare_v2(db,"SELECT state FROM effective_shift_pairs WHERE employee_id=?1 AND date(in_at,'localtime') BETWEEN ?2 AND ?3 ORDER BY in_at,in_id",-1,&scan,NULL)==SQLITE_OK){
   sqlite3_bind_int(scan,1,employee);sqlite3_bind_text(scan,2,starts,-1,SQLITE_TRANSIENT);sqlite3_bind_text(scan,3,ends,-1,SQLITE_TRANSIENT);
   while(sqlite3_step(scan)==SQLITE_ROW){const char*k=ar_text(scan,0);if(!strcmp(k,"IN")){if(open_punch)problems++;open_punch=1;}else{if(!open_punch)problems++;open_punch=0;}}
  }sqlite3_finalize(scan);if(open_punch)problems++;
  if(problems)snprintf(missing_note,sizeof missing_note,"%d punch %s manager correction.\n",problems,problems==1?"problem needs":"problems need");
  snprintf(message,sizeof message,"%s\n\nHello %s,\nView the hours recorded for %s to %s.\n%s\nContact your manager if something is wrong.",link,name,starts,ends,missing_note);
  sqlite3_stmt*tpl=NULL;
  if(sqlite3_prepare_v2(db,"SELECT subject,body FROM email_templates WHERE id='auto_review_employee' AND enabled=1",-1,&tpl,NULL)==SQLITE_OK){
   if(sqlite3_step(tpl)==SQLITE_ROW){
    const struct email_field fields[]={{"{{employee_name}}",name},{"{{period_start}}",starts},{"{{period_end}}",ends},{"{{review_link}}",link},{"{{review_deadline}}",deadline},{"{{missing_punch_note}}",missing_note}};
    char*ts=email_render(ar_text(tpl,0),fields,6),*tb=email_render(ar_text(tpl,1),fields,6);
    if(ts&&tb){snprintf(subject,sizeof subject,"%s",ts);snprintf(message,sizeof message,"%s",tb);}
    free(ts);free(tb);
   }
  }sqlite3_finalize(tpl);

  if(ok)ok=sqlite3_prepare_v2(db,"INSERT OR IGNORE INTO email_queue(recipient,subject,body,status,next_attempt,event_id) VALUES(?1,?2,?3,'pending',strftime('%Y-%m-%dT%H:%M:%fZ','now'),?4)",-1,&q,NULL)==SQLITE_OK;
  if(ok){sqlite3_bind_text(q,1,email,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,2,subject,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,message,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,4,event,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);q=NULL;
  if(ok)ok=sqlite3_prepare_v2(db,"INSERT INTO review_link_audit(manager_id,employee_id,period_id,action) VALUES(?3,?1,?2,'automatically emailed')",-1,&q,NULL)==SQLITE_OK;
  if(ok){sqlite3_bind_int(q,1,employee);sqlite3_bind_int(q,2,period);sqlite3_bind_int(q,3,system_manager);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);q=NULL;
  if(ok)ok=sql("COMMIT");else sql("ROLLBACK");
  if(!ok){all_ok=0;break;}
  queued_count++;
 }
 sqlite3_finalize(employees);
 /* One summary per period, not a copy of each employee message. */
 if(all_ok&&queued_count>0){
  char payroll_email[260]="";
  sqlite3_stmt*n=NULL;
  if(sqlite3_prepare_v2(db,"SELECT recipient FROM payroll_settings WHERE id=1",-1,&n,NULL)==SQLITE_OK&&sqlite3_step(n)==SQLITE_ROW)
   snprintf(payroll_email,sizeof payroll_email,"%s",ar_text(n,0));
  sqlite3_finalize(n);
  if(strchr(payroll_email,'@')&&!strpbrk(payroll_email,"\r\n")){
   char count_str[32],event[90],subject[300],message[1200];
   snprintf(count_str,sizeof count_str,"%d",queued_count);
   snprintf(event,sizeof event,"auto-review-summary:%d",period);
   snprintf(subject,sizeof subject,"Timeclock: Pay reviews issued for %s to %s",starts,ends);
   snprintf(message,sizeof message,"Timesheet notices queued for period %s to %s.\nEmployees emailed: %s\nCheck the email queue for delivery failures.",starts,ends,count_str);
   sqlite3_stmt*tpl=NULL;
   if(sqlite3_prepare_v2(db,"SELECT subject,body FROM email_templates WHERE id='review_dispatch_payroll' AND enabled=1",-1,&tpl,NULL)==SQLITE_OK){
    if(sqlite3_step(tpl)==SQLITE_ROW){
     const struct email_field fields[]={{"{{payday}}",""},{"{{period_start}}",starts},{"{{period_end}}",ends},{"{{sent_count}}",count_str}};
     char*ts=email_render(ar_text(tpl,0),fields,4),*tb=email_render(ar_text(tpl,1),fields,4);
     if(ts&&tb){snprintf(subject,sizeof subject,"%s",ts);snprintf(message,sizeof message,"%s",tb);}
     free(ts);free(tb);
    }else payroll_email[0]=0;
   }sqlite3_finalize(tpl);
   if(payroll_email[0]&&sqlite3_prepare_v2(db,"INSERT OR IGNORE INTO email_queue(recipient,subject,body,status,next_attempt,event_id) VALUES(?1,?2,?3,'pending',strftime('%Y-%m-%dT%H:%M:%fZ','now'),?4)",-1,&n,NULL)==SQLITE_OK){
    sqlite3_bind_text(n,1,payroll_email,-1,SQLITE_TRANSIENT);sqlite3_bind_text(n,2,subject,-1,SQLITE_TRANSIENT);
    sqlite3_bind_text(n,3,message,-1,SQLITE_TRANSIENT);sqlite3_bind_text(n,4,event,-1,SQLITE_TRANSIENT);
    sqlite3_step(n);
   }sqlite3_finalize(n);
  }
 }

 } /* next eligible pay period */
 sqlite3_finalize(periods);
}
static void review_auto_settings(int fd,const char*body){
 if(!body){sqlite3_stmt*q=NULL;int enabled=1,days=1;char send_time[8]="00:00";if(sqlite3_prepare_v2(db,"SELECT enabled,days_after,send_time FROM review_send_settings WHERE id=1",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW){enabled=sqlite3_column_int(q,0);days=sqlite3_column_int(q,1);snprintf(send_time,sizeof send_time,"%s",sqlite3_column_text(q,2));}sqlite3_finalize(q);char b[150];snprintf(b,sizeof b,"{\"enabled\":%s,\"days\":%d,\"send_time\":\"%s\"}",enabled?"true":"false",days,send_time);send_response(fd,200,"application/json",b);return;}
 char en[8]="",ds[8]="",send_time[8]="00:00";if(!form_value(body,"enabled",en,sizeof en)||!form_value(body,"days",ds,sizeof ds)|| (strcmp(en,"0")&&strcmp(en,"1"))){send_response(fd,400,"application/json","{\"error\":\"Check the review settings.\"}");return;}
 if((form_has(body,"send_time")&&!form_value(body,"send_time",send_time,sizeof send_time))||!send_time_valid(send_time)){send_response(fd,400,"application/json","{\"error\":\"Choose a valid send time.\"}");return;}
 char*end=NULL;long days=strtol(ds,&end,10);if(!*ds||*end||days<0||days>7){send_response(fd,400,"application/json","{\"error\":\"Choose 0 to 7 days after a period ends.\"}");return;}
 sqlite3_stmt*q=NULL;int ok=sqlite3_prepare_v2(db,"UPDATE review_send_settings SET enabled=?1,days_after=?2,send_time=?3 WHERE id=1",-1,&q,NULL)==SQLITE_OK;if(ok){sqlite3_bind_int(q,1,en[0]=='1');sqlite3_bind_int(q,2,(int)days);sqlite3_bind_text(q,3,send_time,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}sqlite3_finalize(q);send_response(fd,ok?200:500,"application/json",ok?"{\"status\":\"saved\"}":"{\"error\":\"Could not save review settings.\"}");
}
static void review_links_page(int fd){send_response(fd,200,"text/html; charset=utf-8",UI_HEAD("Timesheet links","review-links")
 "<h1>Timesheet links</h1><section><h2>Automatic timesheet emails</h2><label><input type='checkbox' id='autoEnabled' checked> Automatically email timesheet links</label><label>Send timesheet links <input type='number' min='0' max='7' id='autoDays' value='1'> days after a period ends</label><label>Send at (clock local time)<input type='time' id='autoTime' value='00:00' required></label><p class='muted'>Automatic links queue at or after this local time; missed runs catch up. Create and email sends immediately.</p><div class='actions'><button id='autoSave'>Save settings</button></div><p id='autoStatus' role='status'></p></section><p>Each link lets one employee view their recorded timesheet. Links expire after 14 days. Creating a new link cancels the earlier one.</p><section><label>Employee <select id='employee'></select></label><p id='empflag' role='status'></p><label>Pay period <select id='period'></select></label><div class='actions'><button id='generate'>Create link</button><button id='send'>Create and email</button><button class='secondary' id='revoke'>Revoke link</button></div><p id='message' role='status'></p><label>Timesheet link<input id='linkout' readonly></label><div class='actions'><button class='secondary' id='copy' disabled>Copy link</button></div><p class='muted'>Anyone with the link can view this timesheet. Send it only to the employee. Copy it now; it cannot be shown again.</p></section>"
 "<script>const e=document.getElementById('employee'),p=document.getElementById('period'),msg=document.getElementById('message'),res=document.getElementById('linkout');let emps=[];const call=tc.api;"
 "function showEmp(){res.value='';copy.disabled=true;let x=emps.find(v=>String(v.id)===e.value),f=document.getElementById('empflag');f.replaceChildren();if(x&&!x.email)f.append(tc.flag('No email','check'),' Create and email is not available. Add an email under Employees, or send the link another way.')}"
 "async function load(){let cfg=await call('/api/v1/admin/review/auto-settings');document.getElementById('autoEnabled').checked=cfg.enabled;document.getElementById('autoDays').value=cfg.days;document.getElementById('autoTime').value=cfg.send_time||'00:00';let d=await call('/api/v1/admin/review/choices');emps=d.employees;for(let x of d.employees){let o=document.createElement('option');o.value=x.id;o.textContent=x.name+(x.email?' ('+x.email+')':' (no email)');e.append(o)}for(let x of d.periods){let o=document.createElement('option');o.value=x.id;o.textContent=x.label;p.append(o)}showEmp()}e.onchange=showEmp;p.onchange=()=>{res.value='';copy.disabled=true;};"
 "async function issue(sendNow){try{if(!confirm('Create a new link? Any earlier link for this employee and period stops working.'))return;let j=await call('/api/v1/admin/review/generate',{method:'POST',body:new URLSearchParams({employee:e.value,period:p.value,send:sendNow?'1':'0'})});let h=await call('/api/v1/admin/note-link');if(!h.effective)throw Error('No hostname is set for links. Install a certificate under HTTPS certificate.');res.value=h.effective+j.path;copy.disabled=false;tc.ok(msg,sendNow?'Link created and email queued. Expires in 14 days.':'Link created. Expires in 14 days.')}catch(x){tc.err(msg,x)}}"
 "document.getElementById('autoSave').onclick=async()=>{let st=document.getElementById('autoStatus');try{await call('/api/v1/admin/review/auto-settings',{method:'POST',body:new URLSearchParams({enabled:document.getElementById('autoEnabled').checked?'1':'0',days:document.getElementById('autoDays').value,send_time:document.getElementById('autoTime').value})});tc.ok(st,'Automatic review settings saved.')}catch(e){tc.err(st,e)}};generate.onclick=()=>issue(false);document.getElementById('send').onclick=()=>issue(true);revoke.onclick=async()=>{try{if(!confirm('Revoke the link for this employee and period?'))return;await call('/api/v1/admin/review/revoke',{method:'POST',body:new URLSearchParams({employee:e.value,period:p.value})});res.value='';copy.disabled=true;tc.ok(msg,'Link revoked.')}catch(x){tc.err(msg,x)}};copy.onclick=async()=>{if(!res.value){copy.disabled=true;return;}try{await navigator.clipboard.writeText(res.value);tc.ok(msg,'Link copied.')}catch(x){tc.err(msg,'Copy failed. Select the link and copy it manually.')}};load().catch(x=>tc.err(msg,x))</script>" UI_FOOT);}
