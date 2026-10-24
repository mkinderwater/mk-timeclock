#!/usr/bin/env python3
import os,sqlite3,tempfile,subprocess,time,json,urllib.request,urllib.error,urllib.parse
from pathlib import Path
base=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='tc043-') as td:
 db=Path(td)/'tc.db'; port=19343; env=dict(os.environ,DEVICE_KEY='test-device-key-123456789',TZ='America/Edmonton')
 proc=subprocess.Popen([str(base/'timeclock-server'),str(db),str(port)],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
 try:
  time.sleep(.25);assert proc.poll() is None,proc.stderr.read().decode()
  c=sqlite3.connect(db)
  c.execute("INSERT INTO employees(id,name) VALUES(1,'Manager'),(2,'Worker')")
  c.execute("INSERT INTO employee_roles VALUES(1,'manager')")
  c.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x'),(2,'x')")
  c.execute("INSERT INTO login_sessions VALUES(?,2,CAST(strftime('%s','now') AS INTEGER)+3600)",('e'*64,))
  c.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+3600)",('f'*64,))
  c.execute("INSERT INTO swipes(event_id,clock_id,card_uid,employee_id,swiped_at) VALUES('A','clock','ABC',2,'2026-10-09T14:00:00Z'),('B','clock','ABC',2,'2026-10-09T22:00:00Z')")
  c.execute("INSERT INTO attendance_events(event_id,employee_id,kind) VALUES('A',2,'IN'),('B',2,'OUT')")
  c.commit()
  def request(path,data=None,employee=False):
   req=urllib.request.Request(f'http://127.0.0.1:{port}'+path,headers={'Cookie':'tc_session='+('e' if employee else 'f')*64},data=urllib.parse.urlencode(data).encode() if data is not None else None)
   try:
    with urllib.request.urlopen(req,timeout=4) as r:return r.status,json.loads(r.read())
   except urllib.error.HTTPError as e:return e.code,json.loads(e.read())
  assert request('/api/v1/admin/presence')[1]['count']==0
  assert request('/api/v1/admin/punches',employee=True)[0]==401
  change={'action':'change','employee_id':'2','event_id':'B','at':'2026-10-09T21:00:00Z','kind':'IN','reason':'Manager confirmed error'}
  assert request('/api/v1/admin/punches',change)[0]==200
  assert request('/api/v1/admin/presence')[1]['count']==1
  assert c.execute("SELECT kind FROM attendance_events WHERE event_id='B'").fetchone()[0]=='OUT'
  assert c.execute("SELECT count(*) FROM punch_correction_history").fetchone()[0]==1
  change['kind']='OUT';assert request('/api/v1/admin/punches',change)[0]==200
  assert request('/api/v1/admin/presence')[1]['count']==0
  assert request('/api/v1/admin/punches',employee=True)[0]==401
  assert c.execute('select count(*) from punch_correction_history').fetchone()[0]==2
  # A missing swipe inserted between raw IN/OUT reverses the later calculated direction.
  c.execute("INSERT INTO employees(id,name) VALUES(3,'Second Worker')")
  c.execute("INSERT INTO swipes(event_id,clock_id,card_uid,employee_id,swiped_at) VALUES('C','clock','DEF',3,'2026-10-10T14:00:00Z'),('D','clock','DEF',3,'2026-10-10T20:00:00Z'),('E','clock','DEF',3,'2026-10-10T22:00:00Z')")
  c.execute("INSERT INTO attendance_events(event_id,employee_id,kind) VALUES('C',3,'IN'),('D',3,'OUT'),('E',3,'IN')")
  c.commit()
  add={'action':'add','employee_id':'3','event_id':'','at':'2026-10-10T16:00:00Z','kind':'OUT','reason':'Verified missing lunch exit'}
  assert request('/api/v1/admin/punches',add)[0]==200
  rows=c.execute("SELECT id,kind FROM effective_punches WHERE employee_id=3 ORDER BY at,id").fetchall()
  assert [v[1] for v in rows]==['IN','OUT','IN','OUT'],rows
  assert c.execute("SELECT kind FROM attendance_events WHERE event_id='D'").fetchone()[0]=='OUT'
  # Voiding a raw record must also recompute every subsequent direction.
  void={'action':'void','employee_id':'3','event_id':'D','at':'2026-10-10T20:00:00Z','kind':'OUT','reason':'Duplicate swipe confirmed'}
  assert request('/api/v1/admin/punches',void)[0]==200
  rows=c.execute("SELECT id,kind FROM effective_punches WHERE employee_id=3 ORDER BY at,id").fetchall()
  assert [v[1] for v in rows]==['IN','OUT','IN'],rows
  create={'action':'create','starts':'2026-10-09','ends':'2026-10-09','employee_id':'0','reason':'Weekly payroll closing'}
  assert request('/api/v1/admin/periods',create)[0]==200
  close=dict(create,action='close')
  assert request('/api/v1/admin/periods',close)[0]==409
  sign=dict(create,action='sign',employee_id='2')
  assert request('/api/v1/admin/periods',sign)[0]==409
  period_id=c.execute('SELECT id FROM pay_periods').fetchone()[0]
  assert request('/api/v1/me/sign',{'period_id':period_id,'ack':'I confirm I reviewed my hours'},True)[0]==200
  assert request('/api/v1/admin/periods',sign)[0]==200
  # Editing a reviewed period forces new employee and manager attestations.
  assert request('/api/v1/admin/punches',{'action':'change','employee_id':'2','event_id':'B','at':'2026-10-09T22:00:00Z','kind':'OUT','reason':'Verified departure time'})[0]==200
  assert c.execute("SELECT count(*) FROM period_signoffs").fetchone()[0]==0
  assert request('/api/v1/admin/periods',close)[0]==409
  assert request('/api/v1/me/sign',{'period_id':period_id,'ack':'I confirm I reviewed my hours'},True)[0]==200
  assert request('/api/v1/admin/periods',sign)[0]==200
  assert request('/api/v1/admin/periods',close)[0]==200
  assert request('/api/v1/admin/punches',change)[0]==409
  assert c.execute("SELECT status FROM pay_periods").fetchone()[0]=='closed'
  print('PASS correction revision history, raw immutability, presence, employee signoff, manager approval, period closing, correction block')
 finally:
  proc.terminate();proc.wait(timeout=4)
