"""Verify Lite leave context is distinct from worked hours in live review JSON."""
import ctypes, ctypes.util, datetime, json, os, socket, sqlite3, subprocess, tempfile, time, urllib.request
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
crypt=ctypes.CDLL(ctypes.util.find_library('crypt'))
crypt.crypt.argtypes=[ctypes.c_char_p,ctypes.c_char_p]
crypt.crypt.restype=ctypes.c_char_p
with tempfile.TemporaryDirectory() as tmp:
 db=Path(tmp)/'test.db'
 with socket.socket() as s:
  s.bind(('127.0.0.1',0));port=s.getsockname()[1]
 p=subprocess.Popen([str(ROOT/'timeclock-server'),str(db),str(port)],cwd=ROOT,env={**os.environ,'DEVICE_KEY':'0123456789abcdef0123456789abcdef'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
 try:
  url=f'http://127.0.0.1:{port}'
  for _ in range(80):
   try:urllib.request.urlopen(url+'/health',timeout=.25).close();break
   except OSError:time.sleep(.05)
  else:raise AssertionError('server did not start')
  today=datetime.date.today();monday=today-datetime.timedelta(days=today.weekday());end=monday+datetime.timedelta(days=13)
  token='c'*64
  h=crypt.crypt(token.encode(),b'$6$rounds=5000$payreview2026$').decode()
  vacation_days=[monday+datetime.timedelta(days=i) for i in [2,3,4]]
  sick=monday+datetime.timedelta(days=7); holiday=monday+datetime.timedelta(days=8)
  with sqlite3.connect(db) as c:
   c.execute("INSERT INTO employees(name,email,attendance_mode) VALUES('Vacation Staff','v@example.test','vacation_only')")
   c.execute("INSERT INTO employees(name,email,attendance_mode) VALUES('Manager','m@example.test','punch_required')")
   c.execute("INSERT INTO employee_roles(employee_id,role) VALUES(2,'manager')")
   period_id=c.execute('INSERT INTO pay_periods(starts,ends) VALUES(?,?) RETURNING id',(str(monday),str(end))).fetchone()[0]
   c.execute("INSERT INTO review_links(employee_id,period_id,token_hash,expires_at,created_by,created_at) VALUES(1,?, ?,CAST(strftime('%s','now') AS INTEGER)+3600,2,CAST(strftime('%s','now') AS INTEGER))",(period_id,h))
   c.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status) VALUES(1,'vacation',?,?,'approved')",(str(vacation_days[0]),str(vacation_days[-1])))
   c.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status) VALUES(1,'vacation',?,?,'pending')",(str(monday+datetime.timedelta(days=9)),str(monday+datetime.timedelta(days=9))))
   c.execute("INSERT INTO attendance_absences(employee_id,day,reason,manager_id) VALUES(1,?,'Sick',2)",(str(sick),))
   c.execute("INSERT INTO statutory_holidays(day,name) VALUES(?,'Test Holiday')",(str(holiday),))
  with urllib.request.urlopen(url+'/review/'+token+'/detail') as r: j=json.load(r)
  assert j['vacation_only'] and j['seconds']==0 and j['entries']==[],j
  ctx=j['context'];vac=[x for x in ctx if x['type']=='timeoff:vacation'];sick_rows=[x for x in ctx if x['type']=='absence' and x['label']=='Sick'];hol=[x for x in ctx if x['type']=='holiday']
  assert len(vac)==3 and len(sick_rows)==1 and len(hol)==1,ctx
  assert not any(x['day']==str(monday+datetime.timedelta(days=9)) for x in ctx),ctx
  with urllib.request.urlopen(url+'/review/'+token) as r: html=r.read().decode()
  assert 'p.vacation_only' in html and 'vacation' in html and 'stat' in html
  print('PASS vacation-only live context: three approved days, one sick, one holiday; pending excluded; zero hours')
 finally:
  p.terminate();p.wait(timeout=5);p.stderr.close()
