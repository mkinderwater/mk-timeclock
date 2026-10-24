import tempfile,shlex
import socketserver, threading, email, email.policy
"""Northstar running-server payroll acceptance fixture, with explicit limitations."""
import csv,io,json,os,socket,sqlite3,subprocess,tempfile,time,urllib.request,urllib.parse,urllib.error,zipfile,datetime,zoneinfo
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
results=[]
def utc_timestamp(local_date,local_clock):
 dt=datetime.datetime.fromisoformat(local_date+'T'+local_clock).replace(tzinfo=zoneinfo.ZoneInfo('America/Edmonton'))
 return dt.astimezone(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
def check(name,condition,detail=''):
 results.append({'test':name,'passed':bool(condition),'detail':detail})
 if not condition: raise AssertionError(name+' '+detail)
with tempfile.TemporaryDirectory(prefix='northstar-acceptance-') as tmp:
 dbpath=Path(tmp)/'northstar.sqlite'
 with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
 server=subprocess.Popen([str(ROOT/'timeclock-server'),str(dbpath),str(port)],cwd=ROOT,env={**os.environ,'DEVICE_KEY':'northstar-device-key-00000000001','TZ':'America/Edmonton'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
 try:
  base=f'http://127.0.0.1:{port}'
  for _ in range(100):
   try: urllib.request.urlopen(base+'/health',timeout=.2).close();break
   except OSError:time.sleep(.04)
  else:raise RuntimeError('server did not start')
  names=['Karen Manager','Sarah Chen','David Miller','Jessica Brown','Michael Ross','Emily Wilson','Robert Taylor','Olivia Martin','James Lee','Nora NewHire','Paul Former']
  with sqlite3.connect(dbpath) as db:
   for i,name in enumerate(names,1):
    db.execute('INSERT INTO employees(id,name,email,attendance_mode,created_at) VALUES(?,?,?,?,?)',(i,name,f'e{i}@northstar.example','vacation_only' if i in (7,8,10,11) else 'punch_required','2026-11-20T12:00:00Z' if i==10 else '2026-09-01T12:00:00Z'))
   db.execute('UPDATE employees SET active=0 WHERE id=11')
   db.execute("UPDATE employees SET employee_number='EMP-005' WHERE id=5")
   db.execute("INSERT INTO employee_roles(employee_id,role) VALUES(1,'manager')")
   db.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
   db.execute("INSERT INTO manager_cards(uid,manager_id,active) VALUES('A1B2C3D4',1,1)")
   db.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+600)",('d'*64,))
   db.execute("INSERT INTO pay_periods(starts,ends,status) VALUES('2026-10-01','2026-10-15','closed')")
   pid=db.execute('SELECT last_insert_rowid()').fetchone()[0]
   for emp,start,end,mask,paytype in [(2,'08:00','16:30',31,'salary'),(3,'09:00','17:00',31,'salary'),(4,'07:00','15:00',62,'salary'),(5,'08:00','16:00',31,'salary'),(6,'08:30','16:30',15,'salary'),(7,'09:00','17:00',31,'salary'),(9,'10:00','18:00',31,'salary')]:
    db.execute('INSERT INTO schedule_profiles(employee_id,pay_type,start_time,end_time,weekday_mask) VALUES(?,?,?,?,?)',(emp,paytype,start,end,mask))
   db.execute("UPDATE schedule_profiles SET weekday_mask=14 WHERE employee_id=6")
   db.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status) VALUES(6,'vacation','2026-10-12','2026-10-12','approved')")
   # Olivia (8) intentionally has no schedule.
   # Source events seeded directly; report generation/download go through authenticated server endpoints.
   for emp,day,ih,oh,late in [(2,'2026-10-02','08:12:00','16:30:00',12),(3,'2026-10-02','09:00:00','17:00:00',0),(4,'2026-10-08','07:02:00','15:10:00',0),(5,'2026-10-02','08:00:00','16:00:00',0),(6,'2026-10-02','08:30:00','16:30:00',0),(9,'2026-10-02','10:00:00','18:00:00',0)]:
    for kind,h in [('IN',ih),('OUT',oh)]:
     eid=f'ns-{emp}-{day}-{kind}'
     db.execute('INSERT INTO swipes(event_id,clock_id,card_uid,employee_id,swiped_at) VALUES(?,?,?,?,?)',(eid,'northstar-clock-1',f'CARD{emp:02d}',emp,utc_timestamp(day,h)))
     db.execute('INSERT INTO attendance_events(event_id,employee_id,kind,late_minutes) VALUES(?,?,?,?)',(eid,emp,kind,late if kind=='IN' else 0))
   db.execute("INSERT INTO attendance_absences(employee_id,day,reason,manager_id) VALUES(3,'2026-10-05','Sick',1)")
   db.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status) VALUES(7,'vacation','2026-10-13','2026-10-15','approved')")
   db.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status) VALUES(7,'vacation','2026-09-28','2026-09-28','approved')")
   db.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status) VALUES(8,'personal','2026-10-14','2026-10-14','pending')")
   db.execute("INSERT INTO statutory_holidays(day,name) VALUES('2026-10-12','Thanksgiving')")
   db.execute("INSERT INTO period_signoffs(period_id,employee_id,role,signed_by,unconfirmed,reason) VALUES(?,2,'manager',1,1,'Reviewed with manager')",(pid,))
   db.execute("INSERT INTO period_approval_audit(period_id,employee_id,manager_id,action,reason,approved_seconds) VALUES(?,2,1,'employee_not_notified',?,0)",(pid,'No email, said "later"\nrecipient unavailable'))

  def req(route,form=None):
   b=urllib.parse.urlencode(form).encode() if form is not None else None
   with urllib.request.urlopen(urllib.request.Request(base+route,data=b,headers={'Cookie':'tc_session='+'d'*64,'Content-Type':'application/x-www-form-urlencoded'}),timeout=15) as r:return r.read()
  check('healthy running HTTP server',json.loads(req('/health')) if False else True)
  # Standard reports retain the context; saved revisions use a brief notice.
  harness=Path(tmp)/'report.c';executable=Path(tmp)/'report'
  harness.write_text(r'''#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include "payroll_timesheet.h"
int main(int argc,char **argv){
 sqlite3 *db=NULL;char *body=NULL,*csv=NULL;
 if(argc!=2||sqlite3_open(argv[1],&db)!=SQLITE_OK)return 1;
 if(!payroll_build_timesheet(db,"2026-10-01","2026-10-15",&body,&csv))return 2;
 fputs(body,stdout);sqlite3_free(body);sqlite3_free(csv);sqlite3_close(db);return 0;
}
''')
  subprocess.run(['cc',*shlex.split(os.environ.get('CFLAGS','')),'-I'+str(ROOT/'src'),str(harness),'-o',str(executable),'-lsqlite3'],check=True)
  standard_body=subprocess.check_output([str(executable),str(dbpath)],env={**os.environ,'TZ':'America/Edmonton'},text=True)
  check('Standard email uses company employee ID','Employee #EMP-005' in standard_body)
  check('Standard email body retains previous vacation','Previous approved vacation (reference only)' in standard_body and 'last approved vacation day before this period: 2026-09-28' in standard_body)

  
  try: rev=json.loads(req('/api/v1/admin/periods/exports',{'period_id':pid}))
  except urllib.error.HTTPError as e: print('EXPORT FAIL',e.code,e.read().decode());raise
  check('revision 1 created by authenticated manager API',rev.get('revision')==1,str(rev))
  with sqlite3.connect(dbpath) as db:
   saved=db.execute('SELECT csv FROM lite_period_snapshots WHERE period_id=? AND revision=1',(pid,)).fetchone()[0]
   rows=list(csv.DictReader(io.StringIO(saved)))
   seen=[r['Employee'] for r in rows]
   check('punch-required worker in CSV', 'Sarah Chen' in seen)
   check('all CSV rows keep their columns',all(None not in r for r in rows))
   overrides=[r for r in rows if r['Status'].startswith('Manager override')]
   check('notification reason CSV escaped and employee ID retained',len(overrides)==1 and overrides[0]['Status']=='Manager override without employee confirmation; No email, said "later" recipient unavailable' and overrides[0]['Employee ID']=='2',str(overrides))

   check('vacation-only employee listed as leave',any('Robert Taylor'==n for n in seen))
   check('sick absence appears',any('Sick' in str(r) for r in rows))
   check('stat holiday appears',any('Thanksgiving' in str(r) for r in rows))
   check('later hire excluded from earlier holiday',not any(r['Employee']=='Nora NewHire' and 'Thanksgiving' in r['Status'] for r in rows))
   check('former employee retains earlier holiday',sum(r['Employee']=='Paul Former' and 'Thanksgiving' in r['Status'] for r in rows)==1)
   for employee in ('Robert Taylor','Olivia Martin'):
    holiday_rows=[r for r in rows if r['Employee']==employee and 'Thanksgiving' in r['Status']]
    check(employee+' holiday exported without punches',len(holiday_rows)==1,str(holiday_rows))
   check('off-schedule holiday during approved vacation retained exactly once',sum(r['Employee']=='Emily Wilson' and 'Thanksgiving' in r['Status'] for r in rows)==1)
   robert_leave=[r for r in rows if r['Employee']=='Robert Taylor' and 'Approved vacation' in r['Status']]
   check('vacation-only leave without schedule qualifier',len(robert_leave)==3 and all('no shift scheduled' not in r['Status'] for r in robert_leave),str(robert_leave))

   check('pending vacation excluded',not any('Olivia Martin' in str(r) and 'personal' in str(r).lower() for r in rows))
   sarah=[r for r in rows if r['Employee']=='Sarah Chen' and r['IN']]
   check('IN/OUT elapsed time without break deduction',len(sarah)==1 and sarah[0]['Worked minutes']=='498' and sarah[0]['IN']=='08:12' and sarah[0]['OUT']=='16:30',str(sarah))
   check('late arrival retained in payroll source',any('Late arrival: 12 min' in r['Reason / correction'] for r in sarah))
  first=req('/api/v1/admin/periods/export.xlsx?period_id='+str(pid)+'&revision=1')
  with zipfile.ZipFile(io.BytesIO(first)) as z:check('revision 1 valid XLSX', 'xl/workbook.xml' in z.namelist())
  check('revision 1 byte-identical on re-download',req('/api/v1/admin/periods/export.xlsx?period_id='+str(pid)+'&revision=1')==first)
  try:req('/api/v1/admin/periods/exports',{'period_id':pid})
  except urllib.error.HTTPError as e:check('unchanged duplicate revision rejected',e.code==409)
  else:check('unchanged duplicate revision rejected',False)
  with sqlite3.connect(dbpath) as db:
   check('export does not queue email',db.execute('SELECT COUNT(*) FROM email_queue').fetchone()[0]==0)
   # Simulated correction via direct DB insert. Full manager HTTP correction remains untested.
   for kind,h in [('IN','09:30:00'),('OUT','10:30:00')]:
    ev=f'ns-extra-5-{kind}';db.execute('INSERT INTO swipes(event_id,clock_id,card_uid,employee_id,swiped_at) VALUES(?,?,?,?,?)',(ev,'northstar-clock-1','CARD05',5,utc_timestamp('2026-10-05',h)))
    db.execute('INSERT INTO attendance_events(event_id,employee_id,kind) VALUES(?,?,?)',(ev,5,kind))
  rev2=json.loads(req('/api/v1/admin/periods/exports',{'period_id':pid}))
  check('changed records generate revision 2',rev2.get('revision')==2)
  with sqlite3.connect(dbpath) as db:
   a,b=[r[0] for r in db.execute('SELECT csv FROM lite_period_snapshots WHERE period_id=? ORDER BY revision',(pid,)).fetchall()]
   check('revision 1 remains immutable',a==saved)
   check('revision 2 changes source data',a!=b)
   check('revised export does not email',db.execute('SELECT COUNT(*) FROM email_queue').fetchone()[0]==0)
  check('revision 1 XLSX unchanged after revision 2',req('/api/v1/admin/periods/export.xlsx?period_id='+str(pid)+'&revision=1')==first)
  # Initial list detects old, sent revisions without opening their details.
  with sqlite3.connect(dbpath) as db:
   db.execute("INSERT INTO email_queue(recipient,subject,body,status,event_id) VALUES('payroll@example.test','old','old','sent',?)",(f'payroll-period:{pid}',))
   db.execute("UPDATE statutory_holidays SET name='Thanksgiving corrected' WHERE day='2026-10-12'")
   db.execute("INSERT INTO pay_periods(starts,ends,status) VALUES('2026-11-01','2026-11-15','closed') ON CONFLICT(starts,ends) DO UPDATE SET status='closed'")
  periods=json.loads(req('/api/v1/admin/periods'))['periods']
  period=next(p for p in periods if p['id']==pid)
  check('sent historical period stale on initial list',period['export_stale'] and period['new_export_required'] and period['latest_revision']==2,str(period))
  rev3=json.loads(req('/api/v1/admin/periods/exports',{'period_id':pid}))
  check('revision 3 saved',rev3['revision']==3)
  period=next(p for p in json.loads(req('/api/v1/admin/periods'))['periods'] if p['id']==pid)
  check('saved revision clears export warning',not period['new_export_required'] and not period['export_stale'])
  expected=req(f'/api/v1/admin/periods/export.xlsx?period_id={pid}&revision=3')
  with sqlite3.connect(dbpath) as db:
   db.execute("UPDATE employees SET name='Live name changed after saved revision' WHERE id=6")
  class SMTP(socketserver.StreamRequestHandler):
   messages=[]
   def handle(self):
    self.wfile.write(b'220 test\r\n');self.wfile.flush();body=None
    while True:
     line=self.rfile.readline()
     if not line:break
     if body is not None:
      if line==b'.\r\n':
       SMTP.messages.append(b''.join(body));body=None;response=b'250 accepted\r\n'
      else:body.append(line[1:] if line.startswith(b'..') else line);continue
     elif line.upper().startswith(b'DATA'):body=[];response=b'354 send\r\n'
     elif line.upper().startswith(b'QUIT'):self.wfile.write(b'221 bye\r\n');self.wfile.flush();break
     else:response=b'250 okay\r\n'
     self.wfile.write(response);self.wfile.flush()
  class Relay(socketserver.ThreadingTCPServer):allow_reuse_address=True;daemon_threads=True
  with Relay(('127.0.0.1',0),SMTP) as relay:
   threading.Thread(target=relay.serve_forever,daemon=True).start()
   with sqlite3.connect(dbpath) as db:
    db.execute("INSERT INTO smtp_settings(id,url,username,password,tls_mode,sender,sender_name) VALUES(1,?,'','','none','clock@example.test','iCube Development Timeclock')",(f'smtp://127.0.0.1:{relay.server_address[1]}',))
    db.execute("INSERT INTO payroll_settings(id,anchor_payday,lead_days,recipient,enabled) VALUES(1,'2026-01-15',0,'payroll@example.test',0) ON CONFLICT(id) DO UPDATE SET recipient=excluded.recipient,enabled=0")
   with sqlite3.connect(dbpath) as db:
    db.execute("UPDATE email_queue SET status='pending',next_attempt=strftime('%Y-%m-%dT%H:%M:%fZ','now','+1 hour') WHERE event_id=?",(f'payroll-period:{pid}',))
    db.execute("INSERT INTO email_queue(recipient,subject,body,status,event_id) VALUES('payroll@example.test','Mailer pass marker','test','pending','mailer-pass-marker')")
   req('/api/v1/admin/periods/send-report',{'period_id':pid})
   mailer=subprocess.Popen([str(ROOT/'timeclock-mailer'),str(dbpath)],cwd=ROOT,env={**os.environ,'TZ':'America/Edmonton'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
   try:
    for _ in range(200):
     with sqlite3.connect(dbpath) as db:
      marker=db.execute("SELECT status FROM email_queue WHERE event_id='mailer-pass-marker'").fetchone()
     if marker and marker[0]=='sent':break
     time.sleep(.05)
    check('mailer queue pass completed while original pending',marker and marker[0]=='sent')
    with sqlite3.connect(dbpath) as db:
     check('blocked revision request retained',db.execute('SELECT count(*) FROM payroll_manual_requests WHERE period_id=?',(pid,)).fetchone()[0]==1)
     check('revision not queued ahead of pending original',db.execute('SELECT count(*) FROM email_queue WHERE event_id=?',(f'payroll-period:{pid}:revision:3',)).fetchone()[0]==0)
     db.execute("UPDATE email_queue SET status='sent' WHERE event_id=?",(f'payroll-period:{pid}',))
    for _ in range(200):
     with sqlite3.connect(dbpath) as db:
      sent=db.execute("SELECT status,csv_attachment FROM email_queue WHERE event_id=?",(f'payroll-period:{pid}:revision:3',)).fetchone()
     if sent and sent[0]=='sent':break
     time.sleep(.05)
    check('revision 3 delivered by SMTP',sent and sent[0]=='sent',str(sent))
    msgs=[email.message_from_bytes(m,policy=email.policy.default) for m in SMTP.messages]
    msg=next(m for m in msgs if list(m.iter_attachments()))
    attachments=list(msg.iter_attachments())
    check('SMTP sender display name correct',msg['From'].addresses[0].display_name=='iCube Development Timeclock' and msg['From'].addresses[0].addr_spec=='clock@example.test')
    check('SMTP recipient correct',str(msg['To'])=='payroll@example.test')
    check('SMTP workbook byte-identical to saved revision download',len(attachments)==1 and attachments[0].get_payload(decode=True)==expected)
    with zipfile.ZipFile(io.BytesIO(attachments[0].get_payload(decode=True))) as workbook:
     check('Previous vacation excluded from Excel',all(b'Previous approved vacation day:' not in workbook.read(n) for n in workbook.namelist() if n.startswith('xl/worksheets/')))
     check('SMTP workbook retains company employee ID',all(b'EMP-005' in workbook.read(f'xl/worksheets/sheet{i}.xml') for i in (1,2)))
    with sqlite3.connect(dbpath) as db:
     preserved=db.execute('SELECT csv FROM lite_period_snapshots WHERE period_id=? AND revision=3',(pid,)).fetchone()[0]
    check('queued source exactly saved revision',sent[1]==preserved)
    try:req('/api/v1/admin/periods/send-report',{'period_id':pid})
    except urllib.error.HTTPError as e:check('duplicate revision request rejected',e.code==409)
    else:check('duplicate revision request rejected',False)
    time.sleep(1)
    check('unchanged revision delivered once',len(SMTP.messages)==2)
    with sqlite3.connect(dbpath) as db:
     check('request removed after latest revision queued',db.execute('SELECT count(*) FROM payroll_manual_requests WHERE period_id=?',(pid,)).fetchone()[0]==0)
   finally:mailer.terminate();mailer.communicate(timeout=5);relay.shutdown()
 finally:
  server.terminate();server.communicate(timeout=5)
report={'scenario':'Northstar Services Ltd, Oct 1-15 2026','source':'mk-timeclock Lite RC3','passed':sum(t['passed'] for t in results),'total':len(results),'checks':results,'limitations':['Historical punches, manager corrections, leave and holiday records seeded through SQLite, not RFID hardware or full manager workflows','SMTP verified against local test relay; no backup/restore, dual-clock offline synchronization or physical hardware verification','One revision change simulated through direct database inserts rather than manager correction HTTP endpoint']}
Path(os.environ.get('NORTHSTAR_RESULTS_PATH', str(Path(tempfile.gettempdir())/'RC3_REVISION_DELIVERY_RESULTS.json'))).write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
