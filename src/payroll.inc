/* Payroll run configuration, authorized through the existing RFID manager scan. */
static void payroll_get(int fd){
 sqlite3_stmt*q=NULL;char data[700];
 snprintf(data,sizeof data,"{\"cadence\":\"semi_monthly\",\"anchor\":\"\",\"lead\":3,\"recipient\":\"\",\"enabled\":false,\"send_time\":\"00:00\"}");
 if(sqlite3_prepare_v2(db,"SELECT pc.cycle,coalesce(ps.anchor_payday,''),coalesce(ps.lead_days,3),coalesce(ps.recipient,''),coalesce(ps.enabled,0),coalesce(ps.send_time,'00:00') FROM period_cycle_settings pc LEFT JOIN payroll_settings ps ON ps.id=pc.id WHERE pc.id=1",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW){
  sqlite3_str*o=sqlite3_str_new(db);sqlite3_str_appendall(o,"{\"cadence\":");schedule_json_text(o,(const char*)sqlite3_column_text(q,0));sqlite3_str_appendall(o,",\"anchor\":");schedule_json_text(o,(const char*)sqlite3_column_text(q,1));sqlite3_str_appendf(o,",\"lead\":%d,\"recipient\":",sqlite3_column_int(q,2));schedule_json_text(o,(const char*)sqlite3_column_text(q,3));sqlite3_str_appendf(o,",\"enabled\":%s,\"send_time\":",sqlite3_column_int(q,4)?"true":"false");schedule_json_text(o,(const char*)sqlite3_column_text(q,5));sqlite3_str_appendchar(o,1,'}');char *v=sqlite3_str_finish(o);if(v){snprintf(data,sizeof data,"%s",v);sqlite3_free(v);}
 }sqlite3_finalize(q);
 /* Historical unsent periods are intentionally outside automatic catch-up. */
 long older=0;sqlite3_stmt*oldq=NULL; if(sqlite3_prepare_v2(db,"SELECT count(*) FROM pay_periods p WHERE p.status='closed' AND p.ends<date('now','localtime','-45 days') AND p.ends>=coalesce((SELECT nullif(enabled_since,'') FROM payroll_settings WHERE id=1),'9999-12-31') AND NOT EXISTS (SELECT 1 FROM email_queue e WHERE e.event_id='payroll-period:'||p.id AND e.status='sent')",-1,&oldq,NULL)==SQLITE_OK&&sqlite3_step(oldq)==SQLITE_ROW)older=sqlite3_column_int64(oldq,0);sqlite3_finalize(oldq);
 char reply[850];size_t n=strlen(data);
 if(n>0&&data[n-1]=='}'){data[n-1]=0;snprintf(reply,sizeof reply,"%s,\"older_unsent\":%ld}",data,older);}else snprintf(reply,sizeof reply,"%s",data);
 send_response(fd,200,"application/json",reply);
}
static time_t payroll_day(const char*s);
static int payroll_apply(const char*b){
 char anchor[20]="",before[12]="",recipient[260]="",enabled[8]="",send_time[8]="00:00";
 if(!form_value(b,"anchor",anchor,sizeof anchor)||!form_value(b,"before",before,sizeof before)||!form_value(b,"recipient",recipient,sizeof recipient)||!form_value(b,"enabled",enabled,sizeof enabled))return 0;
 if(payroll_day(anchor)==(time_t)-1||!*before||strspn(before,"0123456789")!=strlen(before))return 0;
 if((form_has(b,"send_time")&&!form_value(b,"send_time",send_time,sizeof send_time))||!send_time_valid(send_time))return 0;
 int lead=atoi(before);if(lead<0||lead>14)return 0;
 if(strlen(recipient)>250||!strchr(recipient,'@')||strpbrk(recipient,"\r\n\t "))return 0;
 if(strcmp(enabled,"0")&&strcmp(enabled,"1"))return 0;
 sqlite3_stmt*q=NULL;
 int valid_cycle=0;
 if(sqlite3_prepare_v2(db,"SELECT cycle FROM period_cycle_settings WHERE id=1",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW){
  const char *cycle=(const char*)sqlite3_column_text(q,0);
  valid_cycle=cycle&&(strcmp(cycle,"semi_monthly")||atoi(anchor+8)==1||atoi(anchor+8)==15);
 }sqlite3_finalize(q);q=NULL;if(!valid_cycle)return 0;
 int ok=sqlite3_prepare_v2(db,"INSERT INTO payroll_settings(id,anchor_payday,lead_days,recipient,enabled,send_time,enabled_since) VALUES(1,?1,?2,?3,?4,?5,CASE WHEN ?4=1 THEN date('now','localtime') ELSE '' END) ON CONFLICT(id) DO UPDATE SET anchor_payday=excluded.anchor_payday,lead_days=excluded.lead_days,recipient=excluded.recipient,enabled=excluded.enabled,send_time=excluded.send_time,enabled_since=CASE WHEN payroll_settings.enabled=0 AND excluded.enabled=1 THEN date('now','localtime') ELSE payroll_settings.enabled_since END",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,anchor,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,2,lead);sqlite3_bind_text(q,3,recipient,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,4,atoi(enabled));sqlite3_bind_text(q,5,send_time,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}
 sqlite3_finalize(q);return ok;
}
static void payroll_arm(int fd,int who,const char*b){
 if(!b||strlen(b)>500){send_response(fd,400,"application/json","{\"error\":\"Check the payday, days before payday and email address.\"}");return;}
 if(!sql("BEGIN IMMEDIATE")){send_response(fd,503,"application/json","{\"error\":\"The clock is busy. Try again.\"}");return;}
 if(authorization_pending()){sql("ROLLBACK");send_response(fd,409,"application/json","{\"error\":\"The card reader is waiting for another approval. Try again in 30 seconds.\"}");return;}
 if(!sql("SAVEPOINT validate_payroll")){sql("ROLLBACK");send_response(fd,500,"application/json","{\"error\":\"The clock could not complete this. Try again.\"}");return;}
 int valid_payroll=payroll_apply(b);
 int validation_reset=sql("ROLLBACK TO validate_payroll")&&sql("RELEASE validate_payroll");
 if(!valid_payroll||!validation_reset){sql("ROLLBACK");send_response(fd,400,"application/json","{\"error\":\"Check the payday, days before payday and email address.\"}");return;}
 sqlite3_stmt*q=NULL;int ok=1;
 if(sqlite3_prepare_v2(db,"INSERT INTO payroll_authorization(id,requested_by,change_body,armed_at) VALUES(1,?1,?2,CAST(strftime('%s','now') AS INTEGER)) ON CONFLICT(id) DO UPDATE SET requested_by=excluded.requested_by,change_body=excluded.change_body,armed_at=excluded.armed_at",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_int(q,1,who);sqlite3_bind_text(q,2,b,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_DONE;}else ok=0;sqlite3_finalize(q);
 char token[33]="";
 if(ok)ok=sql("UPDATE manager_scan SET armed_at=CAST(strftime('%s','now') AS INTEGER),uid='',scanned_at=0,scan_mode='approval' WHERE id=1");
 if(ok)ok=approval_token_new(token);
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");
 char reply[160];snprintf(reply,sizeof reply,"{\"status\":\"waiting\",\"token\":\"%s\"}",token);
 send_response(fd,ok?200:500,"application/json",ok?reply:"{\"error\":\"The clock could not complete this. Try again.\"}");
}
static void payroll_status(int fd,int who){
 sqlite3_stmt*q=NULL;int owner=0;long long armed=0,scanned=0;char body[550]="",uid[40]="";
 if(sqlite3_prepare_v2(db,"SELECT a.requested_by,a.change_body,a.armed_at,s.uid,s.scanned_at FROM payroll_authorization a JOIN manager_scan s ON s.id=1 WHERE a.id=1",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW){owner=sqlite3_column_int(q,0);const char*b=(const char*)sqlite3_column_text(q,1),*u=(const char*)sqlite3_column_text(q,3);if(b)snprintf(body,sizeof body,"%s",b);if(u)snprintf(uid,sizeof uid,"%s",u);armed=sqlite3_column_int64(q,2);scanned=sqlite3_column_int64(q,4);}sqlite3_finalize(q);
 if(!armed||owner!=who){send_response(fd,409,"application/json","{\"error\":\"This approval is no longer valid. Start again.\"}");return;}
 if((long long)time(NULL)-armed>30){sql("DELETE FROM payroll_authorization WHERE id=1");send_response(fd,200,"application/json","{\"status\":\"expired\"}");return;}
 if(!*uid||scanned<armed){send_response(fd,200,"application/json","{\"status\":\"waiting\"}");return;}
 int authorized=0;if(sqlite3_prepare_v2(db,"SELECT 1 FROM manager_cards mc JOIN employees e ON e.id=mc.manager_id JOIN employee_roles r ON r.employee_id=e.id AND r.role='manager' JOIN user_accounts ua ON ua.employee_id=e.id WHERE mc.uid=?1 COLLATE NOCASE AND mc.active=1 AND e.active=1 AND ua.disabled=0",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,uid,-1,SQLITE_TRANSIENT);authorized=sqlite3_step(q)==SQLITE_ROW;}sqlite3_finalize(q);
 if(!authorized){sql("UPDATE manager_scan SET uid='',scanned_at=0 WHERE id=1");send_response(fd,200,"application/json","{\"status\":\"waiting\",\"message\":\"manager card required\"}");return;}
 if(!sql("BEGIN IMMEDIATE")){send_response(fd,503,"application/json","{\"error\":\"The clock is busy. Try again.\"}");return;}
 int ok=payroll_apply(body);if(ok)ok=sql("DELETE FROM payroll_authorization WHERE id=1")&&sql("UPDATE manager_scan SET armed_at=0,uid='',scanned_at=0 WHERE id=1");
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");
 send_response(fd,ok?200:400,"application/json",ok?"{\"status\":\"saved\"}":"{\"error\":\"Check the payday, days before payday and email address.\"}");
}
static void payroll_page(int fd){
 static const char page[]=UI_HEAD("Payroll report","payroll")"<h1>Payroll report</h1><p id='paybanner' role='alert' hidden></p><section><form id='f'><p>Pay frequency: <strong id='frequency'></strong>. Change it under <a href='/pay-periods'>Pay periods</a>.</p><label>Payday anchor<input required type='date' name='anchor'><small>For twice-monthly paydays, use the 1st (1st and 15th) or 15th (15th and month-end).</small></label><label>Send this many days before payday<input required type='number' min='0' max='14' name='before'></label><label>Send at (clock local time)<input required type='time' name='send_time' value='00:00'></label><label>Send to<input required type='email' name='recipient'></label><label><input type='checkbox' name='enabled' value='1'> Send closed-period reports automatically</label><div class='actions'><button type='submit'>Save (manager card required)</button></div><p id='status' role='status'></p></form><p class='muted'>Pay periods are created automatically. Only closed periods can be emailed. Automatic reports queue at or after the selected local time; missed runs catch up. Send report sends immediately. See <a href='/email-queue'>Email queue</a> for delivery status.</p></section><script>let f=document.getElementById('f'),status=document.getElementById('status');const api=tc.api;async function load(){let j=await api('/api/v1/admin/payroll');document.getElementById('frequency').textContent=({weekly:'Weekly',biweekly:'Every two weeks',semi_monthly:'Twice monthly',monthly:'Monthly'})[j.cadence]||j.cadence;f.elements.anchor.value=j.anchor;f.elements.before.value=j.lead;f.elements.send_time.value=j.send_time||'00:00';f.elements.recipient.value=j.recipient;f.elements.enabled.checked=j.enabled;let smtp={};try{smtp=await api('/api/v1/admin/smtp')}catch(e){}tc.banner(document.getElementById('paybanner'),'problem','Problem',j.enabled&&!(smtp.url&&smtp.sender)?'No mail server is set. Set one under Outgoing email.':'')}f.onsubmit=async e=>{e.preventDefault();let p=new URLSearchParams(new FormData(f));p.set('enabled',f.elements.enabled.checked?'1':'0');status.textContent=tc.CARD;try{const a=await api('/api/v1/admin/payroll/arm',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p});for(let i=0;i<45;i++){await new Promise(r=>setTimeout(r,700));let x=await api('/api/v1/admin/payroll/status',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams({token:a.token})});if(x.status==='saved'){await load();tc.ok(status,'Payroll delivery saved.');return}if(x.status==='expired')throw Error(tc.TIMEOUT)}throw Error(tc.TIMEOUT)}catch(e){tc.err(status,e)}};load().catch(e=>tc.err(status,e))</script>"UI_FOOT;
 send_response(fd,200,"text/html; charset=utf-8",page);
}
