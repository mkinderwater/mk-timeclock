"""HTTP + SQLite regression: future periods can be regenerated safely; frozen revisions resist REPLACE."""
import json,os,socket,sqlite3,subprocess,tempfile,time,urllib.request,urllib.parse,urllib.error
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='lite-cycle-') as td:
    path=Path(td)/'db.sqlite'
    with socket.socket() as sock:
        sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
    proc=subprocess.Popen([str(root/'timeclock-server'),str(path),str(port)],env={**os.environ,'DEVICE_KEY':'test-device-key-12345'},stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    try:
        for _ in range(100):
            try:
                urllib.request.urlopen(f'http://127.0.0.1:{port}/health',timeout=.2);break
            except Exception:time.sleep(.04)
        else:raise AssertionError('Server unavailable')
        with sqlite3.connect(path) as db:
            db.execute("INSERT INTO employees(id,name,email) VALUES(1,'Manager','mgr@example.test')")
            db.execute("INSERT INTO employee_roles VALUES(1,'manager')")
            db.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
            db.execute("INSERT INTO manager_cards(uid,manager_id,active) VALUES('A1B2C3D4',1,1)")
            db.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+600)",('d'*64,))
        def req(route,fields=None):
            data=urllib.parse.urlencode(fields).encode() if fields is not None else None
            h={'Cookie':'tc_session='+'d'*64,'Content-Type':'application/x-www-form-urlencoded'}
            with urllib.request.urlopen(urllib.request.Request(f'http://127.0.0.1:{port}'+route,data=data,headers=h),timeout=4) as r:return json.load(r)
        req('/api/v1/admin/periods/cycle',{'cycle':'semi_monthly','anchor':'2026-01-01'})
        with sqlite3.connect(path) as db:
            original=db.execute('SELECT starts,ends FROM pay_periods ORDER BY starts').fetchall()
            assert original
        req('/api/v1/admin/periods/cycle',{'cycle':'biweekly','anchor':'2026-10-05'})
        with sqlite3.connect(path) as db:
            after=db.execute('SELECT starts,ends FROM pay_periods ORDER BY starts').fetchall()
            assert all(x in after for x in original if x[0]<=time.strftime('%Y-%m-%d'))
        with sqlite3.connect(path) as db:
            before=db.execute('SELECT starts,ends FROM pay_periods ORDER BY starts').fetchall()
        req('/api/v1/admin/periods/cycle',{'cycle':'biweekly','anchor':'2026-10-05'})
        with sqlite3.connect(path) as db:
            current=db.execute('SELECT starts,ends FROM pay_periods ORDER BY starts').fetchall()
            assert all(x in current for x in before if x[0]<=time.strftime('%Y-%m-%d')), (before,current)
            assert all(a[1]<b[0] for a,b in zip(current,current[1:])), current
            pid=db.execute('SELECT id FROM pay_periods LIMIT 1').fetchone()[0]
            db.execute('INSERT INTO lite_period_snapshots(period_id,revision,starts,ends,csv) VALUES(?,?,?,?,?)',(pid,1,'2026-10-01','2026-10-15','ORIGINAL'))
            db.execute('INSERT OR REPLACE INTO lite_period_snapshots(period_id,revision,starts,ends,csv) VALUES(?,?,?,?,?)',(pid,1,'2026-10-01','2026-10-15','REPLACED'))
            assert db.execute('SELECT csv FROM lite_period_snapshots WHERE period_id=? AND revision=1',(pid,)).fetchone()[0]=='ORIGINAL'
            db.execute('INSERT OR IGNORE INTO lite_period_snapshots(period_id,revision,starts,ends,csv) VALUES(?,?,?,?,?)',(pid,1,'2026-10-01','2026-10-15','IGNORED'))
            db.execute('INSERT INTO lite_period_snapshots(period_id,revision,starts,ends,csv) VALUES(?,?,?,?,?)',(pid,2,'2026-10-01','2026-10-15','REV2'))
        print('PASS: cycle changes regenerate unused future periods, unchanged settings are stable, snapshots resist REPLACE')
    finally:
        proc.terminate()
        try:proc.communicate(timeout=2)
        except subprocess.TimeoutExpired:proc.kill();proc.communicate(timeout=3)
