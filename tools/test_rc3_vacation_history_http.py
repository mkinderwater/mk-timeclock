"""Real HTTP: manager queue/history buckets, date sorting, employee/year filters and paging."""
import datetime,json,os,socket,sqlite3,subprocess,tempfile,time,urllib.request,urllib.parse,urllib.error
from pathlib import Path
from zoneinfo import ZoneInfo
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
        today=datetime.datetime.now(ZoneInfo('America/Edmonton')).date()
        with sqlite3.connect(path) as db:
            db.execute("INSERT INTO employees(id,name,email,vacation_days) VALUES(1,'Manager','manager@example.test',NULL),(2,'Elena','elena@example.test',15),(3,'Sarah','sarah@example.test',15)")
            db.execute("INSERT INTO employee_roles VALUES(1,'manager')")
            db.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
            db.execute("INSERT INTO manager_cards VALUES('A1B2C3D4',1,1)")
            db.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+3600)",('d'*64,))
            db.execute('UPDATE review_send_settings SET enabled=0')
            for i in reversed(range(360)):
                day=(today-datetime.timedelta(days=i+2)).isoformat()
                db.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status,requested_days,note) VALUES(?,'vacation',?,?,?,1,?)",(2+i%2,day,day,'denied' if i%11==0 else 'approved','Past '+str(i)))
            for start,end,status,cancel in [(today+datetime.timedelta(days=10),today+datetime.timedelta(days=12),'approved',None),(today+datetime.timedelta(days=2),today+datetime.timedelta(days=3),'denied',None),(today-datetime.timedelta(days=1),today+datetime.timedelta(days=1),'approved',None),(today+datetime.timedelta(days=20),today+datetime.timedelta(days=20),'approved','2026-01-01'),(today-datetime.timedelta(days=30),today-datetime.timedelta(days=30),'pending',None),(today+datetime.timedelta(days=90),today+datetime.timedelta(days=90),'pending',None)]:
                db.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status,cancelled_at,requested_days) VALUES(2,'vacation',?,?,?,?,1)",(start.isoformat(),end.isoformat(),status,cancel))
            crossing=db.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status,requested_days) VALUES(3,'vacation',?,?,'approved',2) RETURNING id",(f'{today.year-1}-12-31',f'{today.year}-01-02')).fetchone()[0]
            schema=db.execute("SELECT type,name,sql FROM sqlite_master WHERE name NOT LIKE 'sqlite_%' ORDER BY type,name").fetchall()
        def call(route,form=None,expected=200,manager=True):
            headers={'Cookie':'tc_session='+'d'*64} if manager else {}
            data=urllib.parse.urlencode(form).encode() if form is not None else None
            if data is not None:headers['Content-Type']='application/x-www-form-urlencoded'
            try:
                with urllib.request.urlopen(urllib.request.Request(base+route,data=data,headers=headers),timeout=10) as response:
                    assert response.status==expected;return json.load(response)
            except urllib.error.HTTPError as e:
                body=e.read();assert e.code==expected,(e.code,expected,body);return json.loads(body)
        endpoint='/api/v1/admin/time-off'
        call(endpoint+'?view=past',expected=401,manager=False)
        for query in ['view=garbage','employee_id=2bad','employee_id=0','year=20','year=2026oops','offset=-1','offset=2.5','offset=999999999999999999999999999','view='+'x'*50]:call(endpoint+'?'+query,expected=400)
        waiting=call(endpoint+'?view=waiting');assert waiting['total']==2 and all(x['status']=='pending' for x in waiting['requests']);assert [x['starts'] for x in waiting['requests']]==sorted(x['starts'] for x in waiting['requests'])
        assert [x['dates_passed'] for x in waiting['requests']]==[True,False]
        with sqlite3.connect(path) as db:
            current=db.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status) VALUES(3,'vacation',?,?,'pending') RETURNING id",(today.isoformat(),today.isoformat())).fetchone()[0]
        assert next(x for x in call(endpoint+'?view=waiting')['requests'] if x['id']==current)['dates_passed'] is False
        upcoming=call(endpoint+'?view=upcoming');assert all(x['ends']>=today.isoformat() and x['status']!='pending' for x in upcoming['requests']);assert [x['starts'] for x in upcoming['requests']]==sorted(x['starts'] for x in upcoming['requests']);assert any(x['starts']<today.isoformat() for x in upcoming['requests']);assert any(x['status']=='cancelled' for x in upcoming['requests'])
        first=call(endpoint+'?view=past');assert first['total']>300 and len(first['requests'])==50 and first['has_more']
        records=[]
        for offset in range(0,first['total'],50):
            page=call(endpoint+'?view=past&offset='+str(offset));records.extend(page['requests']);assert page['offset']==offset
        with sqlite3.connect(path) as db:
            expected=[x[0] for x in db.execute("SELECT t.id FROM timeoff_requests t JOIN employees e ON e.id=t.employee_id WHERE (t.status<>'pending' OR t.cancelled_at IS NOT NULL) AND t.ends<? ORDER BY t.starts DESC,e.name COLLATE NOCASE,e.id,t.id DESC",(today.isoformat(),))]
        assert [x['id'] for x in records]==expected and len({x['id'] for x in records})==len(expected)
        assert not page['has_more']
        beyond=call(endpoint+'?view=past&offset=999999');assert beyond['requests']==[] and not beyond['has_more']
        filtered=call(endpoint+f'?view=past&employee_id=3&year={today.year-1}')
        assert all(x['employee_id']==3 and x['starts']<=f'{today.year-1}-12-31' and x['ends']>=f'{today.year-1}-01-01' for x in filtered['requests'])
        for year in [today.year-1,today.year]:
            route=endpoint+f'?view=past&employee_id=3&year={year}'
            result=call(route);ids=[x['id'] for x in result['requests']]
            for offset in range(50,result['total'],50):ids.extend(x['id'] for x in call(route+'&offset='+str(offset))['requests'])
            assert crossing in ids,(year,ids)
        assert str(today.year) in first['years'] and str(today.year-1) in first['years']
        # Optional return date uses the shared suggestion; blank and omitted inputs both work.
        for delta,blank in [(200,False),(220,True)]:
            day=today+datetime.timedelta(days=delta)
            while day.weekday()>4:day+=datetime.timedelta(days=1)
            form=dict(employee_id=2,kind='vacation',starts=day.isoformat(),ends=day.isoformat(),requested_days=1,note='Approved verbally')
            if blank:form['return_date']=''
            preview=call(endpoint+'/preview',form)
            call(endpoint+'/record',form)
            with sqlite3.connect(path) as db:assert db.execute('SELECT return_date FROM timeoff_requests WHERE employee_id=2 AND starts=?',(day.isoformat(),)).fetchone()[0]==preview['suggested_return']
        with sqlite3.connect(path) as db:
            assert db.execute('SELECT count(*) FROM email_queue').fetchone()[0]==0
            assert db.execute("SELECT type,name,sql FROM sqlite_master WHERE name NOT LIKE 'sqlite_%' ORDER BY type,name").fetchall()==schema
    finally:proc.terminate();proc.communicate(timeout=5)
print('PASS waiting first; ongoing/upcoming/past buckets; deterministic vacation-date ordering; employee/year filters including year-crossing leave; all 300+ records paged; optional suggested return; auth/input validation; no schema change or entry email')
