"""Real HTTP: holiday precedence in vacation previews/history, private reviews and exports."""
import ctypes,ctypes.util,io,json,os,socket,sqlite3,subprocess,tempfile,time,urllib.request,urllib.parse,urllib.error,zipfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    path=Path(tmp)/'clock.db'
    with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
    proc=subprocess.Popen([str(root/'timeclock-server'),str(path),str(port)],cwd=root,env={**os.environ,'DEVICE_KEY':'test-device-key-12345','TZ':'America/Edmonton'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        base=f'http://127.0.0.1:{port}'
        for _ in range(100):
            try:urllib.request.urlopen(base+'/health',timeout=.2).close();break
            except OSError:time.sleep(.04)
        crypt=ctypes.CDLL(ctypes.util.find_library('crypt')).crypt;crypt.argtypes=[ctypes.c_char_p,ctypes.c_char_p];crypt.restype=ctypes.c_char_p
        hashed=lambda token:crypt(token.encode(),b'$6$rounds=5000$payreview2026$').decode()
        with sqlite3.connect(path) as db:
            db.execute("INSERT INTO employees(id,name,email,created_at,vacation_days,attendance_mode) VALUES(1,'Manager','manager@example.test','2020-01-01',NULL,'punch_required'),(2,'Sarah Chen','sarah@example.test','2020-01-01',15,'vacation_only'),(3,'Saturday Worker','','2020-01-01',15,'vacation_only'),(4,'Pending Employee','','2020-01-01',15,'vacation_only')")
            db.execute("INSERT INTO employee_roles VALUES(1,'manager')")
            db.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
            db.execute("INSERT INTO manager_cards VALUES('A1B2C3D4',1,1)")
            db.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+3600)",('d'*64,))
            db.execute('UPDATE review_send_settings SET enabled=0')
            for eid,mask in [(2,31),(3,32),(4,31)]:db.execute("INSERT INTO schedule_profiles(employee_id,pay_type,schedule_mode,weekday_mask) VALUES(?,'salary','recurring_weekly',?)",(eid,mask))
            db.execute("INSERT INTO statutory_holidays VALUES('2026-10-12','Thanksgiving')")
            pid=db.execute("INSERT INTO pay_periods(starts,ends,status) VALUES('2026-10-01','2026-10-15','closed') RETURNING id").fetchone()[0]
            for eid,token in [(2,'e'*64),(3,'f'*64),(4,'b'*64)]:
                db.execute("INSERT INTO timeoff_links VALUES(?,?,CAST(strftime('%s','now') AS INTEGER)+3600,0)",(eid,hashed(token)))
                db.execute("INSERT INTO review_links(employee_id,period_id,token_hash,expires_at,created_by,created_at) VALUES(?,?,?,CAST(strftime('%s','now') AS INTEGER)+3600,1,CAST(strftime('%s','now') AS INTEGER))",(eid,pid,hashed(token)))
        def call(route,form=None,manager=True,expected=200,raw=False):
            data=urllib.parse.urlencode(form).encode() if form is not None else None
            headers={'Cookie':'tc_session='+'d'*64} if manager else {}
            if data is not None:headers['Content-Type']='application/x-www-form-urlencoded'
            try:
                with urllib.request.urlopen(urllib.request.Request(base+route,data=data,headers=headers),timeout=10) as r:
                    data=r.read();assert r.status==expected;return data if raw else json.loads(data)
            except urllib.error.HTTPError as e:
                data=e.read();assert e.code==expected,(route,e.code,data);return json.loads(data)
        entry=dict(employee_id=2,kind='vacation',starts='2026-10-12',ends='2026-10-16',requested_days='4',return_date='2026-10-19',note='Approved verbally')
        preview=call('/api/v1/admin/time-off/preview',entry)
        assert preview['days']==4 and preview['stat_days']==1 and preview['holidays']==[dict(date='2026-10-12',name='Thanksgiving')],preview
        assert call('/time-off/'+'e'*64+'/days?starts=2026-10-12&ends=2026-10-16',manager=False)==preview
        only={**entry,'ends':'2026-10-12','requested_days':'0','return_date':'2026-10-13'}
        for route in ['/api/v1/admin/time-off/preview','/api/v1/admin/time-off/record']:
            error=call(route,only,expected=422)['error'];assert 'no vacation working days' in error and '2026-10-12 is Thanksgiving' in error,error
        error=call('/time-off/'+'e'*64+'/days?starts=2026-10-12&ends=2026-10-12',manager=False,expected=422)['error'];assert 'Thanksgiving' in error
        error=call('/time-off/'+'e'*64+'/days?starts=2026-10-17&ends=2026-10-18',manager=False,expected=422)['error'];assert 'no vacation working days' in error and 'Thanksgiving' not in error
        call('/api/v1/admin/time-off/record',entry)
        private='/time-off/'+'e'*64
        record=call(private+'/list',manager=False)['requests'][0];rid=record['id']
        assert record['days']==4 and record['requested_days']==4 and record['stat_days']==1
        assert call(private+'/vacation?year=2026',manager=False)['used']==4
        # An existing pending request also exposes the current breakdown before approval.
        with sqlite3.connect(path) as db:pending=db.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status,requested_days,return_date) VALUES(4,'vacation','2026-10-12','2026-10-16','pending',5,'2026-10-19') RETURNING id").fetchone()[0]
        row=next(x for x in call('/api/v1/admin/time-off')['requests'] if x['id']==pending)
        assert row['days']==4 and row['requested_days']==5 and row['stat_days']==1
        assert call('/time-off/'+'b'*64+'/vacation?year=2026',manager=False)['used']==0
        call('/api/v1/admin/time-off/decide',dict(id=pending,action='approved',note='Four vacation days plus Thanksgiving'))
        # A stat on a non-working Monday remains visible; Saturday vacation is displayed accurately.
        saturday={**entry,'employee_id':3,'starts':'2026-10-10','ends':'2026-10-12','requested_days':'1','return_date':'2026-10-17'}
        check=call('/api/v1/admin/time-off/preview',saturday);assert check['days']==1 and check['stat_days']==1,check
        call('/api/v1/admin/time-off/record',saturday)
        for token,expected_vacation in [('e'*64,3),('f'*64,1)]:
            context=call('/review/'+token+'/detail',manager=False)['context']
            holiday=[x for x in context if x['day']=='2026-10-12'];assert len(holiday)==1 and holiday[0]['type']=='holiday' and holiday[0]['label']=='Stat holiday: Thanksgiving',holiday
            assert len([x for x in context if x['type']=='timeoff:vacation'])==expected_vacation,context
        saved=call('/api/v1/admin/periods/exports',dict(period_id=pid));assert saved['revision']==1
        workbook=call(f'/api/v1/admin/periods/export.xlsx?period_id={pid}&revision=1',raw=True)
        with zipfile.ZipFile(io.BytesIO(workbook)) as z:
            assert 'Stat holiday (not vacation): Thanksgiving' in z.read('xl/worksheets/sheet2.xml').decode()
            assert '4 of 15 days used' in z.read('xl/worksheets/sheet1.xml').decode()
        # Manager calendar edits recalculate current lists/usage without altering original requests or exports.
        call('/api/v1/admin/holidays',dict(skip=1,calendar=''))
        record=call(private+'/list',manager=False)['requests'][0];assert record['days']==5 and record['requested_days']==4 and record['stat_days']==0,record
        assert call(private+'/vacation?year=2026',manager=False)['used']==5
        period=next(x for x in call('/api/v1/admin/periods')['periods'] if x['id']==pid);assert period['new_export_required'] and period['export_stale'],period
        assert call(f'/api/v1/admin/periods/export.xlsx?period_id={pid}&revision=1',raw=True)==workbook
        call('/api/v1/admin/holidays',dict(skip=1,calendar='2026-10-13,Stat "name" <test>'))
        record=call(private+'/list',manager=False)['requests'][0];assert record['days']==4 and record['stat_days']==1 and record['holidays'][0]['name']=='Stat "name" <test>'
        assert call(private+'/vacation?year=2026',manager=False)['used']==4
        with sqlite3.connect(path) as db:assert db.execute('SELECT requested_days FROM timeoff_requests WHERE id=?',(rid,)).fetchone()[0]==4
        assert call(f'/api/v1/admin/periods/export.xlsx?period_id={pid}&revision=1',raw=True)==workbook
        # A later calendar edit can eliminate every vacation day; history must remain visible.
        call('/api/v1/admin/holidays',dict(skip=1,calendar='\n'.join('2026-10-'+str(d)+',Stat '+str(d) for d in range(12,17))))
        record=call(private+'/list',manager=False)['requests'][0];assert record['days']==0 and record['stat_days']==5 and record['requested_days']==4 and record['status']=='approved'
        assert call(private+'/vacation?year=2026',manager=False)['used']==0
        assert call(f'/api/v1/admin/periods/export.xlsx?period_id={pid}&revision=1',raw=True)==workbook
    finally:proc.terminate();proc.communicate(timeout=5)
print('PASS vacation/stat breakdown; named stat-only errors; pending and approved history; current calendar changes; original day count retained; weekly Saturday holiday visibility; no duplicate vacation label; totals and immutable exports')
