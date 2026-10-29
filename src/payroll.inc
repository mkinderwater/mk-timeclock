/* Payroll run configuration, authorized through the existing RFID manager scan. */
static void payroll_get(int fd){
 struct payroll_config c;if(payroll_config_load(db,&c)<0){send_response(fd,500,"application/json","{\"error\":\"Cannot read report settings.\"}");return;}
 char data[700]="";
 sqlite3_str *o=sqlite3_str_new(db);
 sqlite3_str_appendall(o,"{\"cadence\":");json_text(o,c.cadence);
 sqlite3_str_appendall(o,",\"anchor\":");json_text(o,c.anchor);
 sqlite3_str_appendf(o,",\"lead\":%d,\"recipient\":",c.lead);json_text(o,c.recipient);
 sqlite3_str_appendf(o,",\"enabled\":%s,\"send_time\":",c.enabled?"true":"false");json_text(o,c.send_time);
 sqlite3_str_appendf(o,",\"previous_business_day\":%s}",c.previous_business_day?"true":"false");
 char *v=sqlite3_str_finish(o);if(v){snprintf(data,sizeof data,"%s",v);sqlite3_free(v);}
 /* Historical unsent periods are intentionally outside automatic catch-up. */
 long older=0;sqlite3_stmt*oldq=NULL; if(sqlite3_prepare_v2(db,"SELECT count(*) FROM pay_periods p WHERE p.status='closed' AND p.ends<date('now','localtime','-45 days') AND p.ends>=coalesce((SELECT nullif(enabled_since,'') FROM payroll_settings WHERE id=1),'9999-12-31') AND NOT EXISTS (SELECT 1 FROM email_queue e WHERE e.event_id='payroll-period:'||p.id AND e.status='sent')",-1,&oldq,NULL)==SQLITE_OK&&sqlite3_step(oldq)==SQLITE_ROW)older=sqlite3_column_int64(oldq,0);sqlite3_finalize(oldq);
 char reply[850];size_t n=strlen(data);
 if(n>0&&data[n-1]=='}'){data[n-1]=0;snprintf(reply,sizeof reply,"%s,\"older_unsent\":%ld}",data,older);}else snprintf(reply,sizeof reply,"%s",data);
 send_response(fd,200,"application/json",reply);
}
static time_t payroll_day(const char*s);
static int payroll_apply(const char*b){
 char anchor[20]="",before[12]="",recipient[260]="",enabled[8]="",send_time[8]="00:00",business[8]="";
 if(!form_value(b,"anchor",anchor,sizeof anchor)||!form_value(b,"before",before,sizeof before)||!form_value(b,"recipient",recipient,sizeof recipient)||!form_value(b,"enabled",enabled,sizeof enabled))return 0;
 if(form_has(b,"previous_business_day")&&!form_value(b,"previous_business_day",business,sizeof business))return 0;
 if(!*business)strcpy(business,"0");
 if(strcmp(business,"0")&&strcmp(business,"1"))return 0;
 if(payroll_day(anchor)==(time_t)-1||!*before||strspn(before,"0123456789")!=strlen(before))return 0;
 if((form_has(b,"send_time")&&!form_value(b,"send_time",send_time,sizeof send_time))||!time_valid(send_time))return 0;
 int lead=atoi(before);if(lead<0||lead>14)return 0;
 if(!email_address_ok(recipient))return 0;
 if(strcmp(enabled,"0")&&strcmp(enabled,"1"))return 0;
 sqlite3_stmt*q=NULL;
 int valid_cycle=0;
 if(sqlite3_prepare_v2(db,"SELECT cycle FROM period_cycle_settings WHERE id=1",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW){
  const char *cycle=(const char*)sqlite3_column_text(q,0);
  valid_cycle=cycle&&(strcmp(cycle,"semi_monthly")||atoi(anchor+8)==1||atoi(anchor+8)==15);
 }sqlite3_finalize(q);q=NULL;if(!valid_cycle)return 0;
 int ok=sqlite3_prepare_v2(db,"INSERT INTO payroll_settings(id,anchor_payday,lead_days,recipient,enabled,send_time,previous_business_day,enabled_since) VALUES(1,?1,?2,?3,?4,?5,?6,CASE WHEN ?4=1 THEN date('now','localtime') ELSE '' END) ON CONFLICT(id) DO UPDATE SET anchor_payday=excluded.anchor_payday,lead_days=excluded.lead_days,recipient=excluded.recipient,enabled=excluded.enabled,send_time=excluded.send_time,previous_business_day=excluded.previous_business_day,enabled_since=CASE WHEN payroll_settings.enabled=0 AND excluded.enabled=1 THEN date('now','localtime') ELSE payroll_settings.enabled_since END",-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_text(q,1,anchor,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,2,lead);sqlite3_bind_text(q,3,recipient,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,4,atoi(enabled));sqlite3_bind_text(q,5,send_time,-1,SQLITE_TRANSIENT);sqlite3_bind_int(q,6,atoi(business));ok=sqlite3_step(q)==SQLITE_DONE;}
 sqlite3_finalize(q);return ok;
}
static void payroll_arm(int fd,int who,const char*b){
 if(!b||strlen(b)>500){send_response(fd,400,"application/json","{\"error\":\"Check the payday, days before payday and email address.\"}");return;}
 if(!approval_begin(fd))return;
 if(!sql("SAVEPOINT validate_payroll")){sql("ROLLBACK");reply_failed(fd);return;}
 int valid_payroll=payroll_apply(b);
 int validation_reset=sql("ROLLBACK TO validate_payroll")&&sql("RELEASE validate_payroll");
 if(!valid_payroll||!validation_reset){sql("ROLLBACK");send_response(fd,400,"application/json","{\"error\":\"Check the payday, days before payday and email address.\"}");return;}
 approval_finish(fd,approval_save("payroll_authorization",who,b));
}
static void payroll_status(int fd,int who){
 sqlite3_stmt*q=NULL;int owner=0;long long armed=0,scanned=0;char body[550]="",uid[40]="";
 if(sqlite3_prepare_v2(db,"SELECT a.requested_by,a.change_body,a.armed_at,s.uid,s.scanned_at FROM payroll_authorization a JOIN manager_scan s ON s.id=1 WHERE a.id=1",-1,&q,NULL)==SQLITE_OK&&sqlite3_step(q)==SQLITE_ROW){owner=sqlite3_column_int(q,0);const char*b=(const char*)sqlite3_column_text(q,1),*u=(const char*)sqlite3_column_text(q,3);if(b)snprintf(body,sizeof body,"%s",b);if(u)snprintf(uid,sizeof uid,"%s",u);armed=sqlite3_column_int64(q,2);scanned=sqlite3_column_int64(q,4);}sqlite3_finalize(q);
 if(!armed||owner!=who){send_response(fd,409,"application/json","{\"error\":\"This approval is no longer valid. Start again.\"}");return;}
 if((long long)time(NULL)-armed>30){sql("DELETE FROM payroll_authorization WHERE id=1");send_response(fd,200,"application/json","{\"status\":\"expired\"}");return;}
 if(!*uid||scanned<armed){send_response(fd,200,"application/json","{\"status\":\"waiting\"}");return;}
 int authorized=0;if(sqlite3_prepare_v2(db,"SELECT 1 FROM manager_cards mc JOIN employees e ON e.id=mc.manager_id JOIN employee_roles r ON r.employee_id=e.id AND r.role='manager' JOIN user_accounts ua ON ua.employee_id=e.id WHERE mc.uid=?1 COLLATE NOCASE AND mc.active=1 AND e.active=1 AND ua.disabled=0",-1,&q,NULL)==SQLITE_OK){sqlite3_bind_text(q,1,uid,-1,SQLITE_TRANSIENT);authorized=sqlite3_step(q)==SQLITE_ROW;}sqlite3_finalize(q);
 if(!authorized){sql("UPDATE manager_scan SET uid='',scanned_at=0 WHERE id=1");send_response(fd,200,"application/json","{\"status\":\"waiting\",\"message\":\"manager card required\"}");return;}
 if(!begin_change(fd))return;
 int ok=payroll_apply(body);if(ok)ok=sql("DELETE FROM payroll_authorization WHERE id=1")&&sql("UPDATE manager_scan SET armed_at=0,uid='',scanned_at=0 WHERE id=1");
 if(ok)ok=sql("COMMIT");else sql("ROLLBACK");
 send_response(fd,ok?200:400,"application/json",ok?"{\"status\":\"saved\"}":"{\"error\":\"Check the payday, days before payday and email address.\"}");
}
static void payroll_page(int fd){
 static const char page[]=UI_HEAD("Report delivery","payroll")"<h1>Report delivery</h1><p id='paybanner' role='alert' hidden></p><section><form id='f'><p>Pay frequency: <strong id='frequency'></strong>. Change it under <a href='/pay-periods'>Pay periods</a>.</p><label>Payday anchor<input required type='date' name='anchor'><small>For twice-monthly paydays, use the 1st (1st and 15th) or 15th (15th and month-end).</small></label><label>Send this many days before payday<input required type='number' min='0' max='14' name='before'></label><label><input type='checkbox' name='previous_business_day' value='1'> Send payroll reports on the preceding business day when the scheduled report date falls on a weekend or statutory holiday</label><small>Uses dates under Stat holidays. Closed periods only; missed scheduled sends catch up after closure.</small><label>Send at (clock local time)<input required type='time' name='send_time' value='00:00'></label><label>Send to<input required type='email' name='recipient'></label><label><input type='checkbox' name='enabled' value='1'> Send closed-period reports automatically</label><div class='actions'><button type='submit'>Save (manager card required)</button></div><p id='status' role='status'></p></form><section><h2>Preview attendance report</h2><p>Download the current Excel report for any pay period. Open periods show attendance recorded so far, including exceptions. Preview does not close a period, save a revision, or send email.</p><label>Pay period<select id='previewPeriod'></select></label><div class='actions'><button class='secondary' id='previewExcel' type='button'>Preview Excel report</button></div><p id='previewStatus' role='status'></p></section><p class='muted'>Pay periods are created automatically. Attendance periods close automatically when they end, or can be closed earlier by a manager. Reports include unresolved review and punch exceptions. Only closed periods can be emailed. Automatic reports queue at or after the selected local time; missed runs catch up. Send report sends immediately. See <a href='/email-queue'>Email queue</a> for delivery status.</p></section><script>let f=document.getElementById('f'),status=document.getElementById('status');const api=tc.api;async function load(){let j=await api('/api/v1/admin/payroll');document.getElementById('frequency').textContent=({weekly:'Weekly',biweekly:'Every two weeks',semi_monthly:'Twice monthly',monthly:'Monthly'})[j.cadence]||j.cadence;f.elements.anchor.value=j.anchor;f.elements.before.value=j.lead;f.elements.send_time.value=j.send_time||'00:00';f.elements.recipient.value=j.recipient;f.elements.enabled.checked=j.enabled;f.elements.previous_business_day.checked=!!j.previous_business_day;let smtp={};try{smtp=await api('/api/v1/admin/smtp')}catch(e){}tc.banner(document.getElementById('paybanner'),'problem','Problem',j.enabled&&!(smtp.url&&smtp.sender)?'No mail server is set. Set one under Outgoing email.':'')}f.onsubmit=async e=>{e.preventDefault();let p=new URLSearchParams(new FormData(f));p.set('enabled',f.elements.enabled.checked?'1':'0');p.set('previous_business_day',f.elements.previous_business_day.checked?'1':'0');status.textContent=tc.CARD;try{const a=await api('/api/v1/admin/payroll/arm',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:p});await tc.card('/api/v1/admin/payroll/status',a.token);await load();tc.ok(status,'Payroll delivery saved.')}catch(e){tc.err(status,e)}};async function previewLoad(){try{let j=await api('/api/v1/admin/periods'),sel=document.getElementById('previewPeriod');sel.replaceChildren();let today=tc.date(new Date()),current=null;for(let p of j.periods){let o=document.createElement('option');o.value=p.id;o.textContent=p.starts+' to '+p.ends+(p.status==='open'?' (open)':' (closed)');sel.append(o);if(p.starts<=today&&today<=p.ends)current=p.id}if(current!==null)sel.value=String(current);document.getElementById('previewExcel').disabled=!sel.value;if(!sel.value)document.getElementById('previewStatus').textContent='No pay periods available.'}catch(e){tc.err(document.getElementById('previewStatus'),e)}}document.getElementById('previewExcel').onclick=async()=>{let id=document.getElementById('previewPeriod').value,ps=document.getElementById('previewStatus'),btn=document.getElementById('previewExcel');if(!id)return;btn.disabled=true;ps.textContent='Preparing preview...';try{let res=await fetch('/api/v1/admin/periods/preview.xlsx?period_id='+encodeURIComponent(id),{credentials:'same-origin'});if(!res.ok){let msg='Preview failed ('+res.status+').';let ct=res.headers.get('content-type')||'';if(ct.includes('json')){let j=await res.json();msg=j.error||j.message||msg}else if(res.status===401)msg='Login required. Please sign in again.';else if(res.status===409)msg='Manager card setup required.';throw Error(msg)}let blob=await res.blob(),url=URL.createObjectURL(blob),a=document.createElement('a');a.href=url;let disposition=res.headers.get('content-disposition')||'',match=disposition.match(/filename=\"?([^\";]+)\"?/i);a.download=match?match[1]:'Timesheet_PREVIEW.xlsx';document.body.append(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(url),30000);tc.ok(ps,'Preview downloaded. No report sent.')}catch(e){tc.err(ps,e)}finally{btn.disabled=false}};load().catch(e=>tc.err(status,e));previewLoad()</script>"UI_FOOT;
 send_response(fd,200,"text/html; charset=utf-8",page);
}
