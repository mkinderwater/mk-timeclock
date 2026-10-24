"""One frequency drives fresh defaults, report settings and period changes."""
import json,os,socket,sqlite3,subprocess,tempfile,time,urllib.request,urllib.parse,urllib.error
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    path=Path(tmp)/'clock.db'
    with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    proc=subprocess.Popen([str(root/'timeclock-server'),str(path),str(port)],env={**os.environ,'DEVICE_KEY':'test-device-key-12345'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        base=f'http://127.0.0.1:{port}'
        for _ in range(100):
            try:urllib.request.urlopen(base+'/health',timeout=.2).close();break
            except OSError:time.sleep(.04)
        else:raise AssertionError('server startup failed')
        with sqlite3.connect(path) as db:
            assert 'cadence' not in [r[1] for r in db.execute('PRAGMA table_info(payroll_settings)')]
            db.execute("INSERT INTO employees(id,name,email) VALUES(1,'Manager','manager@example.test')")
            db.execute("INSERT INTO employee_roles VALUES(1,'manager')")
            db.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
            db.execute("INSERT INTO manager_cards(uid,manager_id,active) VALUES('A1B2C3D4',1,1)")
            db.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+600)",('d'*64,))
        def req(route,form=None):
            body=urllib.parse.urlencode(form).encode() if form is not None else None
            r=urllib.request.Request(base+route,data=body,headers={'Cookie':'tc_session='+'d'*64})
            with urllib.request.urlopen(r,timeout=10) as response:return json.load(response)
        assert req('/api/v1/admin/payroll')['cadence']==req('/api/v1/admin/periods/cycle')['cycle']=='semi_monthly'
        settings={'anchor':'2026-10-15','before':'0','recipient':'payroll@example.test','enabled':'1'}
        approval=req('/api/v1/admin/payroll/arm',settings)
        with sqlite3.connect(path) as db:db.execute("UPDATE manager_scan SET uid='A1B2C3D4',scanned_at=armed_at+1 WHERE id=1")
        assert req('/api/v1/admin/payroll/status',{'token':approval['token']})['status']=='saved'
        for cycle in ('biweekly','weekly','monthly','semi_monthly'):
            req('/api/v1/admin/periods/cycle',{'cycle':cycle,'anchor':'2026-10-01'})
            current=req('/api/v1/admin/payroll')
            assert current['cadence']==req('/api/v1/admin/periods/cycle')['cycle']==cycle
            assert current['anchor']=='2026-10-15' and current['lead']==0 and current['enabled']
        # Invalid twice-monthly payday anchors cannot silently suppress dispatch.
        try:req('/api/v1/admin/payroll/arm',{**settings,'anchor':'2026-10-19'})
        except urllib.error.HTTPError as e:assert e.code==400
        else:raise AssertionError('invalid twice-monthly payday anchor accepted')
        assert req('/api/v1/admin/payroll')['anchor']=='2026-10-15'
        req('/api/v1/admin/periods/cycle',{'cycle':'weekly','anchor':'2026-10-01'})
        approval=req('/api/v1/admin/payroll/arm',{**settings,'anchor':'2026-10-19'})
        with sqlite3.connect(path) as db:db.execute("UPDATE manager_scan SET uid='A1B2C3D4',scanned_at=armed_at+1 WHERE id=1")
        req('/api/v1/admin/payroll/status',{'token':approval['token']})
        try:req('/api/v1/admin/periods/cycle',{'cycle':'semi_monthly','anchor':'2026-10-01'})
        except urllib.error.HTTPError as e:assert e.code==409
        else:raise AssertionError('cycle change broke enabled report payday')
        assert req('/api/v1/admin/periods/cycle')['cycle']=='weekly'
        approval=req('/api/v1/admin/payroll/arm',{**settings,'anchor':'2026-10-19','enabled':'0'})
        with sqlite3.connect(path) as db:db.execute("UPDATE manager_scan SET uid='A1B2C3D4',scanned_at=armed_at+1 WHERE id=1")
        req('/api/v1/admin/payroll/status',{'token':approval['token']})
        try:req('/api/v1/admin/periods/cycle',{'cycle':'semi_monthly','anchor':'2026-10-01'})
        except urllib.error.HTTPError as e:assert e.code==409
        else:raise AssertionError('disabled report bypassed payday anchor guard')
        assert req('/api/v1/admin/periods/cycle')['cycle']=='weekly'
        print('PASS: one frequency, matching fresh defaults, all four cycles, preserved report timing, invalid payday transitions rejected')
    finally:proc.terminate();proc.communicate(timeout=5)
