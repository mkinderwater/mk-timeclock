"""Live RC2 regression: a shift starting before a pay boundary ends afterward."""
import ctypes, ctypes.util, datetime, json, os, socket, sqlite3, subprocess, tempfile, time, urllib.request, urllib.error, urllib.parse
from pathlib import Path
from zoneinfo import ZoneInfo
CLOCK_TZ=os.environ.get("TEST_CLOCK_TZ","America/Edmonton")
# Fixture hours are local wall times; persisted punches are UTC.
def stamp(day,clock):
    return datetime.datetime.fromisoformat(day+"T"+clock).replace(tzinfo=ZoneInfo(CLOCK_TZ)).astimezone(datetime.timezone.utc).isoformat().replace("+00:00","Z")
ROOT=Path(__file__).resolve().parents[1]
crypt=ctypes.CDLL(ctypes.util.find_library('crypt'))
crypt.crypt.argtypes=[ctypes.c_char_p,ctypes.c_char_p]; crypt.crypt.restype=ctypes.c_char_p
with tempfile.TemporaryDirectory(prefix='rc2-cross-period-') as tmp:
    dbpath=Path(tmp)/'clock.sqlite'
    with socket.socket() as sock: sock.bind(('127.0.0.1',0)); port=sock.getsockname()[1]
    proc=subprocess.Popen([str(ROOT/'timeclock-server'),str(dbpath),str(port)],env={**os.environ,'DEVICE_KEY':'0123456789abcdef0123456789abcdef','TZ':CLOCK_TZ},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        host=f'http://127.0.0.1:{port}'
        for _ in range(100):
            try: urllib.request.urlopen(host+'/health',timeout=.25).close();break
            except OSError: time.sleep(.05)
        else: raise AssertionError('server did not start')
        end=datetime.datetime.now(ZoneInfo(CLOCK_TZ)).date()-datetime.timedelta(days=30)
        first=end-datetime.timedelta(days=14)
        next_end=end+datetime.timedelta(days=15)
        d1=end.isoformat(); d2=(end+datetime.timedelta(days=1)).isoformat()
        token='a'*64
        with sqlite3.connect(dbpath) as db:
            db.execute("INSERT INTO employees(id,name,email) VALUES(1,'Manager','m@example.test')")
            db.execute("INSERT INTO employees(id,name,email,attendance_mode) VALUES(2,'Night Worker','n@example.test','punch_required')")
            db.execute("INSERT INTO employee_roles(employee_id,role) VALUES(1,'manager')")
            db.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
            db.execute("INSERT INTO manager_cards(uid,manager_id,active) VALUES('A1B2C3D4',1,1)")
            db.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+1800)",('d'*64,))
            p1=db.execute("INSERT INTO pay_periods(starts,ends) VALUES(?,?) RETURNING id",(first.isoformat(),d1)).fetchone()[0]
            p2=db.execute("INSERT INTO pay_periods(starts,ends) VALUES(?,?) RETURNING id",(d2,next_end.isoformat())).fetchone()[0]
            hash_=crypt.crypt(token.encode(),b'$6$rounds=5000$payreview2026$').decode()
            db.execute("INSERT INTO review_links(employee_id,period_id,token_hash,expires_at,created_by,created_at) VALUES(2,?,?,CAST(strftime('%s','now') AS INTEGER)+3600,1,CAST(strftime('%s','now') AS INTEGER))",(p1,hash_))
            for id_,at,kind in [('overnight-in',stamp(d1,'22:00:00'),'IN'),('overnight-out',stamp(d2,'06:00:00'),'OUT')]:
                db.execute("INSERT INTO punch_corrections(employee_id,manual_id,action,effective_at,kind,reason,manager_id) VALUES(2,?,'add',?,?, 'Night shift entry',1)",(id_,at,kind))
            pair=db.execute('SELECT in_at,out_at,seconds,state FROM effective_shift_pairs WHERE employee_id=2').fetchall()
            assert len(pair)==1 and pair[0][2]==28800 and pair[0][3]=='complete',pair
        def request(route,params=None,manager=False):
            data=urllib.parse.urlencode(params).encode() if params is not None else None
            headers={'Cookie':'tc_session='+'d'*64} if manager else {}
            if data: headers['Content-Type']='application/x-www-form-urlencoded'
            req=urllib.request.Request(host+route,data=data,headers=headers)
            try:
                with urllib.request.urlopen(req,timeout=10) as res:return res.status,json.load(res)
            except urllib.error.HTTPError as e:return e.code,json.loads(e.read())
        code,detail=request('/review/'+token+'/detail')
        assert code==200 and detail['seconds']==28800 and len(detail['entries'])==1 and detail['entries'][0]['state']=='complete' and detail['entries'][0]['out_at'] and detail['entries'][0]['out_manual'] and 'Night shift entry' in detail['entries'][0]['out_note'],detail
        code,approved=request('/review/'+token+'/approve',{})
        assert code==200,(code,approved)
        code,people=request('/api/v1/admin/periods/people?period_id='+str(p1),manager=True)
        assert code==200 and len(people['people'])==1 and people['people'][0]['unpaired']==0,people
        code,people2=request('/api/v1/admin/periods/people?period_id='+str(p2),manager=True)
        assert code==200 and len(people2['people'])==0,people2
        code,sign=request('/api/v1/admin/periods',{'action':'sign','starts':first.isoformat(),'ends':d1,'employee_id':'2','reason':'Reviewed overnight shift'},manager=True)
        assert code==200,(code,sign)
        code,close=request('/api/v1/admin/periods',{'action':'close','starts':first.isoformat(),'ends':d1,'employee_id':'0','reason':'Night shift reviewed'},manager=True)
        assert code==200,(code,close)
        code,close2=request('/api/v1/admin/periods',{'action':'close','starts':d2,'ends':next_end.isoformat(),'employee_id':'0','reason':'No shifts started'},manager=True)
        assert code==200,(code,close2)
        # RC3: closed period is a reviewed calendar container, not an attendance lock.
        code,reply=request('/api/v1/admin/punches',{'action':'void','event_id':'overnight-out','kind':'OUT','at':stamp(d2,'06:00:00'),'employee_id':'2','reason':'Verified correction after payroll'},manager=True)
        assert code==200,(code,reply)
        with sqlite3.connect(dbpath) as db:
            changed=db.execute("SELECT seconds,state FROM effective_shift_pairs WHERE employee_id=2").fetchall()
            assert changed!=[(28800,'complete')],changed
            status=db.execute("SELECT status FROM pay_periods WHERE id=?",(p1,)).fetchone()[0]
            assert status=='closed',status
        # Corrections remain audited and can be replaced with a valid OUT.
        code,reply=request('/api/v1/admin/punches',{'action':'add','event_id':'','kind':'OUT','at':stamp(d2,'07:00:00'),'employee_id':'2','reason':'Actual departure verified'},manager=True)
        assert code==200,(code,reply)
        with sqlite3.connect(dbpath) as db:
            after=db.execute("SELECT seconds,state FROM effective_shift_pairs WHERE employee_id=2").fetchall()
            assert after==[(32400,'complete')],after
            audits=db.execute("SELECT count(*) FROM punch_corrections WHERE employee_id=2").fetchone()[0]
            assert audits>=2,audits
        print('PASS: completed overnight shift corrected after closure; closed period preserved; attendance updated')
    finally:
        proc.terminate()
        try:proc.wait(timeout=5)
        except subprocess.TimeoutExpired:proc.kill();proc.wait()
        proc.stderr.close()
