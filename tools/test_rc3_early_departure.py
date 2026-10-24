"""Live early departure boundaries, overnight schedules, notes and exact duration."""
import datetime,json,os,re,shlex,socket,sqlite3,subprocess,tempfile,time,urllib.request,urllib.parse,urllib.error
from pathlib import Path
from zoneinfo import ZoneInfo
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
 db=Path(tmp)/'clock.db'
 with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
 driver=Path(tmp)/'departure-driver.c';binary=Path(tmp)/'departure-driver'
 driver.write_text('#define main clock_server_main\n#include "'+str(root/'src/server.c')+'"\n#undef main\nint main(int argc,char **argv){if(argc!=3||!init_db(argv[1]))return 1;attendance_departures_due((time_t)atoll(argv[2]));sqlite3_close(db);return 0;}\n')
 subprocess.run(['cc','-O2','-std=c11','-D_POSIX_C_SOURCE=200809L',*shlex.split(os.environ.get('CFLAGS','')),'-o',str(binary),str(driver),'-lsqlite3','-lcrypt'],check=True)
 def finalize(when):subprocess.run([str(binary),str(db),str(int(when))],check=True,env={**os.environ,'TZ':'America/Edmonton'})
 proc=subprocess.Popen([str(root/'timeclock-server'),str(db),str(port)],env={**os.environ,'TZ':'America/Edmonton','DEVICE_KEY':'fixture-device-key'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
 try:
  base=f'http://127.0.0.1:{port}'
  for _ in range(100):
   try:urllib.request.urlopen(base+'/health',timeout=.2).close();break
   except OSError:time.sleep(.04)
  else:raise RuntimeError(proc.stderr.read().decode())
  c=sqlite3.connect(db)
  c.execute("INSERT INTO employees(id,name,email,created_at) VALUES(1,'Manager','manager@example.test','2026-09-01T00:00:00Z')")
  c.execute("INSERT INTO employee_roles VALUES(1,'manager')")
  c.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
  c.execute("INSERT INTO manager_cards VALUES('AABBCCDD',1,1)")
  c.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+3600)",('d'*64,))
  c.execute("INSERT INTO registered_clocks VALUES('CLOCK','fixture-device-key',1)")
  c.execute("INSERT INTO clock_late_settings VALUES('CLOCK',5)")
  for emp in range(2,30):
   c.execute("INSERT INTO employees(id,name,created_at) VALUES(?,?, '2026-09-01T00:00:00Z')",(emp,'Worker '+str(emp)))
   c.execute("INSERT INTO cards(uid,employee_id) VALUES(?,?)",(f'AA0000{emp:02X}',emp))
   c.execute("INSERT INTO schedule_profiles(employee_id,pay_type,schedule_mode,start_time,end_time,weekday_mask) VALUES(?,'hourly',?,'08:00','16:30',127)",(emp,'none' if emp==8 else 'dated_shifts' if emp==7 else 'recurring_weekly'))
  c.execute("UPDATE card_ownership_history SET effective_at='2026-09-01T00:00:00Z'")
  c.execute("INSERT INTO schedule_shifts(employee_id,work_date,start_time,end_time) VALUES(7,'2026-10-12','22:00','06:00')")
  c.execute("UPDATE employees SET email='worker3@example.test' WHERE id=3")
  c.commit()
  def swipe(emp,e,local,settle=True):
   stamp=datetime.datetime.fromisoformat(local).replace(tzinfo=ZoneInfo('America/Edmonton')).astimezone(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
   req=urllib.request.Request(base+'/api/v1/swipes',data=json.dumps(dict(event_id=e,clock_id='CLOCK',card_uid=f'AA0000{emp:02X}',swiped_at=stamp)).encode(),headers={'X-Clock-ID':'CLOCK','X-Device-Key':'fixture-device-key','Content-Type':'application/json'})
   with urllib.request.urlopen(req,timeout=4) as r:result=json.load(r)
   due=c.execute('SELECT departure_due FROM attendance_events WHERE event_id=?',(e,)).fetchone()
   if settle and due and due[0]:finalize(max(due[0],c.execute('SELECT departure_ready FROM attendance_events WHERE event_id=?',(e,)).fetchone()[0])+1)
   return result
  for emp,finish,expected in [(2,'16:25:00',0),(3,'16:24:59',6),(4,'16:30:00',0),(5,'16:31:00',0),(6,'16:29:59',0),(8,'15:00:00',0)]:
   swipe(emp,f'in{emp}','2026-10-12T08:00:00');swipe(emp,f'out{emp}','2026-10-12T'+finish)
   assert c.execute('SELECT early_minutes FROM attendance_events WHERE event_id=?',(f'out{emp}',)).fetchone()[0]==expected,(emp,expected)
  swipe(7,'in7','2026-10-12T22:00:00');swipe(7,'out7','2026-10-13T05:54:59')
  assert c.execute("SELECT early_minutes FROM attendance_events WHERE event_id='out7'").fetchone()[0]==6
  c.execute("UPDATE clock_late_settings SET grace_minutes=0");c.commit()
  swipe(9,'in9','2026-10-12T08:00:00');swipe(9,'out9','2026-10-12T16:29:59')
  assert c.execute("SELECT early_minutes FROM attendance_events WHERE event_id='out9'").fetchone()[0]==1
  c.execute("UPDATE clock_late_settings SET grace_minutes=10");c.commit()
  assert swipe(3,'out3','2026-10-12T16:24:59')['classification']=='OUT'
  def request(path,form=None,auth=True,expected=200):
   headers={'Cookie':'tc_session='+'d'*64} if auth else {}
   data=urllib.parse.urlencode(form).encode() if form is not None else None
   try:
    with urllib.request.urlopen(urllib.request.Request(base+path,data=data,headers=headers),timeout=6) as r:assert r.status==expected;return r.read()
   except urllib.error.HTTPError as e:assert e.code==expected,(e.code,e.read());return b''
  punches=json.loads(request('/api/v1/admin/punches'))['punches']
  assert next(p for p in punches if p['id']=='out3')['early_minutes']==6
  before=c.execute("SELECT seconds FROM effective_shift_pairs WHERE out_id='out3'").fetchone()[0]
  assert before==30299,before
  templates=json.loads(request('/api/v1/admin/templates'))['templates']
  for template in ['early_departure_employee','early_departure_manager']:
   t=next(t for t in templates if t['id']==template)
   assert t['implemented'] and t['enabled'],t
  employee_body=c.execute("SELECT body FROM email_queue WHERE event_id='early:out3:employee'").fetchone()[0]
  manager_body=c.execute("SELECT body FROM email_queue WHERE event_id='early:out3:manager:1'").fetchone()[0]
  for body in [employee_body,manager_body]:
   assert 'Scheduled finish: 2026-10-12 16:30:00' in body,body
   assert 'Recorded OUT: 2026-10-12 16:24:59' in body,body
   assert 'Early: 6 minutes' in body and '{{' not in body,body
  token=re.search(r'/n/([a-f0-9]{64})',employee_body).group(1)
  assert token not in manager_body and '/n/' not in manager_body
  page=request('/n/'+token,auth=False).decode()
  assert 'Early departure explanation' in page and 'scheduled finish' in page,page
  assert request('/n/'+token,dict(note='Approved appointment'),auth=False,expected=200)
  assert c.execute("SELECT note FROM swipe_notes WHERE event_id='out3'").fetchone()[0]=='Approved appointment'
  assert c.execute("SELECT count(*) FROM email_queue WHERE event_id LIKE 'early:out3:%'").fetchone()[0]==2
  overnight=c.execute("SELECT body FROM email_queue WHERE event_id='early:out7:manager:1'").fetchone()[0]
  assert 'Scheduled finish: 2026-10-13 06:00:00' in overnight,overnight
  pid=c.execute("INSERT INTO pay_periods(starts,ends,status) VALUES('2026-10-12','2026-10-13','closed') RETURNING id").fetchone()[0];c.commit()
  request('/api/v1/admin/periods/exports',dict(period_id=pid))
  frozen=c.execute('SELECT csv FROM lite_period_snapshots WHERE period_id=? AND revision=1',(pid,)).fetchone()[0]
  assert 'Left early: 6 min' in frozen,frozen
  payload=dict(action='note',event_id='out3',reason='Approved departure for appointment')
  request('/api/v1/admin/punches',payload,auth=False,expected=401)
  request('/api/v1/admin/punches',payload)
  assert c.execute("SELECT note,updated_by FROM swipe_notes WHERE event_id='out3'").fetchone()==(payload['reason'],1)
  assert c.execute("SELECT count(*) FROM swipe_note_history WHERE event_id='out3'").fetchone()[0]==2
  request('/api/v1/admin/punches',dict(payload,reason='Approved by manager, revised note'))
  assert c.execute("SELECT count(*) FROM swipe_note_history WHERE event_id='out3'").fetchone()[0]==3
  assert c.execute("SELECT seconds FROM effective_shift_pairs WHERE out_id='out3'").fetchone()[0]==before
  request('/api/v1/admin/punches',dict(payload,event_id='in3'),expected=400)
  request('/api/v1/admin/punches',dict(payload,reason='x'),expected=400)
  request('/api/v1/admin/periods/exports',dict(period_id=pid))
  revised=c.execute('SELECT csv FROM lite_period_snapshots WHERE period_id=? AND revision=2',(pid,)).fetchone()[0]
  assert 'Manager note: Approved by manager, revised note' in revised,revised
  assert c.execute('SELECT csv FROM lite_period_snapshots WHERE period_id=? AND revision=1',(pid,)).fetchone()[0]==frozen
  assert c.execute('SELECT count(*) FROM email_queue').fetchone()[0]==4
  # The employee opt-in and each template independently control delivery.
  c.execute("UPDATE employees SET email='worker10@example.test' WHERE id=10")
  c.execute("UPDATE clock_late_settings SET grace_minutes=5")
  c.execute("UPDATE email_templates SET enabled=1 WHERE id='swipe_employee'");c.commit()
  emp_tpl=next(t for t in templates if t['id']=='early_departure_employee')
  request('/api/v1/admin/templates',dict(id=emp_tpl['id'],subject='Leaving {{early_minutes}} minutes early',body='Finish {{scheduled_end}}; OUT {{actual_end}}',enabled='1'))
  swipe(10,'in10a','2026-10-14T08:00:00');swipe(10,'out10a','2026-10-14T16:24:59')
  assert c.execute("SELECT subject,body FROM email_queue WHERE event_id='early:out10a:employee'").fetchone()==('Leaving 6 minutes early','Finish 2026-10-14 16:30:00; OUT 2026-10-14 16:24:59')
  assert c.execute("SELECT count(*) FROM email_queue WHERE event_id='out10a'").fetchone()[0]==1
  request('/api/v1/admin/templates',dict(id=emp_tpl['id'],subject=emp_tpl['subject'],body=emp_tpl['body'],enabled='0'))
  swipe(10,'in10b','2026-10-15T08:00:00');swipe(10,'out10b','2026-10-15T16:24:59')
  assert c.execute("SELECT count(*) FROM email_queue WHERE event_id='early:out10b:employee'").fetchone()[0]==0
  assert c.execute("SELECT count(*) FROM email_queue WHERE event_id='early:out10b:manager:1'").fetchone()[0]==1
  assert c.execute("SELECT count(*) FROM email_queue WHERE event_id='out10b'").fetchone()[0]==1
  c.execute("UPDATE employees SET late_email_enabled=0 WHERE id=10");c.commit()
  swipe(10,'in10c','2026-10-16T08:00:00');swipe(10,'out10c','2026-10-16T16:24:59')
  assert c.execute("SELECT count(*) FROM email_queue WHERE event_id LIKE 'early:out10c:%'").fetchone()[0]==0
  c.execute("UPDATE employees SET late_email_enabled=1 WHERE id=10")
  c.execute("INSERT INTO statutory_holidays(day,name) VALUES('2026-10-17','Test holiday')")
  c.execute("UPDATE holiday_settings SET skip_alerts=1");c.commit()
  swipe(10,'in10d','2026-10-17T08:00:00');swipe(10,'out10d','2026-10-17T16:24:59')
  assert c.execute("SELECT count(*) FROM email_queue WHERE event_id LIKE 'early:out10d:%'").fetchone()[0]==0
  # Arrival/departure eligibility must agree for approved leave and excused absences.
  c.execute("UPDATE email_templates SET enabled=0 WHERE id='swipe_employee'")
  c.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status) VALUES(11,'vacation','2026-10-20','2026-10-20','approved')")
  c.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status,cancelled_at) VALUES(15,'vacation','2026-10-20','2026-10-20','approved','2026-10-19T00:00:00Z')")
  for emp,reason,start,end in [(12,'Sick','',''),(13,'Medical appointment','12:00','13:00'),(14,'Unexcused absence','','')]:
   c.execute("INSERT INTO attendance_absences(employee_id,day,reason,starts,ends,manager_id) VALUES(?,'2026-10-20',?,?,?,1)",(emp,reason,start,end))
  c.commit()
  for emp,expected in [(11,0),(12,0),(13,7),(14,7),(15,7)]:
   swipe(emp,f'parityin{emp}','2026-10-20T08:06:01')
   swipe(emp,f'parityout{emp}','2026-10-20T16:23:59')
   assert c.execute('SELECT late_minutes FROM attendance_events WHERE event_id=?',(f'parityin{emp}',)).fetchone()[0]==expected
   assert c.execute('SELECT early_minutes FROM attendance_events WHERE event_id=?',(f'parityout{emp}',)).fetchone()[0]==expected
   for prefix,event in [('late',f'parityin{emp}'),('early',f'parityout{emp}')]:
    assert c.execute('SELECT count(*) FROM email_queue WHERE event_id=?',(f'{prefix}:{event}:manager:1',)).fetchone()[0]==int(expected>0)
  request('/api/v1/admin/punches',dict(action='note',event_id='parityin13',reason='Approved delayed arrival'))
  request('/api/v1/admin/punches',dict(action='note',event_id='parityout13',reason='Approved early departure'))
  for event in ['parityin13','parityout13']:
   assert c.execute('SELECT updated_by FROM swipe_notes WHERE event_id=?',(event,)).fetchone()[0]==1
   assert c.execute('SELECT count(*) FROM swipe_note_history WHERE event_id=?',(event,)).fetchone()[0]==1
  paritypid=c.execute("INSERT INTO pay_periods(starts,ends,status) VALUES('2026-10-20','2026-10-20','closed') RETURNING id").fetchone()[0];c.commit()
  request('/api/v1/admin/periods/exports',dict(period_id=paritypid))
  paritycsv=c.execute('SELECT csv FROM lite_period_snapshots WHERE period_id=?',(paritypid,)).fetchone()[0]
  assert 'Complete - late arrival and early departure' in paritycsv,paritycsv
  assert 'Approved delayed arrival' in paritycsv and 'Approved early departure' in paritycsv
  # Lunch and errands are intermediate OUTs, not early departures.
  c.execute("UPDATE schedule_profiles SET end_time='16:00' WHERE employee_id=16")
  c.execute("UPDATE schedule_profiles SET start_time='22:00',end_time='06:00' WHERE employee_id=17")
  c.execute("UPDATE schedule_profiles SET schedule_mode='dated_shifts' WHERE employee_id=18")
  c.execute("INSERT INTO schedule_shifts(employee_id,work_date,start_time,end_time) VALUES(18,'2027-01-11','22:00','06:00')")
  c.execute("INSERT INTO schedule_shifts(employee_id,work_date,start_time,end_time) VALUES(18,'2027-01-12','22:00','06:00')")
  c.execute("INSERT INTO attendance_absences(employee_id,day,reason,starts,ends,manager_id) VALUES(19,'2027-01-11','Medical appointment','14:00','16:30',1)");c.commit()
  swipe(16,'lunchin','2027-01-11T08:00:00',False)
  swipe(16,'lunchout','2027-01-11T12:00:00',False)
  lunchdue=c.execute("SELECT departure_due FROM attendance_events WHERE event_id='lunchout'").fetchone()[0]
  assert c.execute("SELECT early_minutes FROM attendance_events WHERE event_id='lunchout'").fetchone()[0]==0
  assert c.execute("SELECT count(*) FROM email_queue WHERE event_id LIKE 'early:lunchout:%'").fetchone()[0]==0
  finalize(lunchdue)
  assert c.execute("SELECT departure_due FROM attendance_events WHERE event_id='lunchout'").fetchone()[0]==lunchdue
  swipe(16,'lunchreturn','2027-01-11T12:30:00',False)
  swipe(16,'lunchfinish','2027-01-11T16:00:00',False)
  finalize(lunchdue+1);finalize(lunchdue+60)
  assert c.execute("SELECT sum(early_minutes+late_minutes) FROM attendance_events WHERE employee_id=16").fetchone()[0]==0
  assert c.execute("SELECT count(*) FROM email_queue WHERE event_id LIKE 'early:lunch%'").fetchone()[0]==0
  for emp in [17,18]:
   swipe(emp,f'nightin{emp}','2027-01-12T00:30:00',False)
   assert c.execute('SELECT late_minutes FROM attendance_events WHERE event_id=?',(f'nightin{emp}',)).fetchone()[0]==150
   body=c.execute('SELECT body FROM email_queue WHERE event_id=?',(f'late:nightin{emp}:manager:1',)).fetchone()[0]
   assert 'Scheduled start: 2027-01-11 22:00:00' in body,body
   swipe(emp,f'nightout{emp}','2027-01-12T06:00:00')
   assert c.execute('SELECT early_minutes FROM attendance_events WHERE event_id=?',(f'nightout{emp}',)).fetchone()[0]==0
   assert c.execute('SELECT count(*) FROM email_queue WHERE event_id LIKE ?',(f'early:nightout{emp}:%',)).fetchone()[0]==0
   swipe(emp,f'nextnightin{emp}','2027-01-12T22:10:00',False)
   assert c.execute('SELECT late_minutes FROM attendance_events WHERE event_id=?',(f'nextnightin{emp}',)).fetchone()[0]==10
   assert c.execute('SELECT count(*) FROM email_queue WHERE event_id=?',(f'late:nextnightin{emp}:manager:1',)).fetchone()[0]==1
   swipe(emp,f'nextnightout{emp}','2027-01-13T06:00:00')
  swipe(19,'appointmentin','2027-01-11T08:00:00',False)
  swipe(19,'appointmentout','2027-01-11T14:00:00')
  assert c.execute("SELECT early_minutes FROM attendance_events WHERE event_id='appointmentout'").fetchone()[0]==0
  for emp,settle in [(20,False),(21,True)]:
   swipe(emp,f'fixin{emp}','2027-01-11T08:00:00',False)
   swipe(emp,f'fixout{emp}','2027-01-11T14:00:00',settle)
   due=c.execute('SELECT departure_due FROM attendance_events WHERE event_id=?',(f'fixout{emp}',)).fetchone()[0]
   fixed=datetime.datetime.fromisoformat('2027-01-11T16:30:00').replace(tzinfo=ZoneInfo('America/Edmonton')).astimezone(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
   request('/api/v1/admin/punches',dict(action='change',event_id=f'fixout{emp}',employee_id=emp,kind='OUT',at=fixed,reason='Corrected mistyped departure'))
   finalize(due+1)
   assert c.execute('SELECT early_minutes,departure_due FROM attendance_events WHERE event_id=?',(f'fixout{emp}',)).fetchone()==(0,0)
   assert c.execute('SELECT early_minutes FROM punch_corrections WHERE event_id=?',(f'fixout{emp}',)).fetchone()[0]==0
   assert c.execute("SELECT count(*) FROM email_queue WHERE event_id LIKE ? AND status='pending'",(f'early:fixout{emp}:%',)).fetchone()[0]==0
  # A daytime meeting belongs to a different resolved shift from tonight's arrival.
  c.execute("UPDATE schedule_profiles SET start_time='22:00',end_time='06:00' WHERE employee_id=22");c.commit()
  swipe(22,'meetingin','2027-01-12T10:00:00',False);swipe(22,'meetingout','2027-01-12T11:00:00',False)
  swipe(22,'meetingnightin','2027-01-12T22:10:00',False)
  assert c.execute("SELECT late_minutes FROM attendance_events WHERE event_id='meetingnightin'").fetchone()[0]==10
  # Corrected and manually added INs retain derived lateness, without duplicate late notices.
  swipe(23,'correctlatein','2027-01-11T08:00:00',False)
  fixed=datetime.datetime.fromisoformat('2027-01-11T08:30:00').replace(tzinfo=ZoneInfo('America/Edmonton')).astimezone(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
  request('/api/v1/admin/punches',dict(action='change',event_id='correctlatein',employee_id=23,kind='IN',at=fixed,reason='Corrected actual arrival'))
  assert c.execute("SELECT late_minutes FROM attendance_events WHERE event_id='correctlatein'").fetchone()[0]==30
  assert c.execute("SELECT late_minutes FROM punch_corrections WHERE event_id='correctlatein'").fetchone()[0]==30
  swipe(23,'correctlateout','2027-01-11T16:30:00')
  request('/api/v1/admin/punches',dict(action='add',event_id='',employee_id=23,kind='IN',at=fixed.replace('2027-01-11','2027-01-12'),reason='Recorded forgotten arrival'))
  assert c.execute("SELECT late_minutes FROM punch_corrections WHERE employee_id=23 AND action='add'").fetchone()[0]==30
  swipe(23,'manuallateout','2027-01-12T16:30:00')
  assert c.execute("SELECT count(*) FROM email_queue WHERE event_id='late:correctlatein:manager:1'").fetchone()[0]==0
  punches=json.loads(request('/api/v1/admin/punches'))['punches']
  assert next(p for p in punches if p['id']=='correctlatein')['late']==30
  assert any(p['manual'] and p['employee_id']==23 and p['late']==30 for p in punches)
  correctedpid=c.execute("INSERT INTO pay_periods(starts,ends,status) VALUES('2027-01-11','2027-01-12','closed') RETURNING id").fetchone()[0];c.commit()
  request('/api/v1/admin/periods/exports',dict(period_id=correctedpid))
  correctedcsv=c.execute('SELECT csv FROM lite_period_snapshots WHERE period_id=?',(correctedpid,)).fetchone()[0]
  assert 'Late arrival: 30 min' in correctedcsv,correctedcsv
  # An overdue uploaded lunch OUT survives an intervening timer pass while return IN uploads.
  c.execute("UPDATE schedule_profiles SET end_time='16:00' WHERE employee_id=24")
  c.execute("UPDATE employees SET email='offline@example.test' WHERE id=24")
  c.execute("UPDATE email_templates SET enabled=1 WHERE id='swipe_employee'");c.commit()
  swipe(24,'offlinein','2026-10-20T08:00:00',False)
  swipe(24,'offlineout','2026-10-20T12:00:00',False)
  assert c.execute("SELECT count(*) FROM email_queue WHERE event_id='offlineout'").fetchone()[0]==1
  ready=c.execute("SELECT departure_ready FROM attendance_events WHERE event_id='offlineout'").fetchone()[0]
  finalize(ready-1)
  assert c.execute("SELECT early_minutes FROM attendance_events WHERE event_id='offlineout'").fetchone()[0]==0
  swipe(24,'offlinereturn','2026-10-20T12:30:00',False)
  swipe(24,'offlinefinish','2026-10-20T16:00:00',False)
  finalize(ready+120)
  assert c.execute("SELECT early_minutes FROM attendance_events WHERE event_id='offlineout'").fetchone()[0]==0
  assert c.execute("SELECT count(*) FROM email_queue WHERE event_id LIKE 'early:offlineout:%'").fetchone()[0]==0
  # An IN-only correction preserves its already-finalized OUT flag and sent notices.
  swipe(25,'preservein','2027-01-14T08:00:00',False)
  swipe(25,'preserveout','2027-01-14T14:00:00')
  c.execute("UPDATE email_queue SET status='sent' WHERE event_id LIKE 'early:preserveout:%'");c.commit()
  count=c.execute("SELECT count(*) FROM email_queue WHERE event_id LIKE 'early:preserveout:%'").fetchone()[0]
  def stamp(local):return datetime.datetime.fromisoformat(local).replace(tzinfo=ZoneInfo('America/Edmonton')).astimezone(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
  request('/api/v1/admin/punches',dict(action='change',event_id='preservein',employee_id=25,kind='IN',at=stamp('2027-01-14T08:02:00'),reason='Corrected two minute reader error'))
  assert c.execute("SELECT early_minutes FROM attendance_events WHERE event_id='preserveout'").fetchone()[0]==150
  assert c.execute("SELECT count(*) FROM email_queue WHERE event_id LIKE 'early:preserveout:%' AND status='sent'").fetchone()[0]==count
  # An OUT correction recalculates 90 minutes rather than erasing the departure.
  swipe(26,'recalcin','2027-01-14T08:00:00',False)
  swipe(26,'recalcout','2027-01-14T14:00:00')
  count=c.execute("SELECT count(*) FROM email_queue WHERE event_id LIKE 'early:recalcout:%'").fetchone()[0]
  request('/api/v1/admin/punches',dict(action='change',event_id='recalcout',employee_id=26,kind='OUT',at=stamp('2027-01-14T15:00:00'),reason='Corrected actual departure time'))
  assert c.execute("SELECT early_minutes FROM attendance_events WHERE event_id='recalcout'").fetchone()[0]==0
  assert c.execute("SELECT departure_due FROM punch_corrections WHERE event_id='recalcout'").fetchone()[0]>0
  finalize(1900000000)
  assert c.execute("SELECT early_minutes FROM punch_corrections WHERE event_id='recalcout'").fetchone()[0]==90
  assert c.execute("SELECT count(*) FROM email_queue WHERE event_id LIKE 'early:recalcout:%'").fetchone()[0]==count
  assert c.execute("SELECT early_minutes FROM attendance_events WHERE event_id='recalcout'").fetchone()[0]==90
  swipe(27,'manualearlyin','2027-01-14T08:00:00',False)
  request('/api/v1/admin/punches',dict(action='add',event_id='',employee_id=27,kind='OUT',at=stamp('2027-01-14T15:00:00'),reason='Recorded forgotten departure'))
  assert c.execute("SELECT early_minutes FROM punch_corrections WHERE employee_id=27 AND action='add'").fetchone()[0]==0
  finalize(1900000000)
  assert c.execute("SELECT early_minutes FROM punch_corrections WHERE employee_id=27 AND action='add'").fetchone()[0]==90
  entries=json.loads(request('/api/v1/admin/punches'))['punches']
  for event,minutes in [('preserveout',150),('recalcout',90)]:assert next(e for e in entries if e['id']==event)['early_minutes']==minutes
  assert any(e['manual'] and e['employee_id']==27 and e['early_minutes']==90 for e in entries)
  pid2=c.execute("INSERT INTO pay_periods(starts,ends,status) VALUES('2027-01-14','2027-01-14','closed') RETURNING id").fetchone()[0];c.commit()
  request('/api/v1/admin/periods/exports',dict(period_id=pid2))
  outcsv=c.execute('SELECT csv FROM lite_period_snapshots WHERE period_id=?',(pid2,)).fetchone()[0]
  assert outcsv.count('Left early: 90 min')==2 and 'Left early: 150 min' in outcsv,outcsv
  # Lunch OUT corrected before the return must remain pending, for raw and manual punches.
  for emp in [28,29]:
   swipe(emp,f'correctlunchin{emp}','2027-01-15T08:00:00',False)
   if emp==28:
    swipe(emp,f'correctlunchout{emp}','2027-01-15T12:00:00',False)
    request('/api/v1/admin/punches',dict(action='change',event_id=f'correctlunchout{emp}',employee_id=emp,kind='OUT',at=stamp('2027-01-15T12:01:00'),reason='Corrected lunch departure'))
   else:
    request('/api/v1/admin/punches',dict(action='add',event_id='',employee_id=emp,kind='OUT',at=stamp('2027-01-15T12:01:00'),reason='Recorded missed lunch departure'))
   cid,target,due,early=c.execute("SELECT id,coalesce(event_id,manual_id),departure_due,early_minutes FROM punch_corrections WHERE employee_id=? AND kind='OUT'",(emp,)).fetchone()
   assert due>0 and early==0
   finalize(due)
   assert c.execute('SELECT departure_due FROM punch_corrections WHERE id=?',(cid,)).fetchone()[0]==due
   swipe(emp,f'correctlunchreturn{emp}','2027-01-15T12:30:00',False)
   swipe(emp,f'correctlunchfinish{emp}','2027-01-15T16:30:00',False)
   finalize(due+1);finalize(due+120)
   assert c.execute('SELECT early_minutes,departure_due FROM punch_corrections WHERE id=?',(cid,)).fetchone()==(0,0)
   assert c.execute('SELECT count(*) FROM email_queue WHERE instr(event_id,?)=1',('early:'+target+':',)).fetchone()[0]==0
   entries=json.loads(request('/api/v1/admin/punches'))['punches']
   assert next(e for e in entries if e['id']==target)['early_minutes']==0
  # Upgrade an existing current database missing the new column, without changing old punches.
  proc.terminate();proc.communicate(timeout=5)
  c.execute('ALTER TABLE attendance_events DROP COLUMN early_minutes');c.commit();c.close()
  proc=subprocess.Popen([str(root/'timeclock-server'),str(db),str(port)],env={**os.environ,'TZ':'America/Edmonton','DEVICE_KEY':'fixture-device-key'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
  for _ in range(100):
   try:urllib.request.urlopen(base+'/health',timeout=.2).close();break
   except OSError:time.sleep(.04)
  with sqlite3.connect(db) as upgraded:
   assert upgraded.execute("SELECT early_minutes FROM attendance_events WHERE event_id='out3'").fetchone()[0]==0
   assert upgraded.execute("SELECT seconds FROM effective_shift_pairs WHERE out_id='out3'").fetchone()[0]==before
   assert upgraded.execute('SELECT csv FROM lite_period_snapshots WHERE period_id=? AND revision=1',(pid,)).fetchone()[0]==frozen
  print('PASS early grace, lunch, two consecutive overnight shifts (weekly/dated), daytime meeting, corrected/manual lateness in API/payroll, immediate receipts, overdue upload hold and return, notifications, private explanation, audited notes, immutable exports and upgrade')
 finally:
  proc.terminate();proc.communicate(timeout=5)
