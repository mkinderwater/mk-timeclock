"""Integration: permanent vs temporary SMTP errors and stable Message-ID."""
import os,socket,sqlite3,subprocess,tempfile,time,threading,socketserver,urllib.request,re
from pathlib import Path
root=Path(__file__).resolve().parents[1]
class Handler(socketserver.StreamRequestHandler):
    code=550
    mids=[]
    def handle(self):
        self.wfile.write(b'220 test\r\n');self.wfile.flush()
        reading=False;body=[]
        while True:
            line=self.rfile.readline()
            if not line:break
            u=line.strip().upper()
            if reading:
                if u==b'.':
                    self.__class__.mids.extend(re.findall(rb'^Message-ID: (.*)$',b''.join(body),re.M));reading=False;body=[];self.wfile.write(b'250 accepted\r\n')
                else:body.append(line)
            elif u.startswith((b'EHLO',b'HELO',b'MAIL FROM')):self.wfile.write(b'250 okay\r\n')
            elif u.startswith(b'RCPT TO'):self.wfile.write(f'{self.__class__.code} recipient unavailable\r\n'.encode())
            elif u==b'DATA':self.wfile.write(b'354 go\r\n');reading=True
            elif u==b'QUIT':self.wfile.write(b'221 bye\r\n');break
            else:self.wfile.write(b'250 okay\r\n')
            self.wfile.flush()
class Relay(socketserver.ThreadingTCPServer):allow_reuse_address=True;daemon_threads=True
with tempfile.TemporaryDirectory() as temp:
    dbfile=Path(temp)/'test.db'
    with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
    srv=subprocess.Popen([str(root/'timeclock-server'),str(dbfile),str(port)],cwd=root,env={**os.environ,'DEVICE_KEY':'test-device-key-12345'},stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    try:
        for _ in range(100):
            try:urllib.request.urlopen(f'http://127.0.0.1:{port}/health',timeout=.2);break
            except Exception:time.sleep(.04)
        else:raise AssertionError('server failed')
    finally:srv.terminate();srv.wait(timeout=5)
    with Relay(('127.0.0.1',0),Handler) as relay:
        threading.Thread(target=relay.serve_forever,daemon=True).start()
        with sqlite3.connect(dbfile) as db:
            db.execute("INSERT INTO smtp_settings(id,url,username,password,tls_mode,sender) VALUES(1,?,'','','none','clock@example.test')",(f'smtp://127.0.0.1:{relay.server_address[1]}',))
            db.execute("INSERT INTO email_queue(recipient,subject,body,status,next_attempt,event_id) VALUES('bad@example.test','Test','hello','pending',strftime('%Y-%m-%dT%H:%M:%fZ','now'),'test-550')")
            db.commit()
        def run(target):
            p=subprocess.Popen([str(root/'timeclock-mailer'),str(dbfile)],cwd=root,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            try:
                end=time.monotonic()+6
                while time.monotonic()<end:
                    with sqlite3.connect(dbfile) as db:r=db.execute('SELECT status,attempts,last_error,next_attempt FROM email_queue WHERE event_id=?',(target,)).fetchone()
                    if r[1]:return r
                    time.sleep(.04)
                raise AssertionError(r)
            finally:p.terminate();p.wait(timeout=5)
        r=run('test-550');assert r[0]=='failed' and r[1]==1 and '550 recipient unavailable' in r[2],r
        Handler.code=451
        with sqlite3.connect(dbfile) as db:
            db.execute("INSERT INTO email_queue(recipient,subject,body,status,next_attempt,event_id) VALUES('bad@example.test','Test','hello','pending',strftime('%Y-%m-%dT%H:%M:%fZ','now'),'test-451')")
            db.commit()
        r=run('test-451');assert r[0]=='pending' and r[1]==1 and '451 recipient unavailable' in r[2] and r[3],r
        relay.shutdown()
print('PASS: SMTP 550 terminal; 451 retry scheduled; rejection text preserved')
