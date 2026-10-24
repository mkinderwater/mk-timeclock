"""HTTP save/read validation and current-RC3 settings upgrade preserves data."""
import json,os,socket,sqlite3,subprocess,tempfile,time,urllib.request,urllib.parse,urllib.error
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    dbpath=Path(tmp)/'clock.db'
    # Exact current RC3 settings before send-time fields existed.
    with sqlite3.connect(dbpath) as db:
        db.executescript("""CREATE TABLE payroll_settings(id INTEGER PRIMARY KEY CHECK(id=1),anchor_payday TEXT NOT NULL DEFAULT '',lead_days INTEGER NOT NULL DEFAULT 3,recipient TEXT NOT NULL DEFAULT '',enabled INTEGER NOT NULL DEFAULT 0,enabled_since TEXT NOT NULL DEFAULT '');
        CREATE TABLE review_send_settings(id INTEGER PRIMARY KEY CHECK(id=1),enabled INTEGER NOT NULL DEFAULT 1,days_after INTEGER NOT NULL DEFAULT 1 CHECK(days_after BETWEEN 0 AND 7));
        INSERT INTO payroll_settings VALUES(1,'2026-10-15',0,'payroll@example.test',0,'2026-10-01');
        INSERT INTO review_send_settings VALUES(1,0,0);""")
    with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    proc=subprocess.Popen([str(root/'timeclock-server'),str(dbpath),str(port)],cwd=root,env={**os.environ,'DEVICE_KEY':'test-device-key-12345'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        base=f'http://127.0.0.1:{port}'
        for _ in range(100):
            try:urllib.request.urlopen(base+'/health',timeout=.2).close();break
            except OSError:time.sleep(.04)
        with sqlite3.connect(dbpath) as db:
            assert db.execute('SELECT anchor_payday,lead_days,recipient,enabled_since,send_time FROM payroll_settings').fetchone()==('2026-10-15',0,'payroll@example.test','2026-10-01','00:00')
            assert db.execute('SELECT enabled,days_after,send_time FROM review_send_settings').fetchone()==(0,0,'00:00')
            db.execute("INSERT INTO employees(id,name,email) VALUES(1,'Manager','manager@example.test')")
            db.execute("INSERT INTO employee_roles VALUES(1,'manager')")
            db.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
            db.execute("INSERT INTO manager_cards VALUES('A1B2C3D4',1,1)")
            db.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+3600)",('d'*64,))
        def api(path,form=None,authenticated=True):
            headers={'Content-Type':'application/x-www-form-urlencoded'}
            if authenticated:headers['Cookie']='tc_session='+'d'*64
            data=urllib.parse.urlencode(form).encode() if form is not None else None
            with urllib.request.urlopen(urllib.request.Request(base+path,data=data,headers=headers),timeout=5) as response:return json.load(response)
        review='/api/v1/admin/review/auto-settings'
        assert api(review)['send_time']=='00:00'
        for value in ['00:00','08:00','23:59']:
            api(review,dict(enabled='0',days='0',send_time=value));assert api(review)==dict(enabled=False,days=0,send_time=value)
        for value in ['24:00','12:60','8:00','','08:00:00','08:00\x00bad','08:00\r\nBad']:
            try:api(review,dict(enabled='1',days='0',send_time=value))
            except urllib.error.HTTPError as e:assert e.code==400
            else:raise AssertionError('invalid review time accepted: '+repr(value))
        assert api(review)['send_time']=='23:59'
        params=dict(anchor='2026-10-15',before='0',recipient='payroll@example.test',enabled='0',send_time='15:00')
        for value in ['24:00','15:60','3:00','','15:00\x00bad']:
            try:api('/api/v1/admin/payroll/arm',{**params,'send_time':value})
            except urllib.error.HTTPError as e:assert e.code==400
            else:raise AssertionError('invalid payroll time accepted: '+repr(value))
        request=api('/api/v1/admin/payroll/arm',params)
        assert api('/api/v1/admin/payroll')['send_time']=='00:00'  # approval required
        with sqlite3.connect(dbpath) as db:db.execute("UPDATE manager_scan SET uid='A1B2C3D4',scanned_at=armed_at WHERE id=1")
        assert api('/api/v1/admin/payroll/status',{'token':request['token']})['status']=='saved'
        saved=api('/api/v1/admin/payroll');assert saved['send_time']=='15:00' and saved['lead']==0
        try:api(review,dict(enabled='1',days='0',send_time='01:00'),authenticated=False)
        except urllib.error.HTTPError as e:assert e.code in (401,403)
        else:raise AssertionError('unauthenticated settings change accepted')
        assert api(review)['send_time']=='23:59'
    finally:proc.terminate();proc.communicate(timeout=5)
print('PASS: current settings upgrade preserves day/recipient/enabled state and defaults to midnight; independent time save/read, zero days, boundaries, invalid/control rejection, payroll card approval and settings authentication')
