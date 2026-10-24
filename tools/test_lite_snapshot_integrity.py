"""Fresh server DB: payroll source snapshots cannot be overwritten or deleted."""
import os, socket, sqlite3, subprocess, tempfile, time, urllib.request
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='lite-snapshots-') as tmp:
    dbpath=Path(tmp)/'fresh.db'
    with socket.socket() as sock:
        sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
    proc=subprocess.Popen([str(root/'timeclock-server'),str(dbpath),str(port)],env={**os.environ,'DEVICE_KEY':'test-device-key-12345'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        for _ in range(100):
            try:
                with urllib.request.urlopen(f'http://127.0.0.1:{port}/health', timeout=.2): break
            except Exception: time.sleep(.05)
        else: raise RuntimeError(proc.stderr.read().decode())
        with sqlite3.connect(dbpath) as db:
            db.execute("INSERT INTO pay_periods(starts,ends) VALUES('2026-10-01','2026-10-15')")
            db.execute("INSERT INTO lite_period_snapshots(period_id,revision,starts,ends,csv) VALUES(1,1,'2026-10-01','2026-10-15','Original export')")
            db.execute("INSERT OR IGNORE INTO lite_period_snapshots(period_id,revision,starts,ends,csv) VALUES(1,1,'2026-10-01','2026-10-15','Replacement')")
            assert db.execute('SELECT csv FROM lite_period_snapshots').fetchone()[0]=='Original export'
            for sql in ["UPDATE lite_period_snapshots SET csv='Replacement'", 'DELETE FROM lite_period_snapshots']:
                try: db.execute(sql)
                except sqlite3.IntegrityError: pass
                else: raise AssertionError('historical export changed')
            db.execute("INSERT INTO lite_period_snapshots(period_id,revision,starts,ends,csv) VALUES(1,2,'2026-10-01','2026-10-15','Revised export')")
            assert db.execute('SELECT revision,csv FROM lite_period_snapshots ORDER BY revision').fetchall()==[(1,'Original export'),(2,'Revised export')]
    finally:
        proc.terminate();proc.communicate(timeout=5)
print('PASS: fresh database snapshots retain original, block edits/deletes, and allow distinct revisions')
