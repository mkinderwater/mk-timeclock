"""Exercise the actual mailer against a local SMTP relay and fresh server DB."""
import os, socket, sqlite3, subprocess, tempfile, time, threading, socketserver, urllib.request, re
from pathlib import Path
root = Path(__file__).resolve().parents[1]
messages=[]
class Handler(socketserver.StreamRequestHandler):
    def handle(self):
        self.wfile.write(b'220 local-test SMTP\r\n');self.wfile.flush()
        data=False; chunks=[]
        while True:
            line=self.rfile.readline()
            if not line: break
            x=line.strip().upper()
            if data:
                if x==b'.':
                    messages.append(b''.join(chunks)); self.wfile.write(b'250 accepted\r\n');data=False;chunks=[]
                else: chunks.append(line)
            elif x.startswith(b'EHLO') or x.startswith(b'HELO'): self.wfile.write(b'250 localhost\r\n')
            elif x.startswith(b'MAIL FROM') or x.startswith(b'RCPT TO'):self.wfile.write(b'250 ok\r\n')
            elif x==b'DATA':self.wfile.write(b'354 go\r\n');data=True
            elif x==b'QUIT':self.wfile.write(b'221 bye\r\n');break
            else:self.wfile.write(b'250 ok\r\n')
            self.wfile.flush()
class Relay(socketserver.ThreadingTCPServer):allow_reuse_address=True;daemon_threads=True
with tempfile.TemporaryDirectory(prefix='lite-mailer-fixtures-') as tmp:
    dbfile=Path(tmp)/'db.sqlite'
    with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    server=subprocess.Popen([str(root/'timeclock-server'),str(dbfile),str(port)],cwd=root,env={**os.environ,'DEVICE_KEY':'test-device-key-12345'},stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    try:
        for _ in range(100):
            try:urllib.request.urlopen(f'http://127.0.0.1:{port}/health',timeout=.2);break
            except Exception:time.sleep(.04)
        else:raise AssertionError('server did not initialize')
    finally:server.terminate();server.wait(timeout=5)
    with sqlite3.connect(dbfile) as db:
        db.execute("INSERT INTO email_queue(recipient,subject,body,status,next_attempt,event_id) VALUES('payroll@example.test','Test queue','This is a test','pending',strftime('%Y-%m-%dT%H:%M:%fZ','now'),'test-mailer-1')")
        db.commit()
    def run_until(predicate,timeout=7):
        proc=subprocess.Popen([str(root/'timeclock-mailer'),str(dbfile)],cwd=root,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        try:
            deadline=time.monotonic()+timeout
            while time.monotonic()<deadline:
                with sqlite3.connect(dbfile) as db:
                    result=db.execute('SELECT status,attempts,next_attempt,last_error FROM email_queue WHERE event_id=?',('test-mailer-1',)).fetchone()
                if predicate(result):return result
                time.sleep(.07)
            raise AssertionError('mailer did not reach expected state: '+str(result))
        finally:proc.terminate();proc.wait(timeout=5)
    failed=run_until(lambda r:'SMTP is not configured' in r[3]); assert failed[0]=='pending' and failed[1]==0 and failed[2]
    with Relay(('127.0.0.1',0),Handler) as relay:
        thread=threading.Thread(target=relay.serve_forever,daemon=True);thread.start()
        with sqlite3.connect(dbfile) as db:
            db.execute("INSERT INTO smtp_settings(id,url,username,password,tls_mode,sender) VALUES(1,?,'','','none','clock@example.test')",(f'smtp://127.0.0.1:{relay.server_address[1]}',))
            db.execute("UPDATE email_queue SET next_attempt=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE event_id='test-mailer-1'")
            db.commit()
        sent=run_until(lambda r:r[0]=='sent');assert sent[1]==1 and sent[3]=='' and len(messages)==1
        assert b'Test queue' in messages[0]
        with sqlite3.connect(dbfile) as db:
            inst=db.execute('SELECT value FROM lite_install_identity WHERE id=1').fetchone()[0]
            qid=db.execute("SELECT id FROM email_queue WHERE event_id='test-mailer-1'").fetchone()[0]
        token=db.execute('SELECT message_token FROM email_queue WHERE id=?',(qid,)).fetchone()[0]
        expected=f'<mk-timeclock.{token}@example.test>'.encode()
        assert re.search(rb'^Message-ID: (.*)$',messages[0],re.M).group(1).strip()==expected
        relay.shutdown()
    print('PASS: unconfigured SMTP reports reason and defers; configured SMTP accepts exactly one message')
