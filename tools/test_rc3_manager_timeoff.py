"""Real HTTP: manager-recorded leave, shared calendar, security, audit and frozen exports."""
import ctypes, ctypes.util, io, json, os, socket, sqlite3, subprocess, tempfile, time
import urllib.request, urllib.parse, urllib.error, zipfile
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
            db.execute("INSERT INTO employees(id,name,email,created_at,vacation_days) VALUES(1,'Manager','manager@example.test','2020-01-01',NULL),(2,'Sarah Chen','sarah@example.test','2026-10-20',0),(3,'Former Employee','','2026-10-20',NULL)")
            db.execute('UPDATE employees SET active=0 WHERE id=3')
            db.execute("INSERT INTO employee_roles VALUES(1,'manager')")
            db.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
            db.execute("INSERT INTO manager_cards VALUES('A1B2C3D4',1,1)")
            db.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+3600)",('d'*64,))
            db.execute('UPDATE review_send_settings SET enabled=0')
            db.execute("INSERT INTO schedule_profiles(employee_id,pay_type,schedule_mode,weekday_mask) VALUES(2,'salary','recurring_weekly',31)")
            db.execute("INSERT INTO statutory_holidays VALUES('2026-01-01','New Year'),('2027-01-01','New Year')")
            pid=db.execute("INSERT INTO pay_periods(starts,ends,status) VALUES('2026-01-01','2026-01-15','closed') RETURNING id").fetchone()[0]
            before_schema=db.execute("SELECT type,name,sql FROM sqlite_master WHERE name NOT LIKE 'sqlite_%' ORDER BY type,name").fetchall()
        def call(path,form=None,manager=True,raw=False,expected=200):
            headers={'Cookie':'tc_session='+'d'*64} if manager else {}
            if form is not None:headers['Content-Type']='application/x-www-form-urlencoded'
            data=urllib.parse.urlencode(form).encode() if form is not None else None
            try:
                with urllib.request.urlopen(urllib.request.Request(base+path,data=data,headers=headers),timeout=10) as r:
                    content=r.read();assert r.status==expected,(r.status,expected,content);return content if raw else json.loads(content)
            except urllib.error.HTTPError as e:
                content=e.read();assert e.code==expected,(e.code,expected,content);return json.loads(content)
        entry=dict(employee_id=2,kind='vacation',starts='2026-01-01',ends='2026-01-06',requested_days='3',return_date='2026-01-07',note='Approved verbally\nBefore clock installation')
        for route in ['/api/v1/admin/time-off/record','/api/v1/admin/time-off/preview']:
            call(route,entry,manager=False,expected=401)
            call(route,{**entry,'employee_id':'999999'},expected=422)
            call(route,{**entry,'employee_id':'2bad'},expected=422)
        preview=call('/api/v1/admin/time-off/preview',entry)
        assert preview['days']==3 and preview['suggested_return']=='2026-01-07',preview
        first=call('/api/v1/admin/periods/exports',{'period_id':pid});assert first['revision']==1
        saved=call(f'/api/v1/admin/periods/export.xlsx?period_id={pid}&revision=1',raw=True)
        with sqlite3.connect(dbpath) as db:emails_before=db.execute('SELECT count(*) FROM email_queue').fetchone()[0]
        assert 'recorded' in call('/api/v1/admin/time-off/record',entry)['status'].lower()
        requests=call('/api/v1/admin/time-off')['requests'];leave=next(x for x in requests if x['starts']==entry['starts']);rid=leave['id']
        assert leave['status']=='approved' and leave['manager_recorded'] and leave['decision_note']==entry['note'] and leave['note']==entry['note'],leave
        with sqlite3.connect(dbpath) as db:
            row=db.execute('SELECT manager_id,requested_at,decided_at,acknowledged_at FROM timeoff_requests WHERE id=?',(rid,)).fetchone()
            assert row[0]==1 and row[1] and row[2] and row[3] is None,row
            assert db.execute('SELECT actor_id,action FROM timeoff_audit WHERE request_id=?',(rid,)).fetchall()==[(1,'recorded_approved')]
            assert db.execute('SELECT count(*) FROM email_queue').fetchone()[0]==emails_before
            assert db.execute("SELECT type,name,sql FROM sqlite_master WHERE name NOT LIKE 'sqlite_%' ORDER BY type,name").fetchall()==before_schema
        annual=call('/api/v1/admin/time-off/annual.csv?year=2026',raw=True).decode()
        assert '3 of 0 days used' in annual and 'Before clock installation' in annual,annual
        p=next(x for x in call('/api/v1/admin/periods')['periods'] if x['id']==pid)
        assert p['new_export_required'] and p['export_stale'],p
        assert call(f'/api/v1/admin/periods/export.xlsx?period_id={pid}&revision=1',raw=True)==saved
        second=call('/api/v1/admin/periods/exports',{'period_id':pid});assert second['revision']==2
        latest=call(f'/api/v1/admin/periods/export.xlsx?period_id={pid}&revision=2',raw=True)
        with zipfile.ZipFile(io.BytesIO(latest)) as z:
            assert len([n for n in z.namelist() if n.startswith('xl/worksheets/sheet')])==2
            assert '3 of 0 days used' in z.read('xl/worksheets/sheet1.xml').decode()
            assert 'Approved vacation' in z.read('xl/worksheets/sheet2.xml').decode()
        # Retries and malformed entries do not create duplicates or audits.
        call('/api/v1/admin/time-off/record',entry,expected=409)
        for fields in [dict(kind='sick'),dict(starts='2026-02-30'),dict(ends='2025-12-31'),dict(ends='2028-01-01'),dict(requested_days='2'),dict(requested_days='999999999999'),dict(return_date='2026-01-06'),dict(note='x'*501),dict(note='bad\x00note')]:
            call('/api/v1/admin/time-off/record',{**entry,**fields},expected=422)
        # Same calendar rules reject dates without working days.
        call('/api/v1/admin/time-off/record',{**entry,'starts':'2026-02-07','ends':'2026-02-08','requested_days':'0','return_date':'2026-02-09'},expected=422)
        future={**entry,'starts':'2026-12-31','ends':'2027-01-04','requested_days':'2','return_date':'2027-01-05','note':'Approved by email'}
        call('/api/v1/admin/time-off/record',future)
        personal={**entry,'kind':'personal','starts':'2026-02-02','ends':'2026-02-02','requested_days':'1','return_date':'2026-02-03'}
        call('/api/v1/admin/time-off/record',personal,expected=422)
        call('/api/v1/admin/time-off/record',{**personal,'employee_id':'3','kind':'vacation'})
        assert '4 of 0 days used' in call('/api/v1/admin/time-off/annual.csv?year=2026',raw=True).decode()
        assert '1 of 0 days used' in call('/api/v1/admin/time-off/annual.csv?year=2027',raw=True).decode()
        # Cancellation uses the existing audited workflow; original creation source survives.
        call('/api/v1/admin/time-off/decide',dict(id=rid,action='cancelled',note='Entered in error'))
        after=next(x for x in call('/api/v1/admin/time-off')['requests'] if x['id']==rid)
        assert after['status']=='cancelled' and after['manager_recorded'] and after['note']==entry['note']
        assert '1 of 0 days used' in call('/api/v1/admin/time-off/annual.csv?year=2026',raw=True).decode()
        assert call(f'/api/v1/admin/periods/export.xlsx?period_id={pid}&revision=1',raw=True)==saved
        # Employee access cannot request historical leave or promote their own status.
        token='e'*64
        crypt=ctypes.CDLL(ctypes.util.find_library('crypt')).crypt;crypt.argtypes=[ctypes.c_char_p,ctypes.c_char_p];crypt.restype=ctypes.c_char_p
        hashed=crypt(token.encode(),b'$6$rounds=5000$payreview2026$').decode()
        with sqlite3.connect(dbpath) as db:db.execute("INSERT INTO timeoff_links VALUES(?,?,CAST(strftime('%s','now') AS INTEGER)+3600,0)",(2,hashed))
        private='/time-off/'+token
        history=next(x for x in call(private+'/list',manager=False)['requests'] if x['id']==rid)
        assert history['manager_recorded'] and history['note']==entry['note']
        call(private+'/request',{**entry,'manager_id':1,'status':'approved'},manager=False,expected=422)
        pending={**entry,'starts':'2090-01-02','ends':'2090-01-02','requested_days':'1','return_date':'2090-01-03','status':'approved','manager_id':1}
        day=call(private+'/days?starts=2090-01-02&ends=2090-01-02',manager=False)
        assert day['days']==1
        call(private+'/request',pending,manager=False)
        assert next(x for x in call(private+'/list',manager=False)['requests'] if x['starts']=='2090-01-02')['status']=='pending'
        # Only Vacation can be newly submitted through either entry point.
        for kind in ['personal','unpaid','other']:
            call(private+'/request',{**pending,'kind':kind},manager=False,expected=422)
            call('/api/v1/admin/time-off/record',{**entry,'kind':kind},expected=422)
        # Employee requests still notify the employee and manager; approval notifies once.
        request=next(x for x in call(private+'/list',manager=False)['requests'] if x['starts']=='2090-01-02')
        with sqlite3.connect(dbpath) as db:
            queued=db.execute("SELECT recipient,subject,body FROM email_queue WHERE event_id IN (?,?)",('timeoff_ack:'+str(request['id']),'timeoff_manager:'+str(request['id'])+':manager@example.test')).fetchall()
            assert {r[0] for r in queued}=={'sarah@example.test','manager@example.test'},queued
            assert all('Vacation request' in r[1] for r in queued),queued
        call('/api/v1/admin/time-off/decide',dict(id=request['id'],action='approved',note='Approved'))
        with sqlite3.connect(dbpath) as db:
            decision=db.execute("SELECT recipient,subject,body FROM email_queue WHERE event_id=?",('timeoff_decision:'+str(request['id'])+':approved',)).fetchall()
            assert len(decision)==1 and decision[0][0]=='sarah@example.test' and 'Vacation request approved' in decision[0][1] and '{{' not in decision[0][2],decision
        call('/api/v1/admin/time-off/decide',dict(id=request['id'],action='approved',note='Duplicate'),expected=409)
        # Existing non-vacation history remains intact and never counts as vacation.
        with sqlite3.connect(dbpath) as db:
            legacy=db.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status,note) VALUES(2,'personal','2026-03-02','2026-03-02','approved','Existing personal leave') RETURNING id").fetchone()[0]
        old=next(x for x in call(private+'/list',manager=False)['requests'] if x['id']==legacy)
        assert old['kind']=='personal' and old['note']=='Existing personal leave'
        assert '1 of 0 days used' in call('/api/v1/admin/time-off/annual.csv?year=2026',raw=True).decode()
        # A failed audit write rolls back the leave insertion.
        with sqlite3.connect(dbpath) as db:
            db.execute("CREATE TRIGGER reject_manual_audit BEFORE INSERT ON timeoff_audit WHEN NEW.action='recorded_approved' BEGIN SELECT RAISE(ABORT,'test audit failure'); END")
        failed={**personal,'kind':'vacation','starts':'2026-02-04','ends':'2026-02-04','return_date':'2026-02-05'}
        call('/api/v1/admin/time-off/record',failed,expected=500)
        with sqlite3.connect(dbpath) as db:assert db.execute("SELECT count(*) FROM timeoff_requests WHERE starts='2026-02-04'").fetchone()[0]==0
        # Active-install template upgrade refreshes only untouched defaults.
        proc.terminate();proc.communicate(timeout=5)
        with sqlite3.connect(dbpath) as db:
            db.execute("UPDATE email_templates SET subject='Time-off request received: {{period_start}} to {{period_end}}',enabled=0 WHERE id='timeoff_received_employee'")
            old_body="{{employee_name}} requested {{request_type}} from {{period_start}} to {{period_end}}.\nWorking days: {{days}}\nReturns: {{return_date}}\nNotes: {{note}}\n\nReview under Time off requests."
            db.execute("UPDATE email_templates SET subject='Custom manager subject',body=? WHERE id='timeoff_received_manager'",(old_body,))
            db.execute("UPDATE email_templates SET body='Custom welcome text' WHERE id='welcome_employee'")
        proc=subprocess.Popen([str(root/'timeclock-server'),str(dbpath),str(port)],cwd=root,env={**os.environ,'DEVICE_KEY':'test-device-key-12345','TZ':'America/Edmonton'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        for _ in range(100):
            try:urllib.request.urlopen(base+'/health',timeout=.2).close();break
            except OSError:time.sleep(.04)
        with sqlite3.connect(dbpath) as db:
            subject,enabled=db.execute("SELECT subject,enabled FROM email_templates WHERE id='timeoff_received_employee'").fetchone()
            assert subject.startswith('Vacation request received:') and enabled==0
            subject,body=db.execute("SELECT subject,body FROM email_templates WHERE id='timeoff_received_manager'").fetchone()
            assert subject=='Custom manager subject' and body.endswith('Review under Vacation.')
            assert db.execute("SELECT body FROM email_templates WHERE id='welcome_employee'").fetchone()[0]=='Custom welcome text'
            assert db.execute('SELECT kind,note FROM timeoff_requests WHERE id=?',(legacy,)).fetchone()==('personal','Existing personal leave')
    finally:proc.terminate();proc.communicate(timeout=5)
print('PASS manager historical/future vacation; no new schema; audit and rollback; no entry emails; overlap; schedule/holiday/year counts; zero allowance; private authorization; frozen exports and stale indicators; cancellation; vacation-only enforcement; exactly-once notifications; earlier leave retained; default-template upgrade preserves custom text')
