"""Real API: optional company IDs, safe fallback, immutable payroll and exports."""
import csv,io,json,os,socket,sqlite3,subprocess,tempfile,time,urllib.request,urllib.parse,urllib.error,zipfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
 path=Path(tmp)/'clock.db'
 with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
 proc=subprocess.Popen([str(root/'timeclock-server'),str(path),str(port)],env={**os.environ,'TZ':'America/Edmonton','DEVICE_KEY':'test-device-key-12345'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
 try:
  base=f'http://127.0.0.1:{port}'
  for _ in range(100):
   try:urllib.request.urlopen(base+'/health',timeout=.2).close();break
   except OSError:time.sleep(.04)
  with sqlite3.connect(path) as db:
   db.execute("INSERT INTO employees(id,name,email) VALUES(1,'Manager','manager@example.test')")
   db.execute("INSERT INTO employee_roles VALUES(1,'manager')")
   db.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
   db.execute("INSERT INTO manager_cards VALUES('A1B2C3D4',1,1)")
   db.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+3600)",('d'*64,))
   db.execute('UPDATE review_send_settings SET enabled=0')
  def raw(route,form=None,expected=200,auth=True):
   data=urllib.parse.urlencode(form).encode() if form is not None else None
   headers={'Cookie':'tc_session='+'d'*64} if auth else {}
   if data is not None:headers['Content-Type']='application/x-www-form-urlencoded'
   try:
    with urllib.request.urlopen(urllib.request.Request(base+route,data=data,headers=headers),timeout=15) as r:assert r.status==expected;return r.read()
   except urllib.error.HTTPError as e:b=e.read();assert e.code==expected,(e.code,b);return b
  def call(*a,**k):return json.loads(raw(*a,**k))
  def people():return call('/api/v1/admin/employees')['employees']
  fields=dict(id=0,first_name='Sarah',last_name='Chen',position='Technician',email='',phone='',active=1,manager=0,uid='',late_email_enabled=0,auto_review_enabled=0,attendance_mode='vacation_only',vacation_days='15')
  call('/api/v1/admin/employees/save',dict(fields,employee_number='  0007-A  '))
  sarah=next(e for e in people() if e['employee_number']=='0007-A');sid=sarah['id'];assert sarah['display_id']=='0007-A'
  call('/api/v1/admin/employees/save',dict(fields,first_name='Ian',employee_number=''))
  ian=next(e for e in people() if e['first_name']=='Ian');iid=ian['id'];assert ian['display_id']==str(iid)
  for number,code in [('0007-a',409),(str(iid),409),('x'*65,400),('bad\x01id',400)]:call('/api/v1/admin/employees/save',dict(fields,first_name='Rejected',employee_number=number),code)
  assert not any(e['first_name']=='Rejected' for e in people())
  call('/api/v1/admin/employees/save',dict(fields,id=sid,employee_number='other'),401,False)
  with sqlite3.connect(path) as db:
   pid=db.execute("INSERT INTO pay_periods(starts,ends,status) VALUES('2026-09-01','2026-09-15','closed') RETURNING id").fetchone()[0]
   for eid in (sid,iid):db.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status) VALUES(?,'vacation','2026-09-02','2026-09-02','approved')",(eid,))
  call('/api/v1/admin/periods/exports',{'period_id':pid})
  with sqlite3.connect(path) as db:
   saved=db.execute('SELECT csv FROM lite_period_snapshots WHERE period_id=? AND revision=1',(pid,)).fetchone()[0]
   assert {r['Employee ID'] for r in csv.DictReader(io.StringIO(saved))}=={'0007-A',str(iid)}
  call('/api/v1/admin/employees/save',dict(fields,id=sid,employee_number='EMP-0008'))
  assert next(e for e in people() if e['id']==sid)['display_id']=='EMP-0008'
  period=next(p for p in call('/api/v1/admin/periods')['periods'] if p['id']==pid);assert period['new_export_required']
  call('/api/v1/admin/periods/exports',{'period_id':pid})
  with sqlite3.connect(path) as db:
   assert db.execute('SELECT csv FROM lite_period_snapshots WHERE period_id=? AND revision=1',(pid,)).fetchone()[0]==saved
   revised=db.execute('SELECT csv FROM lite_period_snapshots WHERE period_id=? AND revision=2',(pid,)).fetchone()[0]
   assert {r['Employee ID'] for r in csv.DictReader(io.StringIO(revised))}=={'EMP-0008',str(iid)}
   assert db.execute('SELECT employee_id FROM timeoff_requests ORDER BY employee_id').fetchall()==[(sid,),(iid,)]
   assert db.execute('SELECT count(*) FROM employee_change_audit WHERE employee_id=?',(sid,)).fetchone()[0]>=2
  for revision,number in [(1,'0007-A'),(2,'EMP-0008')]:
   with zipfile.ZipFile(io.BytesIO(raw(f'/api/v1/admin/periods/export.xlsx?period_id={pid}&revision={revision}'))) as workbook:
    assert all(number.encode() in workbook.read(f'xl/worksheets/sheet{i}.xml') for i in (1,2))
  annual=raw('/api/v1/admin/time-off/annual.csv?year=2026').decode();assert 'EMP-0008' in annual
  records=call('/api/v1/admin/time-off?view=all')['requests'];assert next(r for r in records if r['employee_id']==sid)['display_id']=='EMP-0008'
  with sqlite3.connect(path) as db:
   db.execute("INSERT INTO swipes(event_id,clock_id,card_uid,employee_id,swiped_at) VALUES('identity-swipe','clock','AABBCCDD',?,'2026-09-03T15:00:00Z')",(sid,))
   db.execute("INSERT INTO attendance_events(event_id,employee_id,kind) VALUES('identity-swipe',?,'IN')",(sid,))
  route='/api/v1/admin/export?from=2026-09-01&to=2026-09-15&scope=all&employee=0&format='
  assert next(csv.DictReader(io.StringIO(raw(route+'csv').decode('utf-8-sig'))))['employee_id']=='EMP-0008'
  record=call(route+'json')['records'][0];assert record['employee_id']==sid and record['employee_number']=='EMP-0008'
  call('/api/v1/admin/employees/save',dict(fields,id=sid,employee_number=''))
  assert next(e for e in people() if e['id']==sid)['display_id']==str(sid)
  # Omitted IDs preserve an existing value; old source IDs never become new keys.
  call('/api/v1/admin/employees/save',dict(fields,id=sid,employee_number='0'))
  call('/api/v1/admin/employees/save',dict(fields,id=sid))
  assert next(e for e in people() if e['id']==sid)['employee_number']=='0'
  # A restart preserves company IDs. A pre-field installation gains only the
  # optional column, with existing employees and attendance links retained.
  for upgrade in (False,True):
   proc.terminate();proc.communicate(timeout=5)
   if upgrade:
    with sqlite3.connect(path) as db:db.execute('ALTER TABLE employees DROP COLUMN employee_number')
   with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
   proc=subprocess.Popen([str(root/'timeclock-server'),str(path),str(port)],env={**os.environ,'TZ':'America/Edmonton','DEVICE_KEY':'test-device-key-12345'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
   for _ in range(100):
    try:urllib.request.urlopen(f'http://127.0.0.1:{port}/health',timeout=.2).close();break
    except OSError:time.sleep(.04)
   else:raise AssertionError('restart/upgrade failed')
   with sqlite3.connect(path) as db:
    assert db.execute('SELECT employee_number FROM employees WHERE id=?',(sid,)).fetchone()[0]==('' if upgrade else '0')
    assert db.execute('SELECT employee_id FROM timeoff_requests ORDER BY employee_id').fetchall()==[(sid,),(iid,)]
    assert db.execute('SELECT csv FROM lite_period_snapshots WHERE period_id=? AND revision=1',(pid,)).fetchone()[0]==saved
    assert db.execute('PRAGMA integrity_check').fetchone()[0]=='ok'
  print('PASS company IDs: create/edit/clear; whitespace/leading zeros/zero; duplicate and fallback collision rollback; auth; stable internal links and audits; payroll/annual/raw exports; immutable old revisions and exact workbook IDs')
 finally:proc.terminate();proc.communicate(timeout=5)
