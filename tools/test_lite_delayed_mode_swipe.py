"""Real HTTP test: offline punch is attributed using mode at swipe time."""
import datetime, json, os, socket, sqlite3, subprocess, tempfile, time, urllib.request
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='lite-history-') as tmp:
    path=Path(tmp)/'server.db'
    with socket.socket() as sock:
        sock.bind(('127.0.0.1',0)); port=sock.getsockname()[1]
    key='development-key-123456789'
    proc=subprocess.Popen([str(root/'timeclock-server'),str(path),str(port)],env={**os.environ,'DEVICE_KEY':key},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        for _ in range(80):
            try:
                with urllib.request.urlopen(f'http://127.0.0.1:{port}/health',timeout=.25):break
            except Exception:time.sleep(.05)
        else:raise AssertionError('server failed to start')
        with sqlite3.connect(path) as db:
            db.execute("INSERT INTO registered_clocks(clock_id,device_key,active) VALUES('CLOCK-001',?,1)",(key,))
            db.execute("INSERT INTO employees(id,name,attendance_mode,created_at) VALUES(1,'Delayed Worker','punch_required','2026-10-01T00:00:00.000Z')")
            db.execute("INSERT INTO cards(uid,employee_id) VALUES('AABBCCDD',1)")
            db.execute("UPDATE card_ownership_history SET effective_at='2026-10-01T00:00:00.000Z' WHERE uid='AABBCCDD'")
            # Use explicit historical timestamps so the test is independent of today's date.
            old=(datetime.datetime.now(datetime.timezone.utc)-datetime.timedelta(days=2)).replace(microsecond=0)
            transition=old+datetime.timedelta(days=1)
            new=old+datetime.timedelta(days=1,hours=12)
            db.execute("UPDATE card_ownership_history SET effective_at=? WHERE uid='AABBCCDD'",((old-datetime.timedelta(days=1)).isoformat().replace('+00:00','Z'),))
            db.execute("UPDATE employee_active_history SET effective_at=? WHERE employee_id=1",((old-datetime.timedelta(days=1)).isoformat().replace('+00:00','Z'),))
            # In a fresh database, transition captures both the initial and new mode.
            db.execute("UPDATE employees SET attendance_mode='vacation_only' WHERE id=1")
            db.execute("UPDATE employee_attendance_modes SET effective_at=? WHERE employee_id=1 AND mode='vacation_only'",(transition.isoformat().replace('+00:00','Z'),))
            db.commit()
            hist=db.execute('SELECT mode FROM employee_attendance_modes WHERE employee_id=1 ORDER BY effective_at').fetchall()
            assert hist[0][0]=='punch_required' and hist[-1][0]=='vacation_only',hist
        def upload(at,ev):
            data=json.dumps({'event_id':ev*32,'clock_id':'CLOCK-001','card_uid':'AABBCCDD','swiped_at':at}).encode()
            req=urllib.request.Request(f'http://127.0.0.1:{port}/api/v1/swipes',data=data,headers={'Content-Type':'application/json','X-Clock-ID':'CLOCK-001','X-Device-Key':key})
            with urllib.request.urlopen(req,timeout=5) as resp:return json.load(resp)
        older=upload(old.isoformat().replace('+00:00','Z'),'a')
        assert older['status']=='accepted' and older['classification']=='IN',older
        assert upload(old.isoformat().replace('+00:00','Z'),'a')==older,'retry must be idempotent'
        newer=upload(new.isoformat().replace('+00:00','Z'),'b')
        assert newer['status']=='accepted',newer
        with sqlite3.connect(path) as db:
            before=db.execute("SELECT employee_id FROM swipes WHERE event_id=?",('a'*32,)).fetchone()
            event=db.execute("SELECT kind FROM attendance_events WHERE event_id=?",('a'*32,)).fetchone()
            after=db.execute("SELECT employee_id FROM swipes WHERE event_id=?",('b'*32,)).fetchone()
            assert before==(1,) and event==('IN',),(before,event)
            assert after==(None,),after
            assert db.execute("SELECT count(*) FROM swipes WHERE event_id=?",('a'*32,)).fetchone()[0]==1
        print('PASS: delayed swipe linked to original employee, classified IN, idempotent; later vacation-only swipe not attendance')
    finally:
        proc.terminate();proc.communicate(timeout=5)
