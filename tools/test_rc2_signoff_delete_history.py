"""Deleting approvals on a correction must retain the superseded approval."""
import os, socket, sqlite3, subprocess, tempfile, time, urllib.request
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='rc2-signoff-delete-') as d:
 dbpath=Path(d)/'db.sqlite'
 with socket.socket() as sock:
  sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
 server=subprocess.Popen([str(root/'timeclock-server'),str(dbpath),str(port)],env={**os.environ,'DEVICE_KEY':'0123456789abcdef0123456789abcdef'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
 try:
  for _ in range(100):
   try:
    urllib.request.urlopen(f'http://127.0.0.1:{port}/health',timeout=.3).close();break
   except OSError:time.sleep(.05)
  else:raise AssertionError('server not ready: '+server.stderr.read().decode(errors='replace')[:1600])
  with sqlite3.connect(dbpath) as db:
   db.execute("INSERT INTO employees(id,name) VALUES(1,'Test Employee')")
   db.execute("INSERT INTO pay_periods(starts,ends) VALUES('2026-10-01','2026-10-15')")
   pid=db.execute('SELECT id FROM pay_periods WHERE starts=?',('2026-10-01',)).fetchone()[0]
   db.execute("INSERT INTO period_signoffs(period_id,employee_id,role,signed_by,punch_count,approved_seconds,leave_fingerprint,reason) VALUES(?,1,'employee',1,2,3600,'abc123','Reviewed')",(pid,))
   db.execute('DELETE FROM period_signoffs WHERE period_id=? AND employee_id=1',(pid,))
   row=db.execute('SELECT role,punch_count,approved_seconds,leave_fingerprint,reason FROM period_signoff_history WHERE period_id=?',(pid,)).fetchone()
   assert row==('employee',2,3600,'abc123','Reviewed'),row
  print('PASS deleted signoff retained in history')
 finally:
  server.terminate();server.communicate(timeout=5)
