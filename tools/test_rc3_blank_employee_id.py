"""Blank company IDs allocate a free clock ID without moving existing records."""
import json,os,socket,sqlite3,subprocess,tempfile,time,urllib.request,urllib.parse,urllib.error
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
 path=Path(tmp)/'clock.db'
 with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
 proc=subprocess.Popen([str(root/'timeclock-server'),str(path),str(port)],env={**os.environ,'DEVICE_KEY':'test-device-key-12345','TZ':'America/Edmonton'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
 try:
  base=f'http://127.0.0.1:{port}'
  for _ in range(100):
   try:urllib.request.urlopen(base+'/health',timeout=.2).close();break
   except OSError:time.sleep(.04)
  with sqlite3.connect(path) as db:
   db.execute("INSERT INTO employees(id,name,employee_number) VALUES(1,'Manager','4'),(2,'Ian','5'),(3,'Sarah','6')")
   db.execute("INSERT INTO employee_roles VALUES(1,'manager')")
   db.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
   db.execute("INSERT INTO manager_cards VALUES('A1B2C3D4',1,1)")
   db.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+3600)",('d'*64,))
   db.execute('UPDATE review_send_settings SET enabled=0')
  def save(number='',uid='',expected=200,omit=False):
   fields=dict(id=0,first_name='New',last_name='Employee',position='Technician',email='',phone='',active=1,manager=0,uid=uid,late_email_enabled=0,auto_review_enabled=0,attendance_mode='vacation_only')
   if not omit:fields['employee_number']=number
   req=urllib.request.Request(base+'/api/v1/admin/employees/save',data=urllib.parse.urlencode(fields).encode(),headers={'Cookie':'tc_session='+'d'*64})
   try:
    with urllib.request.urlopen(req,timeout=10) as r:assert r.status==expected;return json.load(r)
   except urllib.error.HTTPError as e:assert e.code==expected,(e.code,e.read())
  save(uid='DEADBEEF')
  with sqlite3.connect(path) as db:
   assert db.execute("SELECT employee_id FROM cards WHERE uid='DEADBEEF'").fetchone()[0]==7
   assert db.execute('SELECT id,employee_number FROM employees ORDER BY id').fetchall()==[(1,'4'),(2,'5'),(3,'6'),(7,'')]
   assert db.execute('SELECT mode FROM employee_attendance_modes WHERE employee_id=7').fetchall()==[('vacation_only',)]
  save(omit=True) # No field is also a blank ID; normal allocation resumes at 8.
  with sqlite3.connect(path) as db:
   assert db.execute('SELECT max(id) FROM employees').fetchone()[0]==8
   db.execute("UPDATE employees SET employee_number='0009' WHERE id=1")
  save('   ') # 0009 is distinct text, not a reservation of fallback 9.
  save('5',expected=409)
  with sqlite3.connect(path) as db:
   assert db.execute('SELECT max(id) FROM employees').fetchone()[0]==9
   assert db.execute('SELECT count(*) FROM employees').fetchone()[0]==6
   assert db.execute('SELECT employee_number FROM employees WHERE id=9').fetchone()[0]==''
   assert db.execute('SELECT count(*) FROM employee_change_audit').fetchone()[0]==3
   assert db.execute("SELECT employee_id FROM card_ownership_history WHERE uid='DEADBEEF'").fetchone()[0]==7
   assert db.execute('PRAGMA integrity_check').fetchone()[0]=='ok'
  # Long consecutive reservations are handled in one ordered pass.
  with sqlite3.connect(path) as db:
   for eid in range(10,1210):db.execute("INSERT INTO employees(id,name) VALUES(?,'Bulk reservation')",(eid,))
   ids=[r[0] for r in db.execute('SELECT id FROM employees ORDER BY id')]
   first=ids[-1]+1
   for offset,eid in enumerate(ids):db.execute('UPDATE employees SET employee_number=? WHERE id=?',(str(first+offset),eid))
  save()
  with sqlite3.connect(path) as db:assert db.execute('SELECT max(id) FROM employees').fetchone()[0]==first+len(ids)
  print('PASS blank IDs: skip consecutive company-ID reservations; omitted/whitespace blank; leading-zero distinction; unchanged existing IDs/card links; explicit duplicates rejected atomically; initial vacation-only history correct')
 finally:proc.terminate();proc.communicate(timeout=5)
