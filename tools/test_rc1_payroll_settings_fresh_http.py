#!/usr/bin/env python3
"""First payroll settings save succeeds after manager-card approval."""
import json,os,socket,sqlite3,subprocess,tempfile,time,urllib.request,urllib.parse,urllib.error
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as d:
 db=Path(d)/'clock.db';sk=socket.socket();sk.bind(('127.0.0.1',0));port=sk.getsockname()[1];sk.close()
 proc=subprocess.Popen([str(root/'timeclock-server'),str(db),str(port)],env={**os.environ,'DEVICE_KEY':'test-device-key-12345'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
 try:
  for _ in range(100):
   try:
    with urllib.request.urlopen(f'http://127.0.0.1:{port}/health',timeout=.15):break
   except Exception:time.sleep(.05)
  else:raise AssertionError('server failed startup')
  with sqlite3.connect(db) as c:
   assert c.execute('SELECT count(*) FROM payroll_settings').fetchone()[0]==0
   c.execute("INSERT INTO employees(id,name,email) VALUES(1,'Manager','mgr@example.test')")
   c.execute("INSERT INTO employee_roles(employee_id,role) VALUES(1,'manager')")
   c.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
   c.execute("INSERT INTO manager_cards(uid,manager_id,active) VALUES('A1B2C3D4',1,1)")
   c.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+600)",('d'*64,))
  def req(path,data):
   body=urllib.parse.urlencode(data).encode()
   hdr={'Cookie':'tc_session='+'d'*64,'Content-Type':'application/x-www-form-urlencoded'}
   try:
    with urllib.request.urlopen(urllib.request.Request(f'http://127.0.0.1:{port}'+path,data=body,headers=hdr),timeout=5) as r:return r.status,json.load(r)
   except urllib.error.HTTPError as e:return e.code,e.read().decode()
  settings={'anchor':'2026-10-15','before':'0','recipient':'payroll@example.test','enabled':'1'}
  status,ans=req('/api/v1/admin/payroll/arm',settings);assert status==200,(status,ans)
  with sqlite3.connect(db) as c:
   c.execute("UPDATE manager_scan SET uid='A1B2C3D4',scanned_at=armed_at+1 WHERE id=1")
  status,ans=req('/api/v1/admin/payroll/status',{'token':ans['token']});assert status==200 and ans['status']=='saved',(status,ans)
  with sqlite3.connect(db) as c:
   row=c.execute('SELECT pc.cycle,ps.anchor_payday,ps.lead_days,ps.recipient,ps.enabled FROM payroll_settings ps JOIN period_cycle_settings pc ON pc.id=ps.id WHERE ps.id=1').fetchone()
   assert row==('semi_monthly','2026-10-15',0,'payroll@example.test',1),row
  print('PASS: first payroll save persists with manager card and allows zero days before payday')
 finally:
  proc.terminate();proc.communicate(timeout=5)
