"""Fresh databases must have distinct mail identities; restarting preserves the identity."""
import os, socket, sqlite3, subprocess, tempfile, time, urllib.request
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    ids=[]
    for n in range(2):
        db=Path(tmp)/f'{n}.sqlite'
        with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
        p=subprocess.Popen([str(root/'timeclock-server'),str(db),str(port)],cwd=root,env={**os.environ,'DEVICE_KEY':'test-device-key-12345'},stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        try:
            for _ in range(100):
                try:urllib.request.urlopen(f'http://127.0.0.1:{port}/health',timeout=.2);break
                except Exception:time.sleep(.04)
            else:raise AssertionError('server unavailable')
        finally:p.terminate();p.wait(timeout=5)
        with sqlite3.connect(db) as c:
            first=c.execute('select value from lite_install_identity where id=1').fetchone()[0]
            assert len(first)==32 and all(ch in '0123456789abcdef' for ch in first)
            c.execute('INSERT OR IGNORE INTO lite_install_identity(id,value) VALUES(1,lower(hex(randomblob(16))))')
            second=c.execute('select value from lite_install_identity where id=1').fetchone()[0]
            assert first==second
            ids.append(first)
    assert ids[0]!=ids[1]
print('PASS: different fresh database identities; identity stable across restarts')
