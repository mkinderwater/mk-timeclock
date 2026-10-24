#!/usr/bin/env python3
"""Actual server startup recovers an old period only when leave exists."""
import datetime as dt,os,sqlite3,socket,subprocess,tempfile,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as d:
 db=str(Path(d)/'clock.db')
 def boot():
  sk=socket.socket();sk.bind(('127.0.0.1',0));port=sk.getsockname()[1];sk.close()
  p=subprocess.Popen([str(ROOT/'timeclock-server'),db,str(port)],env={**os.environ,'DEVICE_KEY':'7'*32},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
  try:
   for i in range(100):
    if p.poll() is not None:raise AssertionError('server exited: '+p.stderr.read().decode())
    try:
     with sqlite3.connect(db) as c:
      if c.execute('SELECT count(*) FROM pay_periods').fetchone()[0]:break
    except sqlite3.Error:pass
    time.sleep(.05)
   else:raise AssertionError('startup did not create periods')
   time.sleep(.6)
  finally:p.terminate();p.wait(timeout=5);p.stderr.close()
 boot()
 today=dt.date.today();monthstart=today.replace(day=1);prior_end=monthstart-dt.timedelta(days=1)
 past=prior_end.replace(day=5)
 with sqlite3.connect(db) as c:
  assert not c.execute('SELECT 1 FROM pay_periods WHERE ? BETWEEN starts AND ends',(str(past),)).fetchone()
  c.execute("INSERT INTO employees(name,email) VALUES('Employee','employee@example.test')")
  c.execute("INSERT INTO employees(name,email) VALUES('Manager','manager@example.test')")
  event_day=past+dt.timedelta(days=2)
  c.execute("INSERT INTO attendance_absences(employee_id,day,reason,manager_id) VALUES(1,?,'Sick',2)",(str(past),))
  c.execute("INSERT INTO swipes(event_id,clock_id,card_uid,employee_id,swiped_at) VALUES('off001','test-secondary','CARD01',1,?)",(str(event_day)+'T08:00:00Z',))
  c.execute("INSERT INTO attendance_events(event_id,employee_id,kind) VALUES('off001',1,'IN')")
 boot()
 with sqlite3.connect(db) as c:
  matched=c.execute('SELECT id,starts,ends,status FROM pay_periods WHERE ? BETWEEN starts AND ends',(str(past),)).fetchall()
  assert len(matched)==1 and matched[0][3]=='open', matched
  assert c.execute('SELECT count(*) FROM pay_periods WHERE ends<?',(str(today),)).fetchone()[0]==1
  assert not c.execute('SELECT 1 FROM payroll_manual_requests WHERE period_id=?',(matched[0][0],)).fetchone()
  assert c.execute("SELECT count(*) FROM effective_punches WHERE date(at,'localtime') BETWEEN ? AND ?",(matched[0][1],matched[0][2])).fetchone()[0]==1
 print('PASS: delayed historical punch and leave restore one open period, no empty backfill, no approval or send')
