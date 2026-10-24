"""Live large history period-list benchmark. No asserted hardware latency."""
import datetime, json, os, socket, sqlite3, subprocess, tempfile, time, urllib.request
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='rc2-period-perf-') as t:
    path=Path(t)/'db.sqlite'
    with socket.socket() as so: so.bind(('127.0.0.1',0)); port=so.getsockname()[1]
    proc=subprocess.Popen([str(ROOT/'timeclock-server'),str(path),str(port)],env={**os.environ,"DEVICE_KEY":"0123456789abcdef0123456789abcdef"},stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    try:
        host=f'http://127.0.0.1:{port}'
        for i in range(100):
            try:urllib.request.urlopen(host+'/health',timeout=.2).close();break
            except OSError:time.sleep(.05)
        else: raise AssertionError('server did not become healthy')
        with sqlite3.connect(path) as db:
            db.execute("INSERT INTO employees(id,name,email) VALUES(1,'Manager','m@example.test')")
            db.execute("INSERT INTO employee_roles(employee_id,role) VALUES(1,'manager')")
            db.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
            db.execute("INSERT INTO manager_cards(uid,manager_id,active) VALUES('A1B2C3D4',1,1)")
            db.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+1800)",('d'*64,))
            start=datetime.date.today()-datetime.timedelta(days=400)
            for k in range(16):db.execute('INSERT INTO employees(id,name) VALUES(?,?)',(k+2,f'Worker {k}'))
            periods=[]
            for p in range(26):
                d1=start+datetime.timedelta(days=14*p);d2=d1+datetime.timedelta(days=13)
                periods.append((d1.isoformat(),d2.isoformat()))
            for d1,d2 in periods:db.execute("INSERT OR IGNORE INTO pay_periods(starts,ends,status,closed_at) VALUES(?,?,'closed',strftime('%Y-%m-%dT%H:%M:%fZ','now'))",(d1,d2))
            swipes=[];events=[]
            for emp in range(2,18):
                for dayno in range(365):
                    day=start+datetime.timedelta(days=dayno)
                    if day.weekday()>=5:continue
                    for kind,hh in [('IN',8),('OUT',16)]:
                        key=f'perf-{emp}-{dayno}-{kind}'
                        swipes.append((key,'CLOCK-001',str(emp),emp,f'{day}T{hh:02d}:00:00Z'))
                        events.append((key,emp,kind))
            db.executemany('INSERT INTO swipes(event_id,clock_id,card_uid,employee_id,swiped_at) VALUES(?,?,?,?,?)',swipes)
            db.executemany('INSERT INTO attendance_events(event_id,employee_id,kind) VALUES(?,?,?)',events)
        req=urllib.request.Request(host+'/api/v1/admin/periods',headers={'Cookie':'tc_session='+'d'*64})
        begin=time.perf_counter()
        with urllib.request.urlopen(req,timeout=35) as r: payload=json.load(r)
        elapsed=time.perf_counter()-begin
        assert payload.get('periods'),payload
        print(f'PASS period list, {len(swipes)} swipes, {len(payload["periods"])} periods, {elapsed:.3f}s')
    finally:
        proc.terminate()
        try:proc.wait(timeout=5)
        except subprocess.TimeoutExpired:proc.kill();proc.wait()
