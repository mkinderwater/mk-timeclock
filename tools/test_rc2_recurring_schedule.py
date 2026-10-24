#!/usr/bin/env python3
"""Live swipe acceptance: recurring weekly, dated shift, and unscheduled employee."""
import os, sqlite3, socket, subprocess, tempfile, time, json, urllib.request, datetime, zoneinfo
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='rc2-sophie-') as td:
    path=Path(td)/'clock.db'
    with socket.socket() as s:
        s.bind(('127.0.0.1',0)); port=s.getsockname()[1]
    key='rc2-schedule-test-device-key'
    proc=subprocess.Popen([str(ROOT/'timeclock-server'),str(path),str(port)],env={**os.environ,'TZ':'America/Edmonton','DEVICE_KEY':key},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        base=f'http://127.0.0.1:{port}'
        for _ in range(100):
            try: urllib.request.urlopen(base+'/health',timeout=.2).close();break
            except Exception:time.sleep(.05)
        else: raise AssertionError('server did not start')
        with sqlite3.connect(path) as db:
            for i,uid,mode in [(10,'AA001100','recurring_weekly'),(11,'AA001101','dated_shifts'),(12,'AA001102','none')]:
                db.execute("INSERT INTO employees(id,name,email,created_at) VALUES(?,?,?,'2026-09-01T00:00:00Z')",(i,'Sophie Snow' if i==10 else 'Worker '+str(i),f'e{i}@example.test'))
                db.execute('INSERT INTO cards(uid,employee_id) VALUES(?,?)',(uid,i))
                db.execute("UPDATE card_ownership_history SET effective_at='2026-09-01T00:00:00Z' WHERE uid=?",(uid,))
                # Tuesday (2), Thursday (8); weekly fields on dated/none are deliberate leftovers.
                db.execute("INSERT INTO schedule_profiles(employee_id,pay_type,schedule_mode,start_time,end_time,weekday_mask) VALUES(?,'hourly',?,'08:30','16:30',10)",(i,mode))
            db.execute("INSERT INTO schedule_shifts(employee_id,work_date,start_time,end_time) VALUES(11,'2026-10-20','09:00','17:00')")
            db.execute("INSERT INTO registered_clocks(clock_id,device_key,active) VALUES('NS-01',?,1)",(key,))
        def swipe(uid,day,hhmm,symbol):
            local=datetime.datetime.fromisoformat(day+'T'+hhmm).replace(tzinfo=zoneinfo.ZoneInfo('America/Edmonton'))
            at=local.astimezone(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
            payload=json.dumps({'event_id':symbol*32,'clock_id':'NS-01','card_uid':uid,'swiped_at':at}).encode()
            req=urllib.request.Request(base+'/api/v1/swipes',data=payload,headers={'Content-Type':'application/json','X-Clock-ID':'NS-01','X-Device-Key':key})
            with urllib.request.urlopen(req,timeout=5) as r:return json.load(r)
        assert swipe('AA001100','2026-10-20','08:35:00','a')['classification']=='IN'
        assert swipe('AA001100','2026-10-22','08:36:00','b')['classification']=='IN_LATE'
        assert swipe('AA001101','2026-10-20','09:06:00','c')['classification']=='IN_LATE'
        assert swipe('AA001102','2026-10-20','10:00:00','d')['classification']=='IN'
        with sqlite3.connect(path) as db:
            assert db.execute('SELECT late_minutes FROM attendance_events WHERE event_id=?',('b'*32,)).fetchone()[0]==6
            assert db.execute("SELECT schedule_mode FROM schedule_profiles WHERE employee_id=10").fetchone()[0]=='recurring_weekly'
            # Verify reminder selection reflects recurring and dated modes, not legacy pay_type.
            for f in ['timeclock-attendance-reminders.py','timeclock-missing-digest.py']:
                s=(ROOT/f).read_text()
                assert "p.schedule_mode='recurring_weekly'" in s and "p.schedule_mode='dated_shifts'" in s
            rows=db.execute("SELECT employee_id FROM schedule_profiles WHERE schedule_mode='recurring_weekly' AND weekday_mask & 2 != 0").fetchall()
            assert (10,) in rows and (11,) not in rows
        print('PASS: Sophie recurring Tuesday/Thursday grace; dated shift; no-fixed ignores stale weekly defaults')
    finally:
        proc.terminate();proc.wait(timeout=5)
