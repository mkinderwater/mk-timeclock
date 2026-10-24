"""Real HTTP: optional allowance, annual usage, unrestricted requests and frozen exports."""
import csv,ctypes,ctypes.util,io,json,os,socket,sqlite3,subprocess,tempfile,time,urllib.request,urllib.parse,urllib.error,zipfile,xml.etree.ElementTree as ET
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    dbpath=Path(tmp)/'clock.db'
    with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    proc=subprocess.Popen([str(root/'timeclock-server'),str(dbpath),str(port)],cwd=root,env={**os.environ,'DEVICE_KEY':'test-device-key-12345','TZ':'America/Edmonton'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        base=f'http://127.0.0.1:{port}'
        for _ in range(100):
            try:urllib.request.urlopen(base+'/health',timeout=.2).close();break
            except OSError:time.sleep(.04)
        with sqlite3.connect(dbpath) as db:
            db.execute("INSERT INTO employees(id,name,email) VALUES(1,'Test Manager','manager@example.test')")
            db.execute("INSERT INTO employee_roles VALUES(1,'manager')")
            db.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
            db.execute("INSERT INTO manager_cards VALUES('A1B2C3D4',1,1)")
            db.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+3600)",('d'*64,))
            db.execute('UPDATE review_send_settings SET enabled=0')
        def call(path,form=None,manager=True,raw=False):
            headers={'Cookie':'tc_session='+'d'*64} if manager else {}
            if form is not None:headers['Content-Type']='application/x-www-form-urlencoded'
            data=urllib.parse.urlencode(form).encode() if form is not None else None
            with urllib.request.urlopen(urllib.request.Request(base+path,data=data,headers=headers),timeout=7) as response:
                content=response.read();return content if raw else json.loads(content)
        fields=dict(id='0',first_name='Sarah',last_name='Chen',position='Technician',email='',phone='',active='1',manager='0',uid='',late_email_enabled='0',auto_review_enabled='0',attendance_mode='vacation_only',vacation_days='15')
        employee=call('/api/v1/admin/employees/save',fields)['id'];fields['id']=str(employee)
        def listed():return next(e for e in call('/api/v1/admin/employees')['employees'] if e['id']==employee)
        assert listed()['vacation_days']==15
        for value,expected in [('0',0),('15.5',15.5),('',None),('15',15)]:
            call('/api/v1/admin/employees/save',{**fields,'vacation_days':value});assert listed()['vacation_days']==expected
        for value in ['-1','1.25','367','nan','15\x00bad','9'*50]:
            try:call('/api/v1/admin/employees/save',{**fields,'vacation_days':value})
            except urllib.error.HTTPError as e:assert e.code==400
            else:raise AssertionError('invalid allowance accepted: '+repr(value))
        assert listed()['vacation_days']==15
        token='e'*64
        crypt=ctypes.CDLL(ctypes.util.find_library('crypt')).crypt;crypt.argtypes=[ctypes.c_char_p,ctypes.c_char_p];crypt.restype=ctypes.c_char_p
        hashed=crypt(token.encode(),b'$6$rounds=5000$payreview2026$').decode()
        with sqlite3.connect(dbpath) as db:
            db.execute("UPDATE employees SET created_at='2020-01-01T00:00:00Z' WHERE id=?",(employee,))
            db.execute("INSERT INTO schedule_profiles(employee_id,pay_type,schedule_mode,weekday_mask) VALUES(?,'salary','recurring_weekly',31)",(employee,))
            db.execute("INSERT INTO statutory_holidays VALUES('2026-10-12','Thanksgiving')")
            for kind,start,end,status,cancelled in [('vacation','2026-10-01','2026-10-26','approved',None),('vacation','2026-10-05','2026-10-08','approved',None),('vacation','2026-10-27','2026-10-28','pending',None),('vacation','2026-10-29','2026-10-30','denied',None),('vacation','2026-11-04','2026-11-05','approved','2026-10-01'),('personal','2026-11-06','2026-11-06','approved',None)]:
                db.execute('INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status,cancelled_at) VALUES(?,?,?,?,?,?)',(employee,kind,start,end,status,cancelled))
            db.execute("INSERT INTO timeoff_links VALUES(?,?,CAST(strftime('%s','now') AS INTEGER)+3600,0)",(employee,hashed))
            pid=db.execute("INSERT INTO pay_periods(starts,ends,status) VALUES('2026-10-01','2026-10-15','closed') RETURNING id").fetchone()[0]
        private='/time-off/'+token
        assert call(private+'/vacation?year=2026',manager=False)=={'year':2026,'used':17,'allowed':15}
        assert call(private+'/vacation?year=2027',manager=False)=={'year':2027,'used':0,'allowed':15}
        for bad in ['1999','2026xyz']:
            try:call(private+'/vacation?year='+bad,manager=False)
            except urllib.error.HTTPError as e:assert e.code==400
            else:raise AssertionError('bad year accepted')
        try:call('/time-off/'+'f'*64+'/vacation?year=2026',manager=False)
        except urllib.error.HTTPError as e:assert e.code==404
        else:raise AssertionError('unknown token accepted')
        # No budget enforcement: even zero allowance accepts a further valid request.
        call('/api/v1/admin/employees/save',{**fields,'vacation_days':'0'})
        result=call(private+'/request',dict(kind='vacation',starts='2026-11-02',ends='2026-11-03',requested_days='2',return_date='2026-11-04',note='Additional vacation requested'),manager=False)
        assert 'submitted' in result['status'].lower()
        assert call(private+'/vacation?year=2026',manager=False)['used']==17  # pending is excluded
        call('/api/v1/admin/employees/save',fields)
        saved=call('/api/v1/admin/periods/exports',{'period_id':pid});assert saved['revision']==1
        first=call(f'/api/v1/admin/periods/export.xlsx?period_id={pid}&revision=1',raw=True)
        def summary(data):
            with zipfile.ZipFile(io.BytesIO(data)) as z:
                assert len(ET.fromstring(z.read('xl/workbook.xml')).find('{*}sheets'))==2
                tree=ET.fromstring(z.read('xl/worksheets/sheet1.xml'))
                return [[c.findtext('{*}is/{*}t','') if c.get('t')=='inlineStr' else c.findtext('{*}v','') for c in row] for row in tree.find('{*}sheetData')]
        rows=summary(first);sarah=next(row for row in rows if row[0]=='Sarah Chen')
        assert sarah[-2]=='2026: 17 of 15 days used' and sarah[-1]==str(employee),sarah
        assert sarah[1]=='0'  # reference carries no hours
        annual=call('/api/v1/admin/time-off/annual.csv?year=2026',raw=True).decode()
        assert '17 of 15 days used' in annual
        call('/api/v1/admin/employees/save',{**fields,'vacation_days':'20'})
        assert call(f'/api/v1/admin/periods/export.xlsx?period_id={pid}&revision=1',raw=True)==first
        latest=call('/api/v1/admin/periods/exports',{'period_id':pid});assert latest['revision']==2
        second=call(f'/api/v1/admin/periods/export.xlsx?period_id={pid}&revision=2',raw=True)
        assert next(row for row in summary(second) if row[0]=='Sarah Chen')[-2]=='2026: 17 of 20 days used'
        with sqlite3.connect(dbpath) as db:
            db.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status) VALUES(?,'vacation','2026-12-31','2027-01-04','approved')",(employee,))
            db.execute("INSERT INTO statutory_holidays VALUES('2027-01-01','New Year')")
        assert call(private+'/vacation?year=2026',manager=False)['used']==18
        assert call(private+'/vacation?year=2027',manager=False)['used']==1
        assert call(f'/api/v1/admin/periods/export.xlsx?period_id={pid}&revision=1',raw=True)==first
    finally:proc.terminate();proc.communicate(timeout=5)
print('PASS: create/edit optional, zero and half-day allowances; deduplicated annual calendar usage; holidays/year clipping; request above zero allowed; no pending/denied/cancelled usage; private access; CSV and two-tab frozen revisions')
