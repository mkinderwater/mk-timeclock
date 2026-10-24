#!/usr/bin/env python3
import json,os,sqlite3,subprocess,tempfile,time,urllib.request,urllib.error,urllib.parse,datetime
from zoneinfo import ZoneInfo
from pathlib import Path
base=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
 db=Path(tmp)/'test.db';port=18892;key='test-device-key-123456789'
 proc=subprocess.Popen([str(base/'timeclock-server'),str(db),str(port)],env={**os.environ,'DEVICE_KEY':key,'TZ':'America/Edmonton'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
 try:
  for i in range(80):
   try:urllib.request.urlopen(f'http://127.0.0.1:{port}/health',timeout=.5);break
   except Exception:time.sleep(.1)
  else:raise AssertionError('server unavailable')
  conn=sqlite3.connect(db)
  conn.execute("INSERT INTO employees(id,name,email,active) VALUES(1,'Holiday Test','test@example.test',1)")
  conn.execute("INSERT INTO employees(id,name,email,active) VALUES(2,'Manager','manager@example.test',1)")
  conn.execute("INSERT INTO employee_roles(employee_id,role) VALUES(2,'manager')")
  conn.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(2,'hash')")
  conn.execute("INSERT INTO manager_cards(uid,manager_id,active) VALUES('4903305B',2,1)")
  conn.execute("INSERT INTO cards(uid,employee_id,active) VALUES('01020304',1,1)")
  conn.execute("INSERT INTO schedule_profiles(employee_id,pay_type,start_time,end_time,weekday_mask) VALUES(1,'salary','08:00','17:00',127)")
  conn.execute("INSERT INTO login_sessions VALUES('"+'f'*64+"',2,CAST(strftime('%s','now') AS INTEGER)+600)")
  conn.commit()
  def call(path,data=None,logged=True):
   headers={"Content-Type":"application/x-www-form-urlencoded"}
   if logged:headers['Cookie']='tc_session='+'f'*64
   req=urllib.request.Request(f'http://127.0.0.1:{port}{path}',data=urllib.parse.urlencode(data).encode() if data is not None else None,headers=headers)
   try:
    with urllib.request.urlopen(req,timeout=4) as r:return r.status,r.read().decode()
   except urllib.error.HTTPError as e:return e.code,e.read().decode()
  assert 'Sign in' in call('/system',logged=False)[1]
  assert call('/api/v1/admin/holidays',logged=False)[0]==401
  assert call('/api/v1/admin/system/arm',dict(action='reboot'),logged=False)[0]==401
  assert call('/holidays')[0]==200
  assert call('/system')[0]==200
  status,body=call('/api/v1/admin/holidays',dict(skip=1,calendar='2026-10-12,Test Holiday\n2026-12-25,Christmas Day'))
  assert status==200,(status,body)
  data=json.loads(call('/api/v1/admin/holidays')[1]);assert len(data['days'])==2 and data['skip_alerts']
  assert call('/api/v1/admin/holidays',dict(skip=1,calendar='2026-02-30,Invalid'))[0]==422
  assert len(json.loads(call('/api/v1/admin/holidays')[1])['days'])==2
  stamp=datetime.datetime(2026,10,12,8,7,tzinfo=ZoneInfo('America/Edmonton')).astimezone(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
  payload=json.dumps({'event_id':'holiday1','clock_id':'TEST','card_uid':'01020304','swiped_at':stamp}).encode()
  req=urllib.request.Request(f'http://127.0.0.1:{port}/api/v1/swipes',data=payload,headers={'X-Device-Key':key,'Content-Type':'application/json'})
  with urllib.request.urlopen(req,timeout=5) as r:assert r.status==200
  assert conn.execute("SELECT late_minutes FROM attendance_events WHERE event_id='holiday1'").fetchone()==(7,)
  assert conn.execute("SELECT count(*) FROM email_queue WHERE event_id LIKE 'late:holiday1:%'").fetchone()==(0,)
  st,b=call('/api/v1/admin/system/arm',dict(action='reboot'));assert st==200,(st,b)
  token=json.loads(b)['token']
  assert conn.execute("SELECT scan_mode FROM manager_scan WHERE id=1").fetchone()==('approval',)
  assert call('/api/v1/admin/system/arm',dict(action='shutdown'))[0]==409
  assert call('/api/v1/admin/system/status',dict(token='0'*32))[0]==409
  # Do not supply manager scan here: test won't request actual reboot.
  print('PASS manager-protected holidays, invalid-date atomic rollback, holiday suppresses emails but records lateness, system approval exclusively armed and token checked')
 finally:
  proc.terminate();proc.wait(timeout=5)
