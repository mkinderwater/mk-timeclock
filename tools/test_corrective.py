import os, sqlite3, subprocess, tempfile, urllib.request, json, time, datetime
from zoneinfo import ZoneInfo
from pathlib import Path
BASE=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as td:
 db=f'{td}/db.sqlite';port=18879;key='test-only-device-key-123'
 proc=subprocess.Popen([str(BASE/'timeclock-server'),db,str(port)],env={**os.environ,'TZ':'America/Edmonton','DEVICE_KEY':key},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
 try:
  for _ in range(60):
   if os.path.isfile(db):
    try:
     c=sqlite3.connect(db);found=c.execute("SELECT 1 FROM sqlite_master WHERE name='employees'").fetchone();c.close()
     if found:break
    except sqlite3.OperationalError:pass
   time.sleep(.05)
  conn=sqlite3.connect(db,timeout=5)
  conn.execute("INSERT INTO employees(id,name,email) VALUES(1,'Tester','tester@example.test')")
  conn.execute("INSERT INTO cards(uid,employee_id) VALUES('AABBCCDD',1)")
  conn.execute("INSERT INTO schedule_profiles(employee_id,pay_type,start_time,end_time,weekday_mask) VALUES(1,'salary','08:00','17:00',127)")
  conn.commit()
  def swipe(label,t):
   stamp=datetime.datetime.fromisoformat(t).replace(tzinfo=ZoneInfo('America/Edmonton')).astimezone(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
   req=urllib.request.Request(f'http://127.0.0.1:{port}/api/v1/swipes',data=json.dumps({'event_id':label,'clock_id':'one','card_uid':'AABBCCDD','swiped_at':stamp}).encode(),headers={'X-Device-Key':key,'Content-Type':'application/json'})
   with urllib.request.urlopen(req,timeout=3) as r:assert r.status==200
  # Normal day and lunch: no second late alert
  for name,t in [('in1','2026-10-12T08:07:00'),('out1','2026-10-12T12:00:00'),('in2','2026-10-12T12:45:00'),('out2','2026-10-12T17:00:00'),('dup','2026-10-12T17:00:03')]:swipe(name,t)
  vals=dict(conn.execute('SELECT event_id,late_minutes FROM attendance_events'))
  assert vals['in1']==7 and vals['in2']==0,vals
  assert conn.execute("SELECT exception FROM attendance_events WHERE event_id='dup'").fetchone()[0]=='duplicate_tap'
  assert conn.execute("SELECT COUNT(*) FROM email_queue WHERE event_id LIKE 'late:%'").fetchone()[0]==1
  # New day after missed OUT: mark old IN and evaluate new one
  swipe('in3','2026-10-13T08:00:00')
  swipe('in4','2026-10-14T08:20:00')
  assert conn.execute("SELECT exception FROM attendance_events WHERE event_id='in3'").fetchone()[0]=='missing_out'
  assert conn.execute("SELECT late_minutes,exception FROM attendance_events WHERE event_id='in4'").fetchone()==(20,'')
  print('PASS lunch return, rapid duplicate, missed-out attribution, next-day lateness')
 finally:
  proc.terminate();proc.communicate(timeout=3)
