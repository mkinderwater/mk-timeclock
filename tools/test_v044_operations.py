#!/usr/bin/env python3
import os,sqlite3,tempfile,subprocess,time,json,urllib.request,urllib.error,urllib.parse,socket
from pathlib import Path
base=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='tc044-') as d:
 db=Path(d)/'server.db'
 sock=socket.socket();sock.bind(('127.0.0.1',0));port=sock.getsockname()[1];sock.close()
 proc=subprocess.Popen([str(base/'timeclock-server'),str(db),str(port)],env=dict(os.environ,DEVICE_KEY='test-device-key-123456789'),stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
 try:
  time.sleep(.2);assert proc.poll() is None
  c=sqlite3.connect(db);c.execute("INSERT INTO employees(id,name,active) VALUES(1,'Manager',1)")
  c.execute("INSERT INTO employee_roles VALUES(1,'manager')")
  c.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'test')")
  c.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+3600)",('a'*64,))
  c.execute("INSERT INTO manager_cards(uid,manager_id,active) VALUES('AABBCCDD',1,1)")
  c.execute("INSERT INTO email_queue(id,recipient,subject,body,status,attempts) VALUES(10,'test@example.com','Bad','retry','failed',9)")
  c.commit()
  def req(path,data=None,admin=True,device=False):
   headers={'Content-Type':'application/x-www-form-urlencoded'}
   if admin:headers['Cookie']='tc_session='+'a'*64
   if device:headers['X-Device-Key']='test-device-key-123456789'
   r=urllib.request.Request('http://127.0.0.1:'+str(port)+path,headers=headers,data=urllib.parse.urlencode(data).encode() if data is not None else None)
   try:
    with urllib.request.urlopen(r,timeout=5) as res:return res.status,json.loads(res.read())
   except urllib.error.HTTPError as e:return e.code,json.loads(e.read())
  assert req('/api/v1/admin/email-queue/retry',{'id':'10'},admin=False)[0]==401
  assert req('/api/v1/admin/email-queue/retry',{'id':'10'})[0]==200
  assert req('/api/v1/admin/email-queue/retry',{'id':'10'})[0]==409
  assert c.execute('SELECT status,attempts FROM email_queue WHERE id=10').fetchone()==('pending',0)
  assert c.execute('SELECT count(*) FROM email_retry_audit').fetchone()[0]==1
  assert req('/api/v1/admin/templates/restore',{'id':'swipe_employee'})[0]==403
  before=c.execute("SELECT subject FROM email_templates WHERE id='swipe_employee'").fetchone()[0]
  c.execute("UPDATE email_templates SET subject='CHANGED' WHERE id='swipe_employee'");c.commit()
  arm=req('/api/v1/admin/templates/restore/arm',{'id':'swipe_employee'})
  assert arm[0]==200,arm
  assert req('/api/v1/admin/templates/restore/status',{'token':'0'*32})[0]==409
  assert req('/api/v1/admin/templates/restore/status',{'token':arm[1]['token']})[1]['status']=='waiting'
  c.execute("UPDATE manager_scan SET uid='AABBCCDD',scanned_at=armed_at+1 WHERE id=1");c.commit()
  assert req('/api/v1/admin/templates/restore/status',{'token':arm[1]['token']})[1]['status']=='restored'
  assert c.execute("SELECT subject FROM email_templates WHERE id='swipe_employee'").fetchone()[0]==before
  assert c.execute('SELECT count(*) FROM template_restore_audit WHERE manager_id=1 AND approved_uid=\'AABBCCDD\'').fetchone()[0]==1
  event={'event_id':'a'*32,'clock_id':'clock1','card_uid':'ABCDEF12','swiped_at':'2026-10-11T18:00:00Z','http_status':'422','rejected_at':'2026-10-11T18:01:00Z'}
  assert req('/api/v1/clock/rejected',event,admin=False)[0]==401
  assert req('/api/v1/clock/rejected',event,admin=False,device=True)[0]==200
  assert req('/api/v1/clock/rejected',event,admin=False,device=True)[0]==200
  assert c.execute('SELECT count(*) FROM rejected_uploads').fetchone()[0]==1
  assert req('/api/v1/admin/rejected-uploads')[1]['rejected_uploads'][0]['event_id']=='a'*32
  assert req('/api/v1/admin/tls/status')[0]==200
  print('PASS v0.9.44 audited retry, manager-card template restore, device rejected upload sync and TLS status')
 finally:
  proc.terminate();proc.wait(timeout=4)
