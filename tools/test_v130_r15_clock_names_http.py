"""HTTP integration: rename dispatch, UTF-8, JSON, and Master revoke safety."""
import json, os, sqlite3, subprocess, tempfile, time, urllib.error, urllib.parse, urllib.request
from pathlib import Path
root=Path(__file__).resolve().parents[1]
key='primary-unit-test-secret-key'
port=19715
with tempfile.TemporaryDirectory() as tmp:
 db=Path(tmp)/'server.db'
 proc=subprocess.Popen([str(root/'timeclock-server'),str(db),str(port)],env={**os.environ,'DEVICE_KEY':key},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
 try:
  for _ in range(80):
   if db.exists():
    try:
     with urllib.request.urlopen(f'http://127.0.0.1:{port}/health',timeout=.3):break
    except Exception:pass
   time.sleep(.05)
  else:raise AssertionError('server did not start')
  with sqlite3.connect(db) as conn:
   conn.execute("INSERT INTO registered_clocks(clock_id,device_key,active) VALUES('CLOCK-001',?,1)",(key,))
   conn.execute("INSERT INTO registered_clocks(clock_id,device_key,active) VALUES('CLOCK-002','slave-key',1)")
   conn.execute("INSERT INTO employees(id,name,email) VALUES(1,'Manager','mgr@example.test')")
   conn.execute("INSERT INTO employee_roles VALUES(1,'manager')")
   conn.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
   conn.execute("INSERT INTO manager_cards(uid,manager_id,active) VALUES('A1B2C3D4',1,1)")
   conn.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+600)",('d'*64,))
  def call(path,data=None):
   headers={'Cookie':'tc_session='+'d'*64}
   if data is not None:
    data=urllib.parse.urlencode(data).encode();headers['Content-Type']='application/x-www-form-urlencoded'
   req=urllib.request.Request(f'http://127.0.0.1:{port}'+path,data=data,headers=headers)
   try:
    with urllib.request.urlopen(req,timeout=4) as r:return r.status,r.read().decode()
   except urllib.error.HTTPError as e:return e.code,e.read().decode()
  code,body=call('/api/v1/admin/clock-name',{'clock_id':'CLOCK-002','name':'Front Door'})
  assert code==200,(code,body)
  code,body=call('/api/v1/admin/clock-name',{'clock_id':'CLOCK-002','name':'🚪'*16})
  assert code==200,(code,body)
  code,body=call('/api/v1/admin/adoption/list')
  assert code==200,(code,body)
  j=json.loads(body)
  master=next(x for x in j['clocks'] if x['clock_id']=='CLOCK-001')
  slave=next(x for x in j['clocks'] if x['clock_id']=='CLOCK-002')
  assert master['is_master'] is True and slave['is_master'] is False
  assert slave['name']=='🚪'*16
  code,body=call('/api/v1/admin/clock-name',{'clock_id':'CLOCK-002','name':'Bayÿ'})
  assert code==200,(code,body)
  # Malformed octet is decoded by form parser and must be rejected before JSON serialization.
  headers={'Cookie':'tc_session='+'d'*64,'Content-Type':'application/x-www-form-urlencoded'}
  req=urllib.request.Request(f'http://127.0.0.1:{port}/api/v1/admin/clock-name',data=b'clock_id=CLOCK-002&name=Bay%FF',headers=headers)
  try:
   urllib.request.urlopen(req,timeout=4)
   raise AssertionError('malformed UTF8 accepted')
  except urllib.error.HTTPError as e:assert e.code==400,e.code
  code,body=call('/api/v1/admin/adoption/remove',{'clock_id':'CLOCK-001'})
  assert code==409,(code,body)
  code,body=call('/api/v1/admin/adoption/remove',{'clock_id':'CLOCK-002'})
  assert code==200,(code,body)
  with sqlite3.connect(db) as conn:
   assert conn.execute("SELECT active FROM registered_clocks WHERE clock_id='CLOCK-001'").fetchone()[0]==1
  print('PASS: HTTP rename, UTF-8, JSON, Master revoke guard')
 finally:
  proc.terminate();proc.communicate(timeout=5)
