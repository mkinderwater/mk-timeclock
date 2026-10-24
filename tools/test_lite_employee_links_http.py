"""Live HTTP security and read-only vacation-only timesheet regression."""
import ctypes, ctypes.util, datetime, json, os, socket, sqlite3, subprocess, tempfile, time, urllib.request, urllib.error
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
crypt=ctypes.CDLL(ctypes.util.find_library('crypt'))
crypt.crypt.argtypes=[ctypes.c_char_p,ctypes.c_char_p]
crypt.crypt.restype=ctypes.c_char_p
hash_token=lambda t:crypt.crypt(t.encode(),b'$6$rounds=5000$payreview2026$').decode()
with tempfile.TemporaryDirectory() as tmp:
 db=Path(tmp)/'test.db'
 with socket.socket() as s:
  s.bind(('127.0.0.1',0)); port=s.getsockname()[1]
 proc=subprocess.Popen([str(ROOT/'timeclock-server'),str(db),str(port)],cwd=ROOT,env={**os.environ,'DEVICE_KEY':'0123456789abcdef0123456789abcdef'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
 try:
  url=f'http://127.0.0.1:{port}'
  for _ in range(80):
   try: urllib.request.urlopen(url+'/health',timeout=.25).close();break
   except OSError:time.sleep(.05)
  else:raise AssertionError('server did not start')
  yesterday=datetime.date.today()-datetime.timedelta(days=1)
  start=yesterday-datetime.timedelta(days=13)
  a,b='a'*64,'b'*64
  with sqlite3.connect(db) as c:
   c.execute("INSERT INTO employees(name,email,attendance_mode) VALUES('Vacation Staff','v@example.test','vacation_only')")
   c.execute("INSERT INTO employees(name,email,attendance_mode) VALUES('Punch Staff','p@example.test','punch_required')")
   c.execute("INSERT INTO employee_roles(employee_id,role) VALUES(2,'manager')")
   period_id=c.execute('INSERT INTO pay_periods(starts,ends) VALUES(?,?)',(str(start),str(yesterday))).lastrowid
   c.execute('INSERT INTO review_links(employee_id,period_id,token_hash,expires_at,created_by,created_at) VALUES(1,?, ?,CAST(strftime(\'%s\',\'now\') AS INTEGER)+3600,2,CAST(strftime(\'%s\',\'now\') AS INTEGER))',(period_id,hash_token(a)))
   c.execute('INSERT INTO review_links(employee_id,period_id,token_hash,expires_at,created_by,created_at) VALUES(2,?, ?,CAST(strftime(\'%s\',\'now\') AS INTEGER)-1,2,CAST(strftime(\'%s\',\'now\') AS INTEGER))',(period_id,hash_token(b)))
  with urllib.request.urlopen(url+'/review/'+a+'/detail') as r: detail=json.load(r)
  assert detail['seconds']==0 and detail['entries']==[],detail
  assert detail['id']==period_id
  with urllib.request.urlopen(url+'/review/'+a) as r: assert b'Your timesheet' in r.read()
  for path in ('/review/'+b+'/detail','/review/'+'f'*64+'/detail'):
   try:urllib.request.urlopen(url+path)
   except urllib.error.HTTPError as e:assert e.code==404,(path,e.code)
   else:raise AssertionError('expired or unknown token accepted')
  # Vacation-only links remain readable without creating an unnecessary signoff.
  try:
   urllib.request.urlopen(urllib.request.Request(url+'/review/'+a+'/approve',data=b'ack=yes',method='POST'))
  except urllib.error.HTTPError as e:assert e.code==409,e.code
  else:raise AssertionError('vacation-only approval accepted')
  with sqlite3.connect(db) as c:
   assert c.execute("SELECT count(*) FROM period_signoffs WHERE period_id=? AND employee_id=1",(period_id,)).fetchone()[0]==0
  print('PASS live employee link isolation, expiry, no signoff, zero-punch vacation-only access')
 finally:
  proc.terminate();proc.wait(timeout=5);proc.stderr.close()
