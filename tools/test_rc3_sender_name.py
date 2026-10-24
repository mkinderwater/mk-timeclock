"""Saved sender name survives updating RC3 and is delivered with a plain SMTP envelope."""
import email,email.policy,json,os,socket,socketserver,sqlite3,subprocess,tempfile,threading,time,urllib.request,urllib.parse,urllib.error
from pathlib import Path
root=Path(__file__).resolve().parents[1]
class Handler(socketserver.StreamRequestHandler):
    messages=[];envelopes=[]
    def handle(self):
        self.wfile.write(b'220 test\r\n');self.wfile.flush();body=None
        while True:
            line=self.rfile.readline()
            if not line:return
            if body is not None:
                if line==b'.\r\n':self.messages.append(b''.join(body));body=None;reply=b'250 accepted\r\n'
                else:body.append(line);continue
            elif line.upper().startswith(b'MAIL FROM:'):self.envelopes.append(line.strip());reply=b'250 okay\r\n'
            elif line.upper().startswith(b'DATA'):body=[];reply=b'354 send\r\n'
            elif line.upper().startswith(b'QUIT'):self.wfile.write(b'221 bye\r\n');self.wfile.flush();return
            else:reply=b'250 okay\r\n'
            self.wfile.write(reply);self.wfile.flush()
class Relay(socketserver.ThreadingTCPServer):allow_reuse_address=True;daemon_threads=True
with tempfile.TemporaryDirectory() as tmp,Relay(('127.0.0.1',0),Handler) as relay:
    threading.Thread(target=relay.serve_forever,daemon=True).start()
    dbpath=Path(tmp)/'clock.db'
    # Existing RC3 SMTP table, before the new optional field.
    with sqlite3.connect(dbpath) as db:
        db.executescript("CREATE TABLE smtp_settings(id INTEGER PRIMARY KEY CHECK(id=1),url TEXT NOT NULL DEFAULT '',username TEXT NOT NULL DEFAULT '',password TEXT NOT NULL DEFAULT '',tls_mode TEXT NOT NULL DEFAULT 'starttls',sender TEXT NOT NULL DEFAULT ''); INSERT INTO smtp_settings(id,sender,password) VALUES(1,'existing@example.test','preserved-secret');")
    with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    server=subprocess.Popen([str(root/'timeclock-server'),str(dbpath),str(port)],cwd=root,env={**os.environ,'DEVICE_KEY':'test-device-key-12345'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        base=f'http://127.0.0.1:{port}'
        for _ in range(100):
            try:urllib.request.urlopen(base+'/health',timeout=.2).close();break
            except OSError:time.sleep(.04)
        else:raise AssertionError('server failed to start')
        with sqlite3.connect(dbpath) as db:
            assert db.execute('SELECT sender,password,sender_name FROM smtp_settings').fetchone()==('existing@example.test','preserved-secret','')
            db.execute("INSERT INTO employees(id,name,email) VALUES(1,'Test Manager','manager@example.test')")
            db.execute("INSERT INTO employee_roles VALUES(1,'manager')")
            db.execute("INSERT INTO manager_cards(uid,manager_id,active) VALUES('A1B2C3D4',1,1)")
            db.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
            db.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+600)",('d'*64,))
        def api(form=None):
            req=urllib.request.Request(base+'/api/v1/admin/smtp',data=urllib.parse.urlencode(form).encode() if form is not None else None,headers={'Cookie':'tc_session='+'d'*64,'Content-Type':'application/x-www-form-urlencoded'})
            with urllib.request.urlopen(req,timeout=3) as response:return json.load(response)
        params=dict(url=f'smtp://127.0.0.1:{relay.server_address[1]}',username='',password='',clear_password='0',tls_mode='none',sender='clock@example.test')
        for index,name in enumerate(['iCube Development Timeclock','Payroll, "North" \\ Office','Équipe de Montréal','']):
            api({**params,'sender_name':name})
            saved=api();assert saved['sender_name']==name and saved['sender']=='clock@example.test' and saved['has_password']
            assert 'password' not in saved
            with sqlite3.connect(dbpath) as db:
                db.execute("INSERT INTO email_queue(recipient,subject,body,event_id) VALUES('payroll@example.test','Sender test','Exact body',?)",(f'name-{index}',))
            mailer=subprocess.Popen([str(root/'timeclock-mailer'),str(dbpath)],cwd=root,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
            try:
                for _ in range(100):
                    with sqlite3.connect(dbpath) as db:status=db.execute('SELECT status FROM email_queue WHERE event_id=?',(f'name-{index}',)).fetchone()[0]
                    if status=='sent':break
                    time.sleep(.04)
                assert status=='sent'
            finally:mailer.terminate();mailer.communicate(timeout=5)
            msg=email.message_from_bytes(Handler.messages[index],policy=email.policy.default)
            assert msg['From'].addresses[0].display_name==name,str(msg['From'])
            assert msg['From'].addresses[0].addr_spec=='clock@example.test'
            assert Handler.envelopes[index]==b'MAIL FROM:<clock@example.test>',Handler.envelopes
            assert msg.get_content().strip()=='Exact body'
        for name in ['Bad\r\nBcc: bad@example.test','x'*201]:
            try:api({**params,'sender_name':name})
            except urllib.error.HTTPError as e:assert e.code==400
            else:raise AssertionError('invalid sender name saved')
        # Enabling scheduled backup mail must accept the configured None relay.
        request=urllib.request.Request(base+'/api/v1/admin/backups/settings',data=urllib.parse.urlencode({'enabled':'1','manager_id':'1'}).encode(),headers={'Cookie':'tc_session='+'d'*64,'Content-Type':'application/x-www-form-urlencoded'})
        with urllib.request.urlopen(request,timeout=3) as response:assert json.load(response)['status']=='saved'
        with sqlite3.connect(dbpath) as db:assert db.execute('SELECT email_enabled,manager_id FROM backup_settings').fetchone()==(1,1)
        assert api()['sender_name']==''
        assert len(Handler.messages)==4
    finally:server.terminate();server.communicate(timeout=5);relay.shutdown()
print('PASS: current RC3 update preserves SMTP settings; sender name save/read; ASCII, quotes, backslash, Unicode and blank delivered; address-only SMTP envelope; injection/length rejected')
