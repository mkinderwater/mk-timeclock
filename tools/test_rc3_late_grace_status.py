import subprocess,tempfile,sqlite3,time,urllib.request,json,os,datetime
from zoneinfo import ZoneInfo
BASE=os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
with tempfile.TemporaryDirectory() as tmp:
 db=tmp+'/t.db'; port=18793; key='test-device-secret'
 srv=subprocess.Popen([BASE+'/timeclock-server',db,str(port)],env={**os.environ,'DEVICE_KEY':key,'TZ':'America/Edmonton'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
 try:
  for _ in range(50):
   if os.path.exists(db):break
   time.sleep(.1)
  c=sqlite3.connect(db)
  c.execute("INSERT INTO employees(id,name,email,created_at) VALUES(1,'Jane','jane@example.test','2026-09-01T00:00:00Z')")
  c.execute("INSERT INTO cards(uid,employee_id) VALUES('AABBCCDD',1)")
  c.execute("INSERT INTO schedule_profiles(employee_id,pay_type,schedule_mode,start_time,end_time,weekday_mask) VALUES(1,'salary','recurring_weekly','08:00','16:00',127)")
  c.execute("INSERT INTO clock_late_settings(clock_id,grace_minutes) VALUES('CLOCK-A',5)")
  c.execute("UPDATE card_ownership_history SET effective_at='2026-09-01T00:00:00Z'")
  for clock in ['CLOCK-A','CLOCK-B','CLOCK-ZERO']:c.execute('INSERT INTO registered_clocks(clock_id,device_key,active) VALUES(?,?,1)',(clock,key+clock))
  c.execute("INSERT INTO employees(id,name,email) VALUES(2,'Manager','manager@example.test')")
  c.execute("INSERT INTO employee_roles VALUES(2,'manager')")
  c.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(2,'x')")
  c.execute("INSERT INTO manager_cards VALUES('A1B2C3D4',2,1)")
  c.execute("INSERT INTO login_sessions VALUES(?,2,CAST(strftime('%s','now') AS INTEGER)+3600)",('d'*64,))
  c.commit()
  assert c.execute("SELECT enabled FROM email_templates WHERE id='swipe_employee'").fetchone()[0]==0
  def swipe(event,clock,local):
   t=datetime.datetime.fromisoformat(local).replace(tzinfo=ZoneInfo('America/Edmonton')).astimezone(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
   req=urllib.request.Request(f'http://127.0.0.1:{port}/api/v1/swipes',data=json.dumps({'event_id':event,'clock_id':clock,'card_uid':'AABBCCDD','swiped_at':t}).encode(),headers={'X-Device-Key':key+clock,'X-Clock-ID':clock,'Content-Type':'application/json'})
   with urllib.request.urlopen(req,timeout=4) as r:
    assert r.status==200
    return json.load(r)
  swipe('a1','CLOCK-A','2026-10-12T08:05:00')
  assert c.execute("SELECT late_minutes FROM attendance_events WHERE event_id='a1'").fetchone()[0]==0
  assert c.execute("SELECT count(*) FROM email_queue").fetchone()[0]==0
  swipe('out1','CLOCK-A','2026-10-12T16:00:00')
  # Another day, one second beyond grace, even with generic template enabled.
  c.execute("UPDATE email_templates SET enabled=1 WHERE id='swipe_employee'");c.commit()
  swipe('a2','CLOCK-A','2026-10-13T08:05:01')
  assert c.execute("SELECT late_minutes FROM attendance_events WHERE event_id='a2'").fetchone()[0]==6
  assert c.execute("SELECT count(*) FROM email_queue WHERE event_id='late:a2:employee'").fetchone()[0]==1
  assert c.execute("SELECT count(*) FROM email_queue WHERE event_id='a2'").fetchone()[0]==0
  for recipient in ['employee','manager:2']:
   body=c.execute("SELECT body FROM email_queue WHERE event_id=?",('late:a2:'+recipient,)).fetchone()
   assert body is not None,recipient
   assert 'Scheduled start: 2026-10-13 08:00:00' in body[0],body[0]
   assert 'Recorded IN: 2026-10-13 08:05:01' in body[0],body[0]
  c.execute("UPDATE clock_late_settings SET grace_minutes=10 WHERE clock_id='CLOCK-A'");c.commit()
  assert swipe('a2','CLOCK-A','2026-10-13T08:05:01')['classification']=='IN_LATE'
  assert c.execute("SELECT count(*) FROM email_queue WHERE event_id LIKE 'late:a2:%'").fetchone()[0]==2
  # Dashboard uses stored status even after grace changes. Copy fixture to today's local date.
  c.execute("UPDATE swipes SET swiped_at=strftime('%Y-%m-%dT%H:%M:%SZ','now') WHERE event_id='a2'");c.commit()
  req=urllib.request.Request(f'http://127.0.0.1:{port}/',headers={'Cookie':'tc_session='+'d'*64})
  with urllib.request.urlopen(req,timeout=4) as r:dashboard=r.read().decode()
  assert 'Late 6 min' in dashboard,dashboard
  c.execute("UPDATE swipes SET swiped_at='2026-10-13T14:05:01Z' WHERE event_id='a2'")
  c.execute("UPDATE clock_late_settings SET grace_minutes=5 WHERE clock_id='CLOCK-A'");c.commit()
  swipe('out2','CLOCK-A','2026-10-13T16:00:00')
  # A different clock inherits the five-minute default; generic email stays enabled.
  swipe('a3','CLOCK-B','2026-10-14T08:00:01')
  assert c.execute("SELECT late_minutes FROM attendance_events WHERE event_id='a3'").fetchone()[0]==0
  assert c.execute("SELECT count(*) FROM email_queue WHERE event_id='late:a3:employee'").fetchone()[0]==0
  assert c.execute("SELECT count(*) FROM email_queue WHERE event_id='a3'").fetchone()[0]==1
  swipe('out3','CLOCK-B','2026-10-14T16:00:00')
  # Within grace with the generic notification enabled, the generic mail is still sent.
  swipe('a4','CLOCK-A','2026-10-15T08:04:59')
  assert c.execute("SELECT count(*) FROM email_queue WHERE event_id='a4'").fetchone()[0]==1
  assert c.execute("SELECT count(*) FROM email_queue WHERE event_id='late:a4:employee'").fetchone()[0]==0
  assert c.execute("SELECT late_minutes FROM attendance_events WHERE event_id='a4'").fetchone()[0]==0
  swipe('out4','CLOCK-A','2026-10-15T16:00:00')
  c.execute("INSERT INTO clock_late_settings(clock_id,grace_minutes) VALUES('CLOCK-ZERO',0)");c.commit()
  swipe('a5','CLOCK-ZERO','2026-10-16T08:00:01')
  assert c.execute("SELECT late_minutes FROM attendance_events WHERE event_id='a5'").fetchone()[0]==1
  assert c.execute("SELECT count(*) FROM email_queue WHERE event_id='late:a5:employee'").fetchone()[0]==1
  for body, in c.execute("SELECT body FROM email_queue WHERE event_id LIKE 'late:a5:%'"):
   assert 'Scheduled start: 2026-10-16 08:00:00' in body,body
  print('PASS per-clock grace boundaries, late status only beyond grace, generic email opt-in, one employee notice for late swipe')
 finally:
  srv.terminate()
  try:srv.communicate(timeout=3)
  except subprocess.TimeoutExpired:srv.kill()
