"""Real HTTP acceptance for manager override when employee has not approved."""
import json, os, socket, sqlite3, subprocess, tempfile, time, urllib.error, urllib.parse, urllib.request
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='lite-approval-') as tmp:
    path=Path(tmp)/'new.db'
    with socket.socket() as sk:
        sk.bind(('127.0.0.1',0)); port=sk.getsockname()[1]
    proc=subprocess.Popen([str(root/'timeclock-server'),str(path),str(port)],env={**os.environ,'DEVICE_KEY':'test-device-key-12345'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        for _ in range(100):
            try:
                with urllib.request.urlopen(f'http://127.0.0.1:{port}/health',timeout=.15):break
            except Exception:time.sleep(.05)
        else:raise RuntimeError(proc.stderr.read().decode())
        with sqlite3.connect(path) as db:
            db.execute("INSERT INTO employees(id,name,email) VALUES(1,'Manager','mgr@example.test')")
            db.execute("INSERT INTO employees(id,name,email) VALUES(2,'Worker','worker@example.test')")
            db.execute("INSERT INTO employee_roles VALUES(1,'manager')")
            db.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
            db.execute("INSERT INTO manager_cards(uid,manager_id,active) VALUES('A1B2C3D4',1,1)")
            db.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+600)",('d'*64,))
            db.execute("INSERT INTO pay_periods(starts,ends) VALUES('2026-10-01','2026-10-15')")
            # Only employees with actual period activity may be approved.
            db.execute("INSERT INTO attendance_absences(employee_id,manager_id,day,reason,note) VALUES(2,1,'2026-10-06','Sick','')")
            body=db.execute("SELECT body FROM email_templates WHERE id='auto_review_employee'").fetchone()[0]
            assert 'Please review and approve' in body
            assert db.execute("SELECT enabled FROM email_templates WHERE id='override_employee'").fetchone()[0]==0
        def request(path, data=None):
            headers={'Cookie':'tc_session='+'d'*64}
            if data is not None:
                data=urllib.parse.urlencode(data).encode();headers['Content-Type']='application/x-www-form-urlencoded'
            try:
                with urllib.request.urlopen(urllib.request.Request(f'http://127.0.0.1:{port}'+path,data=data,headers=headers),timeout=5) as r:return r.status,r.read().decode()
            except urllib.error.HTTPError as e:return e.code,e.read().decode()
        status,page=request('/pay-periods')
        assert status==200,(status,page)
        assert 'Approve without confirmation' not in page and 'Not confirmed' not in page
        assert 'Employees confirm their hours' not in page
        params={'action':'sign','starts':'2026-10-01','ends':'2026-10-15','employee_id':'2','reason':'Manager reviewed'}
        # An employee without period activity cannot inflate the approval count.
        code,body=request('/api/v1/admin/periods',{**params,'employee_id':'1'})
        assert code==409 and 'no punches or leave' in body,(code,body)
        code,body=request('/api/v1/admin/periods',params)
        assert code==200,(code,body)
        code,body=request('/api/v1/admin/periods')
        assert code==200,(code,body)
        period=next(p for p in json.loads(body)['periods'] if p['starts']=='2026-10-01' and p['ends']=='2026-10-15')
        assert period['manager_signoffs']==1 and period['approval_total']==1,period
        with sqlite3.connect(path) as db:
            assert db.execute("SELECT unconfirmed FROM period_signoffs WHERE employee_id=2 AND role='manager'").fetchone()==(1,)
            assert db.execute("SELECT count(*) FROM period_signoffs WHERE role='employee'").fetchone()[0]==0
            assert db.execute("SELECT count(*) FROM email_queue WHERE event_id LIKE 'period-override:%'").fetchone()[0]==0
            assert db.execute("SELECT count(*) FROM period_approval_audit WHERE action='manager_override'").fetchone()[0]==1
        code,_=request('/api/v1/admin/periods',{**params,'action':'sign_unconfirmed'})
        assert code==400,code
        print('PASS: manager override without employee approval is recorded and audited; old override route rejected')
    finally:
        proc.terminate();proc.communicate(timeout=5)
