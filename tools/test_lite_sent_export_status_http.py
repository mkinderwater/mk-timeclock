"""Sent payroll snapshots are historical, not recalculated in the periods listing."""
import datetime as dt
import json, os, socket, sqlite3, subprocess, tempfile, time, urllib.request
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='lite-status-') as tmp:
 dbpath=Path(tmp)/'db.sqlite'
 with socket.socket() as sock:
  sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
 proc=subprocess.Popen([str(root/'timeclock-server'),str(dbpath),str(port)],cwd=root,env={**os.environ,'DEVICE_KEY':'test-device-key-12345'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
 try:
  base=f'http://127.0.0.1:{port}'
  for _ in range(100):
   try:urllib.request.urlopen(base+'/health',timeout=.2);break
   except Exception:time.sleep(.04)
  else:raise AssertionError('Server unavailable')
  with sqlite3.connect(dbpath) as db:
   db.execute("INSERT INTO employees(id,name,email) VALUES(1,'Manager','mgr@example.test')")
   db.execute("INSERT INTO employee_roles VALUES(1,'manager')")
   db.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
   db.execute("INSERT INTO manager_cards(uid,manager_id,active) VALUES('A1B2C3D4',1,1)")
   db.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+600)",('d'*64,))
   day=dt.date(2025,1,1)
   for i in range(26):
    starts=(day+dt.timedelta(days=14*i)).isoformat();ends=(day+dt.timedelta(days=14*i+13)).isoformat()
    db.execute("INSERT INTO pay_periods(starts,ends,status) VALUES(?,?,'closed')",(starts,ends))
    pid=db.execute('SELECT last_insert_rowid()').fetchone()[0]
    db.execute("INSERT INTO lite_period_snapshots(period_id,revision,starts,ends,csv) VALUES(?,1,?,?,'old snapshot')",(pid,starts,ends))
    if i<25:
     db.execute("INSERT INTO email_queue(recipient,subject,body,status,event_id) VALUES('payroll@example.test','Payroll','CSV','sent',?)",(f'payroll-period:{pid}',))
   db.execute("UPDATE employees SET name='Manager Renamed' WHERE id=1")
  req=urllib.request.Request(base+'/api/v1/admin/periods',headers={'Cookie':'tc_session='+'d'*64})
  start=time.monotonic()
  
  try:
   with urllib.request.urlopen(req,timeout=8) as r:data=json.load(r)
  except Exception as e:
   print("HTTP FAILURE",e, getattr(e,"read",lambda:b"")(),proc.poll(),flush=True);raise
  elapsed=time.monotonic()-start
  tested=[p for p in data['periods'] if p['starts']>='2025-01-01' and p['starts']<'2026-01-01']
  assert len(tested)==26,len(tested)
  sent=tested[1:]
  assert all('export_stale' not in p and 'export_check_failed' not in p for p in sent),sent
  assert tested[0]['export_stale'] is True,tested[0]
  assert elapsed<3.0,elapsed
  print(f'PASS: 25 sent periods skipped; unsent period still stale; list {elapsed:.3f}s')
 finally:
  proc.terminate();proc.communicate(timeout=5)
