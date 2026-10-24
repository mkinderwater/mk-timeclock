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
  c.execute("INSERT INTO employees(id,name,email) VALUES(1,'Jane','jane@example.test')")
  c.execute("INSERT INTO cards(uid,employee_id) VALUES('AABBCCDD',1)")
  c.execute("INSERT INTO schedule_profiles(employee_id,pay_type,start_time,end_time,weekday_mask) VALUES(1,'salary','08:00','16:00',127)")
  c.execute("INSERT INTO clock_late_settings(clock_id,grace_minutes) VALUES('CLOCK-A',5)")
  c.commit()
  assert c.execute("SELECT enabled FROM email_templates WHERE id='swipe_employee'").fetchone()[0]==0
  def swipe(event,clock,local):
   t=datetime.datetime.fromisoformat(local).replace(tzinfo=ZoneInfo('America/Edmonton')).astimezone(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
   req=urllib.request.Request(f'http://127.0.0.1:{port}/api/v1/swipes',data=json.dumps({'event_id':event,'clock_id':clock,'card_uid':'AABBCCDD','swiped_at':t}).encode(),headers={'X-Device-Key':key,'Content-Type':'application/json'})
   with urllib.request.urlopen(req,timeout=4) as r:assert r.status==200
  swipe('a1','CLOCK-A','2026-10-12T08:05:00')
  assert c.execute("SELECT late_minutes FROM attendance_events WHERE event_id='a1'").fetchone()[0]==5
  assert c.execute("SELECT count(*) FROM email_queue").fetchone()[0]==0
  # Another day, one second beyond grace, even with generic template enabled.
  c.execute("UPDATE email_templates SET enabled=1 WHERE id='swipe_employee'");c.commit()
  swipe('a2','CLOCK-A','2026-10-13T08:05:01')
  assert c.execute("SELECT late_minutes FROM attendance_events WHERE event_id='a2'").fetchone()[0]==6
  assert c.execute("SELECT count(*) FROM email_queue WHERE event_id='late:a2:employee'").fetchone()[0]==1
  assert c.execute("SELECT count(*) FROM email_queue WHERE event_id='a2'").fetchone()[0]==0
  # A different clock inherits the five-minute default; generic email stays enabled.
  swipe('a3','CLOCK-B','2026-10-14T08:00:01')
  assert c.execute("SELECT late_minutes FROM attendance_events WHERE event_id='a3'").fetchone()[0]==1
  assert c.execute("SELECT count(*) FROM email_queue WHERE event_id='late:a3:employee'").fetchone()[0]==0
  assert c.execute("SELECT count(*) FROM email_queue WHERE event_id='a3'").fetchone()[0]==1
  # Within grace with the generic notification enabled, the generic mail is still sent.
  swipe('a4','CLOCK-A','2026-10-15T08:04:59')
  assert c.execute("SELECT count(*) FROM email_queue WHERE event_id='a4'").fetchone()[0]==1
  assert c.execute("SELECT count(*) FROM email_queue WHERE event_id='late:a4:employee'").fetchone()[0]==0
  print('PASS per-clock grace boundaries, actual lateness retained, generic email opt-in, one employee notice for late swipe')
 finally:
  srv.terminate()
  try:srv.communicate(timeout=3)
  except subprocess.TimeoutExpired:srv.kill()
