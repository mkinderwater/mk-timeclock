"""Real HTTP test: a slow Excel converter cannot stall the primary request loop."""
import io, json, os, socket, sqlite3, subprocess, tempfile, threading, time
import urllib.request, urllib.parse, urllib.error, zipfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='lite-xlsx-worker-') as tmp:
    d=Path(tmp)
    (d/'sitecustomize.py').write_text('import time\ntime.sleep(3)\n')
    dbfile=d/'db.sqlite'
    with socket.socket() as s:
        s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    proc=subprocess.Popen([str(root/'timeclock-server'),str(dbfile),str(port)],cwd=root,env={**os.environ,'DEVICE_KEY':'test-device-key-12345','PYTHONPATH':str(d)},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        base=f'http://127.0.0.1:{port}'
        for _ in range(100):
            try:urllib.request.urlopen(base+'/health',timeout=.2);break
            except Exception:time.sleep(.04)
        else:raise AssertionError('server not ready')
        with sqlite3.connect(dbfile) as db:
            db.execute("INSERT INTO employees(id,name,email) VALUES(1,'Manager','mgr@example.test')")
            db.execute("INSERT INTO employee_roles VALUES(1,'manager')")
            db.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
            db.execute("INSERT INTO manager_cards(uid,manager_id,active) VALUES('A1B2C3D4',1,1)")
            db.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+600)",('d'*64,))
            db.execute("INSERT INTO pay_periods(starts,ends,status) VALUES('2026-10-01','2026-10-15','closed')")
            pid=db.execute('SELECT last_insert_rowid()').fetchone()[0]
        def req(path,data=None,timeout=12):
            body=urllib.parse.urlencode(data).encode() if data is not None else None
            r=urllib.request.Request(base+path,data=body,headers={'Cookie':'tc_session='+'d'*64,'Content-Type':'application/x-www-form-urlencoded'})
            with urllib.request.urlopen(r,timeout=timeout) as response:return response.read()
        assert json.loads(req('/api/v1/admin/periods/exports',{'period_id':pid}))['revision']==1
        path=f'/api/v1/admin/periods/export.xlsx?period_id={pid}&revision=1'
        result=[]
        thread=threading.Thread(target=lambda:result.append(req(path)))
        thread.start();time.sleep(.5)
        start=time.monotonic();assert urllib.request.urlopen(base+'/health',timeout=1).status==200
        assert time.monotonic()-start<1.0,'server blocked while converting Excel'
        try:req(path,timeout=2)
        except urllib.error.HTTPError as err:assert err.code==503,err.code
        else:raise AssertionError('second converter was allowed to start')
        thread.join(timeout=12);assert not thread.is_alive() and len(result)==1
        with zipfile.ZipFile(io.BytesIO(result[0])) as z:assert 'xl/workbook.xml' in z.namelist()
        print('PASS: slow XLSX converter does not block HTTP; second converter limited; XLSX valid')
    finally:
        proc.terminate();proc.communicate(timeout=5)
