"""Scheduled backup obeys shared SMTP transport, preserving exact attachment."""
import email.policy
import email
import runpy
import socketserver
import sqlite3
import tempfile
import threading
from pathlib import Path
from unittest.mock import patch, MagicMock
root=Path(__file__).resolve().parents[1]
send=runpy.run_path(str(root/'timeclock-backup'))['email_scheduled']
class SMTP(socketserver.StreamRequestHandler):
    messages=[]
    def handle(self):
        self.wfile.write(b'220 test\r\n');self.wfile.flush();body=None
        while True:
            line=self.rfile.readline()
            if not line:return
            if body is not None:
                if line==b'.\r\n':
                    SMTP.messages.append(b''.join(body));body=None;reply=b'250 accepted\r\n'
                else:body.append(line[1:] if line.startswith(b'..') else line);continue
            elif line.upper().startswith(b'DATA'):body=[];reply=b'354 send\r\n'
            elif line.upper().startswith(b'QUIT'):self.wfile.write(b'221 bye\r\n');self.wfile.flush();return
            else:reply=b'250 okay\r\n'
            self.wfile.write(reply);self.wfile.flush()
class Relay(socketserver.ThreadingTCPServer):allow_reuse_address=True;daemon_threads=True
with tempfile.TemporaryDirectory() as tmp:
    dbpath=Path(tmp)/'test.db';archive=Path(tmp)/'timeclock-test.tar.gz'
    archive.write_bytes(b'\x1f\x8bexact backup attachment\x00\xff')
    with sqlite3.connect(dbpath) as db:
        db.executescript('''CREATE TABLE backup_settings(id,email_enabled,manager_id);
        CREATE TABLE employees(id,email,active);
        CREATE TABLE employee_roles(employee_id,role);
        CREATE TABLE user_accounts(employee_id,disabled);
        CREATE TABLE smtp_settings(id,url,username,password,tls_mode,sender,sender_name);
        INSERT INTO backup_settings VALUES(1,1,7);
        INSERT INTO employees VALUES(7,'manager@example.test',1);
        INSERT INTO employee_roles VALUES(7,'manager');
        INSERT INTO user_accounts VALUES(7,0);
        INSERT INTO smtp_settings VALUES(1,'smtp://relay.example.test','','','none','clock@example.test','iCube Development Timeclock');''')
    def config(url,mode):
        with sqlite3.connect(dbpath) as db:db.execute('UPDATE smtp_settings SET url=?,tls_mode=?',(url,mode))
    for mode,url,port in [('none','smtp://relay.example.test',25),('starttls','smtp://relay.example.test',587),('implicit','smtps://relay.example.test',465)]:
        config(url,mode)
        with patch('smtplib.SMTP') as plain,patch('smtplib.SMTP_SSL') as secure:
            send(archive,dbpath)
            factory=secure if mode=='implicit' else plain
            assert factory.call_args.args==('relay.example.test',port)
            other=plain if mode=='implicit' else secure
            other.assert_not_called()
            client=factory.return_value.__enter__.return_value
            assert client.starttls.call_count==(1 if mode=='starttls' else 0)
            client.send_message.assert_called_once()
    with Relay(('127.0.0.1',0),SMTP) as relay:
        threading.Thread(target=relay.serve_forever,daemon=True).start()
        config(f'smtp://127.0.0.1:{relay.server_address[1]}','none')
        send(archive,dbpath)
        msg=email.message_from_bytes(SMTP.messages[0],policy=email.policy.default)
        assert msg['To']=='manager@example.test'
        assert msg['From'].addresses[0].display_name=='iCube Development Timeclock'
        assert msg['From'].addresses[0].addr_spec=='clock@example.test'
        attachment=list(msg.iter_attachments())
        assert len(attachment)==1 and attachment[0].get_payload(decode=True)==archive.read_bytes()
        relay.shutdown()
    for url,mode in [('smtps://relay.example.test','none'),('smtp://relay.example.test','implicit'),('smtp://relay.example.test','unknown')]:
        config(url,mode)
        with patch('smtplib.SMTP') as plain,patch('smtplib.SMTP_SSL') as secure:
            try:send(archive,dbpath)
            except ValueError:pass
            else:raise AssertionError('invalid SMTP transport accepted')
            plain.assert_not_called();secure.assert_not_called()
print('PASS: backup SMTP none defaults to 25; STARTTLS/SSL retained; explicit port delivery preserves exact backup; invalid modes rejected')
