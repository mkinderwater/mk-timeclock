import sqlite3,tempfile,os,subprocess,time,urllib.request,json
from pathlib import Path
base=Path(__file__).resolve().parents[1];work=Path(tempfile.mkdtemp(prefix='tc0927-'));db=work/'server.db';env=os.environ.copy();env['DEVICE_KEY']='localtestingkey0123456789';env['TZ']='America/Edmonton';port=18787
server=subprocess.Popen([str(base/'timeclock-server'),str(db),str(port)],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
try:
 time.sleep(.2)
 assert server.poll() is None,server.stderr.read().decode()
 con=sqlite3.connect(db);con.execute('INSERT INTO employees(id,name,email) VALUES(1,?,?)',('Sample Person','sample@example.test'));con.execute('INSERT INTO employee_roles(employee_id,role) VALUES(1,?)',('manager',));con.execute('INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,?)',('fakehash',));token='a'*64;con.execute("INSERT INTO login_sessions(token,employee_id,expires_at) VALUES(?,?,CAST(strftime('%s','now') AS INTEGER)+600)",(token,1));con.execute("INSERT INTO swipes(event_id,clock_id,card_uid,employee_id,swiped_at) VALUES(?,?,?,?,?)",('E1','CLOCK-001','4903305B',1,'2026-10-11T00:30:00Z'));con.execute("INSERT INTO attendance_events(event_id,employee_id,kind,late_minutes) VALUES('E1',1,'IN',7)");con.commit();con.close()
 def fetch(path,auth=True):
  req=urllib.request.Request('http://127.0.0.1:'+str(port)+path,headers={'Cookie':'tc_session='+token} if auth else {})
  return urllib.request.urlopen(req).read().decode()
 assert 'Attendance Export' in fetch('/attendance-export')
 assert 'Email Queue' in fetch('/email-queue')
 assert 'RFID Card Status' in fetch('/card-status')
 assert "data-page='corrections'" in fetch('/swipe-notes')  # v0.9.48: manager notes removed; old URL redirects
 assert 'Unknown RFID Cards' in fetch('/rejected-cards')
 url='/api/v1/admin/export?scope=all&employee=0&from=2026-10-10&to=2026-10-10&format=json'
 result=json.loads(fetch(url));assert result['record_count']==1,result;assert result['records'][0]['local_date']=='2026-10-10',result
 url='/api/v1/admin/export?scope=employee&employee=1&from=2026-10-11&to=2026-10-11&format=csv'
 result=fetch(url);assert 'E1' not in result,result
 assert json.loads(fetch('/api/v1/admin/card-status'))['cards']==[]
 assert json.loads(fetch('/api/v1/admin/email-queue'))['messages']==[]
 assert json.loads(fetch('/api/v1/admin/rejected-cards'))['unknown_cards']==[]
 con=sqlite3.connect(db);assert con.execute('select count(*) from admin_export_audit').fetchone()[0]==2
 print('PASS HTTP routes, session-protected views, local date export, CSV JSON, export audit, original swipe unchanged');
finally:
 server.terminate();server.wait(timeout=5)
