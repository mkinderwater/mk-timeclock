"""Mode changes cannot retroactively remove timesheet approvals or override notes."""
import csv,ctypes,ctypes.util,io,json,os,socket,sqlite3,subprocess,tempfile,time,urllib.request,urllib.parse,urllib.error
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
  crypt=ctypes.CDLL(ctypes.util.find_library('crypt'));crypt.crypt.argtypes=[ctypes.c_char_p,ctypes.c_char_p];crypt.crypt.restype=ctypes.c_char_p
  with sqlite3.connect(path) as db:
   for eid,mode in [(1,'punch_required'),(2,'punch_required'),(3,'vacation_only'),(4,'punch_required'),(5,'vacation_only'),(6,'vacation_only'),(7,'punch_required'),(8,'vacation_only'),(9,'vacation_only')]:
    db.execute("INSERT INTO employees(id,name,first_name,last_name,position,attendance_mode,created_at) VALUES(?,?,'Worker',?,'Tech',?,'2026-08-01T00:00:00Z')",(eid,'Worker '+str(eid),str(eid),mode))
   db.execute("INSERT INTO employee_roles VALUES(1,'manager')")
   db.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
   db.execute("INSERT INTO manager_cards VALUES('A1B2C3D4',1,1)")
   db.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+3600)",('d'*64,))
   db.execute('UPDATE review_send_settings SET enabled=0')
   pid=db.execute("INSERT INTO pay_periods(starts,ends) VALUES('2026-09-01','2026-09-15') RETURNING id").fetchone()[0]
   for eid in range(2,10):
    db.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status) VALUES(?,'vacation','2026-09-10','2026-09-10','approved')",(eid,))
   for eid,token in [(2,'e'*64),(3,'f'*64)]:
    hashed=crypt.crypt(token.encode(),b'$6$rounds=5000$payreview2026$').decode()
    db.execute("INSERT INTO review_links VALUES(?,?,?,CAST(strftime('%s','now') AS INTEGER)+3600,1,CAST(strftime('%s','now') AS INTEGER),0)",(eid,pid,hashed))
   for eid,events in [(2,[('2026-09-03T15:00:00Z','IN'),('2026-09-03T23:00:00Z','OUT'),('2026-09-04T15:00:00Z','IN'),('2026-09-05T00:00:00Z','OUT')]),(8,[('2026-09-03T15:00:00Z','IN'),('2026-09-03T16:00:00Z','OUT')])]:
    for i,(at,kind) in enumerate(events):db.execute("INSERT INTO punch_corrections(employee_id,manual_id,action,effective_at,kind,reason,manager_id) VALUES(?,?,'add',?,?,'Verified actual attendance',1)",(eid,f'{eid}-{i}',at,kind))
   db.execute('DELETE FROM employee_attendance_modes WHERE employee_id=9') # Missing history must fail closed.
  def call(route,fields=None,expected=200,auth=True):
   headers={'Cookie':'tc_session='+'d'*64} if auth else {}
   data=urllib.parse.urlencode(fields).encode() if fields is not None else None
   if data is not None:headers['Content-Type']='application/x-www-form-urlencoded'
   try:
    with urllib.request.urlopen(urllib.request.Request(base+route,data=data,headers=headers),timeout=15) as r:assert r.status==expected;return json.load(r)
   except urllib.error.HTTPError as e:b=e.read();assert e.code==expected,(e.code,b);return json.loads(b)
  period=dict(starts='2026-09-01',ends='2026-09-15',employee_id=0,reason='Verified payroll records')
  def mode(eid,mode):
   call('/api/v1/admin/employees/save',dict(id=eid,first_name='Worker',last_name=str(eid),position='Tech',email='',phone='',active=1,manager=0,uid='',late_email_enabled=0,auto_review_enabled=0,attendance_mode=mode))
  call('/api/v1/admin/periods',dict(period,action='close'),409)
  for eid,new in [(2,'vacation_only'),(3,'punch_required'),(4,'vacation_only'),(5,'punch_required'),(6,'punch_required'),(7,'vacation_only')]:mode(eid,new)
  with sqlite3.connect(path) as db:
   for eid,at in [(4,'2026-09-08T06:00:00Z'),(5,'2026-09-08T06:00:00Z'),(6,'2026-09-16T06:00:00Z'),(7,'2026-09-01T06:00:00Z')]:
    db.execute("UPDATE employee_attendance_modes SET effective_at=? WHERE employee_id=? AND effective_at<>'2026-08-01T00:00:00Z'",(at,eid))
  people={p['id']:p for p in call('/api/v1/admin/periods/people?period_id='+str(pid))['people']}
  assert {eid:p['approval_required'] for eid,p in people.items()}=={2:True,3:False,4:True,5:True,6:False,7:False,8:True,9:True}
  assert people[2]['current_seconds']==17*3600
  listing=next(p for p in call('/api/v1/admin/periods')['periods'] if p['id']==pid);assert listing['approval_total']==5 and listing['manager_signoffs']==0
  assert call('/review/'+'e'*64+'/detail',auth=False)['vacation_only'] is False
  assert call('/review/'+'f'*64+'/detail',auth=False)['vacation_only'] is True
  call('/review/'+'e'*64+'/approve',{},auth=False)
  call('/review/'+'f'*64+'/approve',{},409,False)
  call('/api/v1/admin/periods',dict(period,action='close'),409) # The 17 hours remain protected after switching.
  for eid in (2,4,5,8,9):call('/api/v1/admin/periods',dict(period,action='sign',employee_id=eid))
  call('/api/v1/admin/periods',dict(period,action='close'))
  call('/api/v1/admin/periods/exports',dict(period_id=pid))
  with sqlite3.connect(path) as db:
   original=db.execute('SELECT csv FROM lite_period_snapshots WHERE period_id=? AND revision=1',(pid,)).fetchone()[0]
   rows=list(csv.DictReader(io.StringIO(original)));assert sum(float(r['Worked hours'] or 0) for r in rows if r['Employee ID']=='2')==17
   assert any(r['Employee ID']=='8' and 'Manager override' in r['Status'] for r in rows)
   assert db.execute('SELECT count(*) FROM period_signoffs WHERE employee_id IN (3,6,7)').fetchone()[0]==0
  state=next(p for p in call('/api/v1/admin/periods')['periods'] if p['id']==pid);assert state['approval_total']==state['manager_signoffs']==5 and not state['changed_after_close']
  # A later change still invalidates historical approval and preserves saved revision 1.
  with sqlite3.connect(path) as db:
   for i,(at,kind) in enumerate([('2026-09-08T15:00:00Z','IN'),('2026-09-08T16:00:00Z','OUT')]):db.execute("INSERT INTO punch_corrections(employee_id,manual_id,action,effective_at,kind,reason,manager_id) VALUES(2,?,'add',?,?,'Later verified shift',1)",('extra'+str(i),at,kind))
  state=next(p for p in call('/api/v1/admin/periods')['periods'] if p['id']==pid);assert state['changed_after_close'] and state['new_export_required']
  call('/api/v1/admin/periods',dict(period,action='sign',employee_id=2))
  call('/api/v1/admin/periods/exports',dict(period_id=pid))
  with sqlite3.connect(path) as db:assert db.execute('SELECT csv FROM lite_period_snapshots WHERE period_id=? AND revision=1',(pid,)).fetchone()[0]==original
  print('PASS historical approval: 17-hour switch cannot bypass review; reverse and mixed modes; local midnight boundaries; missing history fails closed; actual punches protected; list/private/direct close consistent; override retained; stale approvals and immutable exports')
 finally:proc.terminate();proc.communicate(timeout=5)
