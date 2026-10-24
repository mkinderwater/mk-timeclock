"""Live employee link approval against fresh SQLite and real HTTP server."""
import ctypes, ctypes.util, datetime, json, os, socket, sqlite3, subprocess, tempfile, time, urllib.request, urllib.error
from pathlib import Path
root=Path(__file__).resolve().parents[1]
crypt=ctypes.CDLL(ctypes.util.find_library('crypt'))
crypt.crypt.argtypes=[ctypes.c_char_p,ctypes.c_char_p];crypt.crypt.restype=ctypes.c_char_p
with tempfile.TemporaryDirectory() as temp:
 db=Path(temp)/'clock.db'
 with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
 server=subprocess.Popen([str(root/'timeclock-server'),str(db),str(port)],cwd=root,env={**os.environ,'DEVICE_KEY':'0123456789abcdef0123456789abcdef'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
 try:
  url=f'http://127.0.0.1:{port}'
  for _ in range(100):
   try:urllib.request.urlopen(url+'/health',timeout=.2).close();break
   except OSError:time.sleep(.05)
  else:raise AssertionError('Server failed to start')
  day=datetime.date.today()-datetime.timedelta(days=30)
  start=str(day);end=str(day+datetime.timedelta(days=1));token='e'*64
  hashed=crypt.crypt(token.encode(),b'$6$rounds=5000$payreview2026$').decode()
  with sqlite3.connect(db) as c:
   c.execute("INSERT INTO employees(name,email,attendance_mode) VALUES('Leave Only','leave@example.test','punch_required')")
   c.execute("INSERT INTO employees(name,email) VALUES('Manager','manager@example.test')")
   c.execute("INSERT INTO employee_roles(employee_id,role) VALUES(2,'manager')")
   pid=c.execute('INSERT INTO pay_periods(starts,ends) VALUES(?,?) RETURNING id',(start,end)).fetchone()[0]
   c.execute("INSERT INTO review_links(employee_id,period_id,token_hash,expires_at,created_by,created_at) VALUES(1,?,?,CAST(strftime('%s','now') AS INTEGER)+3600,2,CAST(strftime('%s','now') AS INTEGER))",(pid,hashed))
   c.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status) VALUES(1,'vacation',?,?,'approved')",(start,end))
  def fetch(path,method='GET'):
   request=urllib.request.Request(url+path,method=method)
   try:
    with urllib.request.urlopen(request,timeout=5) as response:return response.status,json.loads(response.read())
   except urllib.error.HTTPError as ex:return ex.code,json.loads(ex.read())
  with urllib.request.urlopen(url+'/review/'+token,timeout=5) as html_response:
   html=html_response.read().decode()
  assert 'Approve timesheet' in html and "base+'/approve'" in html
  import re
  scripts=re.findall(r'<script[^>]*>(.*?)</script>',html,re.S|re.I)
  for script in scripts:
   if script.strip():
    check=subprocess.run(['node','--check'],input=script,text=True,capture_output=True)
    assert check.returncode==0,check.stderr
  status,j=fetch('/review/'+token+'/detail');assert status==200 and j['signed'] is False,(status,j)
  status,j=fetch('/review/'+token+'/approve','POST');assert status==200,(status,j)
  status,j=fetch('/review/'+token+'/detail');assert status==200 and j['signed'] is True,(status,j)
  status,j=fetch('/review/'+token+'/approve','POST');assert status==200,(status,j)
  with sqlite3.connect(db) as c:
   assert c.execute("SELECT count(*) FROM period_signoffs WHERE period_id=? AND employee_id=1 AND role='employee'",(pid,)).fetchone()[0]==1
   c.execute("UPDATE pay_periods SET status='closed' WHERE id=?",(pid,))
  status,j=fetch('/review/'+token+'/approve','POST');assert status==409,(status,j)
  print('PASS live employee approval: current signoff, idempotence, one row, closed period refused')
 finally:
  server.terminate();server.wait(timeout=5);server.stderr.close()
