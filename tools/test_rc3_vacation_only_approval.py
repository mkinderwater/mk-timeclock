"""Vacation-only records need no employee or manager signoff, including old overrides."""
import csv,ctypes,ctypes.util,io,json,os,socket,sqlite3,subprocess,tempfile,time,urllib.error,urllib.parse,urllib.request
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
 dbpath=Path(tmp)/'clock.db'
 with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
 proc=subprocess.Popen([str(root/'timeclock-server'),str(dbpath),str(port)],env={**os.environ,'TZ':'America/Edmonton','DEVICE_KEY':'test-device-key-12345'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
 try:
  base=f'http://127.0.0.1:{port}'
  for _ in range(100):
   try:urllib.request.urlopen(base+'/health',timeout=.2).close();break
   except OSError:time.sleep(.04)
  crypt=ctypes.CDLL(ctypes.util.find_library('crypt'));crypt.crypt.argtypes=[ctypes.c_char_p,ctypes.c_char_p];crypt.crypt.restype=ctypes.c_char_p
  hashed=crypt.crypt(b'e'*64,b'$6$rounds=5000$payreview2026$').decode()
  with sqlite3.connect(dbpath) as db:
   db.execute("INSERT INTO employees(id,name,email,attendance_mode) VALUES(1,'Manager','manager@example.test','punch_required'),(2,'Sarah Chen','sarah@example.test','vacation_only'),(3,'Ian May','ian@example.test','punch_required')")
   db.execute("INSERT INTO employee_roles VALUES(1,'manager')")
   db.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
   db.execute("INSERT INTO manager_cards VALUES('A1B2C3D4',1,1)")
   db.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+3600)",('d'*64,))
   db.execute('UPDATE review_send_settings SET enabled=0')
   pid=db.execute("INSERT INTO pay_periods(starts,ends) VALUES('2026-09-01','2026-09-15') RETURNING id").fetchone()[0]
   db.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status) VALUES(2,'vacation','2026-09-02','2026-09-02','approved')")
   pending=db.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status) VALUES(2,'vacation','2026-09-07','2026-09-07','pending') RETURNING id").fetchone()[0]
   db.execute("INSERT INTO review_links VALUES(2,?,?,CAST(strftime('%s','now') AS INTEGER)+3600,1,CAST(strftime('%s','now') AS INTEGER),0)",(pid,hashed))
   for kind,hour in [('IN','15:00'),('OUT','16:00')]:db.execute("INSERT INTO punch_corrections(employee_id,manual_id,action,effective_at,kind,reason,manager_id) VALUES(3,?,'add',?,?,'Verified attendance',1)",(kind,'2026-09-03T'+hour+':00Z',kind))
   # Keep a pre-upgrade stale override as audit history, without letting it block close.
   db.execute("INSERT INTO period_signoffs(period_id,employee_id,role,signed_by,unconfirmed,reason,leave_fingerprint) VALUES(?,2,'manager',1,1,'Old leave override','stale')",(pid,))
   schema=db.execute("SELECT type,name,sql FROM sqlite_master ORDER BY type,name").fetchall()
  def call(route,form=None,expected=200,manager=True):
   headers={'Cookie':'tc_session='+'d'*64} if manager else {}
   data=urllib.parse.urlencode(form).encode() if form is not None else None
   if data is not None:headers['Content-Type']='application/x-www-form-urlencoded'
   try:
    with urllib.request.urlopen(urllib.request.Request(base+route,data=data,headers=headers),timeout=15) as r:assert r.status==expected;return json.load(r)
   except urllib.error.HTTPError as e:body=e.read();assert e.code==expected,(e.code,body);return json.loads(body)
  period=dict(starts='2026-09-01',ends='2026-09-15',reason='Payroll records reviewed',employee_id=0)
  people=call('/api/v1/admin/periods/people?period_id='+str(pid))['people'];assert {p['id']:p['approval_required'] for p in people}=={2:False,3:True}
  detail=call('/review/'+'e'*64+'/detail',manager=False);assert detail['vacation_only'] and len(detail['context'])==1
  call('/review/'+'e'*64+'/approve',{},409,False)
  call('/api/v1/admin/periods',dict(period,action='sign',employee_id=2),409)
  call('/api/v1/admin/periods',dict(period,action='close'),409) # Ian still requires approval.
  call('/api/v1/admin/periods',dict(period,action='sign',employee_id=3))
  call('/api/v1/admin/periods',dict(period,action='close'))
  call('/api/v1/admin/periods/exports',{'period_id':pid})
  with sqlite3.connect(dbpath) as db:
   original=db.execute('SELECT csv FROM lite_period_snapshots WHERE period_id=? AND revision=1',(pid,)).fetchone()[0]
   rows=list(csv.DictReader(io.StringIO(original)))
   assert any(r['Employee ID']=='2' and r['Status'].startswith('Approved vacation') for r in rows)
   assert not any(r['Employee ID']=='2' and 'override' in r['Status'].lower() for r in rows)
   assert any(r['Employee ID']=='3' and 'override' in r['Status'].lower() for r in rows)
   assert db.execute('SELECT status FROM timeoff_requests WHERE id=?',(pending,)).fetchone()[0]=='pending'
  # Vacation decisions remain explicit; changes still require a revised export.
  call('/api/v1/admin/time-off/decide',dict(id=pending,action='approved',note='Approved vacation'))
  state=next(p for p in call('/api/v1/admin/periods')['periods'] if p['id']==pid);assert state['new_export_required'] and state['status']=='closed'
  call('/api/v1/admin/periods/exports',{'period_id':pid})
  with sqlite3.connect(dbpath) as db:
   assert db.execute('SELECT csv FROM lite_period_snapshots WHERE period_id=? AND revision=1',(pid,)).fetchone()[0]==original
   assert db.execute('SELECT count(*) FROM period_signoffs WHERE employee_id=2').fetchone()[0]==1
   assert db.execute('SELECT count(*) FROM period_approval_audit WHERE employee_id=2').fetchone()[0]==0
   assert db.execute('SELECT status FROM timeoff_requests WHERE id=?',(pending,)).fetchone()[0]=='approved'
   assert db.execute("SELECT type,name,sql FROM sqlite_master ORDER BY type,name").fetchall()==schema
  # A period containing vacation only closes with no signoffs at all.
  with sqlite3.connect(dbpath) as db:
   db.execute("INSERT INTO pay_periods(starts,ends) VALUES('2026-10-01','2026-10-15')")
   db.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status) VALUES(2,'vacation','2026-10-02','2026-10-02','approved')")
  call('/api/v1/admin/periods',dict(period,starts='2026-10-01',ends='2026-10-15',action='close'))
  print('PASS vacation-only: no signoff or override; mixed-period punch approval retained; old override ignored/preserved; leave decisions retained; close/export; revised exports and saved CSV immutable; schema unchanged')
 finally:proc.terminate();proc.communicate(timeout=5)
