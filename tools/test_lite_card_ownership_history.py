"""Authenticated HTTP acceptance: delayed uploads honor card and employee history."""
import json, os, socket, sqlite3, subprocess, tempfile, time, urllib.request
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='lite-owner-') as tmp:
    path=Path(tmp)/'server.db'
    with socket.socket() as sock:
        sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
    key='development-key-123456789'
    proc=subprocess.Popen([str(root/'timeclock-server'),str(path),str(port)],env={**os.environ,'DEVICE_KEY':key},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        for _ in range(80):
            try:
                with urllib.request.urlopen(f'http://127.0.0.1:{port}/health',timeout=.25):break
            except Exception:time.sleep(.05)
        else:raise AssertionError('server did not start')
        with sqlite3.connect(path) as db:
            db.execute("INSERT INTO registered_clocks(clock_id,device_key,active) VALUES('CLOCK-001',?,1)",(key,))
            for i in (1,2):
                db.execute("INSERT INTO employees(id,name,created_at) VALUES(?,?, '2026-10-01T00:00:00.000Z')",(i,f'Worker {i}'))
            db.execute("INSERT INTO cards(uid,employee_id) VALUES('AABBCCDD',1)")
            db.execute("UPDATE card_ownership_history SET effective_at='2026-10-01T00:00:00.000Z'")
            db.execute("UPDATE cards SET active=0 WHERE uid='AABBCCDD'")
            db.execute("UPDATE card_ownership_history SET effective_at='2026-10-10T00:00:00.000Z' WHERE id=(SELECT max(id) FROM card_ownership_history)")
            db.execute("UPDATE cards SET employee_id=2,active=1 WHERE uid='AABBCCDD'")
            db.execute("UPDATE card_ownership_history SET effective_at='2026-10-15T00:00:00.000Z' WHERE id=(SELECT max(id) FROM card_ownership_history)")
            db.execute("UPDATE employees SET active=0 WHERE id=2")
            db.execute("UPDATE employee_active_history SET effective_at='2026-10-20T00:00:00.000Z' WHERE id=(SELECT max(id) FROM employee_active_history)")
            db.commit()
        def upload(day,marker):
            ev=marker*32
            data=json.dumps({'event_id':ev,'clock_id':'CLOCK-001','card_uid':'AABBCCDD','swiped_at':f'2026-10-{day}T08:00:00Z'}).encode()
            req=urllib.request.Request(f'http://127.0.0.1:{port}/api/v1/swipes',data=data,headers={'Content-Type':'application/json','X-Clock-ID':'CLOCK-001','X-Device-Key':key})
            with urllib.request.urlopen(req,timeout=5) as resp:result=json.load(resp)
            with sqlite3.connect(path) as db:
                owner=db.execute('SELECT employee_id FROM swipes WHERE event_id=?',(ev,)).fetchone()[0]
                classified=db.execute('SELECT kind FROM attendance_events WHERE event_id=?',(ev,)).fetchone()
            return result,owner,classified
        prior=upload('05','a');revoked=upload('12','b');reassigned=upload('17','c');inactive=upload('22','d')
        assert prior[1]==1 and prior[2]==('IN',),prior
        assert revoked[1] is None and revoked[2] is None,revoked
        assert reassigned[1]==2 and reassigned[2]==('IN',),reassigned
        assert inactive[1] is None and inactive[2] is None,inactive
        assert upload('05','a')==prior,'retry must be stable'
        print('PASS: historical owner, revoked interval, reassignment, inactive employee, and duplicate retry')
    finally:
        proc.terminate();proc.communicate(timeout=5)
