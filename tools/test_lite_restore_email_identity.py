#!/usr/bin/env python3
"""One queued email's identity survives retries; a restored pre-send backup cannot reuse it."""
import sqlite3, subprocess, tempfile, time, urllib.request, socket, os, shutil
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    dbfile=Path(tmp)/'test.db'
    with socket.socket() as sk: sk.bind(('127.0.0.1',0));port=sk.getsockname()[1]
    server=subprocess.Popen([str(root/'timeclock-server'),str(dbfile),str(port)],cwd=root,env={**os.environ,'DEVICE_KEY':'test-device-key-12345'},stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    try:
        for i in range(100):
            try:urllib.request.urlopen(f'http://127.0.0.1:{port}/health',timeout=.2);break
            except Exception:time.sleep(.04)
        else:raise AssertionError('server did not start')
    finally:server.terminate();server.wait(timeout=5)
    with sqlite3.connect(dbfile) as db,sqlite3.connect(Path(tmp)/'backup.db') as backup:db.backup(backup)
    with sqlite3.connect(dbfile) as db:
        db.execute("INSERT INTO email_queue(recipient,subject,body) VALUES('a@example.test','First','body')")
        before=db.execute('SELECT id,message_token FROM email_queue').fetchone()
        assert len(before[1])==32
        db.commit()
    with sqlite3.connect(Path(tmp)/'backup.db') as backup,sqlite3.connect(dbfile) as db:backup.backup(db)
    with sqlite3.connect(dbfile) as db:
        db.execute("INSERT INTO email_queue(recipient,subject,body) VALUES('a@example.test','Second','body')")
        after=db.execute('SELECT id,message_token FROM email_queue').fetchone()
        assert before[0]==after[0] and before[1]!=after[1],(before,after)
        assert db.execute('SELECT message_token FROM email_queue').fetchone()[0]==after[1]
print('PASS: restored database reuses queue ID but generates new random message token')
