"""Functional HTTP regression: immutable XLSX versions are downloadable and never emailed."""
import io, json, os, socket, sqlite3, subprocess, tempfile, time, urllib.request, urllib.parse, urllib.error, zipfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='lite-exports-') as td:
    path=Path(td)/'db.sqlite'
    with socket.socket() as sock:
        sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
    proc=subprocess.Popen([str(root/'timeclock-server'),str(path),str(port)],cwd=root,env={**os.environ,'DEVICE_KEY':'test-device-key-12345'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        for _ in range(100):
            try:urllib.request.urlopen(f'http://127.0.0.1:{port}/health',timeout=.2);break
            except Exception:time.sleep(.04)
        else:raise AssertionError('Server unavailable')
        with sqlite3.connect(path) as db:
            db.execute("INSERT INTO employees(id,name,email) VALUES(1,'Manager','mgr@example.test')")
            db.execute("INSERT INTO employee_roles VALUES(1,'manager')")
            db.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
            db.execute("INSERT INTO manager_cards(uid,manager_id,active) VALUES('A1B2C3D4',1,1)")
            db.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+600)",('d'*64,))
            db.execute("INSERT INTO pay_periods(starts,ends,status) VALUES('2026-10-01','2026-10-15','closed')")
            pid=db.execute('SELECT last_insert_rowid()').fetchone()[0]
        def req(route,fields=None):
            data=urllib.parse.urlencode(fields).encode() if fields is not None else None
            h={'Cookie':'tc_session='+'d'*64,'Content-Type':'application/x-www-form-urlencoded'}
            with urllib.request.urlopen(urllib.request.Request(f'http://127.0.0.1:{port}'+route,data=data,headers=h),timeout=10) as r:return r.read()
        assert json.loads(req('/api/v1/admin/periods/exports',{'period_id':pid}))['revision']==1
        versions=json.loads(req('/api/v1/admin/periods/exports?period_id='+str(pid)))['revisions']
        assert [r['revision'] for r in versions]==[1],versions
        data=req('/api/v1/admin/periods/export.xlsx?period_id='+str(pid)+'&revision=1')
        with zipfile.ZipFile(io.BytesIO(data)) as z:
            assert 'xl/workbook.xml' in z.namelist()
        try:req('/api/v1/admin/periods/exports',{'period_id':pid})
        except urllib.error.HTTPError as e:assert e.code==409
        else:raise AssertionError('Unchanged export created a new revision')
        with sqlite3.connect(path) as db:
            assert db.execute('SELECT count(*) FROM lite_period_snapshots').fetchone()[0]==1
            assert db.execute('SELECT count(*) FROM email_queue').fetchone()[0]==0
        # A simulated corrected source is stored as revision 2; revision 1 bytes stay intact.
        with sqlite3.connect(path) as db:
            original=db.execute('SELECT csv FROM lite_period_snapshots WHERE period_id=? AND revision=1',(pid,)).fetchone()[0]
            db.execute('INSERT INTO lite_period_snapshots(period_id,revision,starts,ends,csv) VALUES(?,2,?,?,?)',(pid,'2026-10-01','2026-10-15',original+'\n'))
        second=req('/api/v1/admin/periods/export.xlsx?period_id='+str(pid)+'&revision=2')
        with zipfile.ZipFile(io.BytesIO(second)) as z:assert 'xl/workbook.xml' in z.namelist()
        assert req('/api/v1/admin/periods/export.xlsx?period_id='+str(pid)+'&revision=1')==data
        print('PASS: authenticated create, list, XLSX download, no duplicate, unchanged history, no queue')
    finally:
        proc.terminate();proc.communicate(timeout=5)
