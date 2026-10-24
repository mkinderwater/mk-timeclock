#!/usr/bin/env python3
"""Live HTTP acceptance for hourly dated-shift semantics, based on Lite 1.9.5."""
import os, sqlite3, socket, subprocess, tempfile, time, json, urllib.request, datetime, zoneinfo
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
checks=[]
def check(label,condition):
    assert condition,label
    checks.append(label)
def utc(day,clock):
    dt=datetime.datetime.fromisoformat(day+'T'+clock).replace(tzinfo=zoneinfo.ZoneInfo('America/Edmonton'))
    return dt.astimezone(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
with tempfile.TemporaryDirectory(prefix='northstar-hourly-') as tmp:
    db=Path(tmp)/'timeclock.sqlite'
    sock=socket.socket();sock.bind(('127.0.0.1',0));port=sock.getsockname()[1];sock.close()
    key='northstar-fixture-key-2026'
    proc=subprocess.Popen([str(ROOT/'timeclock-server'),str(db),str(port)],env={**os.environ,'TZ':'America/Edmonton','DEVICE_KEY':key},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        base=f'http://127.0.0.1:{port}'
        for _ in range(120):
            try:urllib.request.urlopen(base+'/health',timeout=.2).close();break
            except Exception:time.sleep(.05)
        else:raise RuntimeError('server did not start: '+proc.stderr.read().decode())
        with sqlite3.connect(db) as conn:
            for eid,uid in [(2,'AA001122'),(3,'BB001122'),(4,'CC001122'),(5,'DD001122')]:
                conn.execute('INSERT INTO employees(id,name,email,created_at) VALUES(?,?,?,?)',(eid,'Hourly '+str(eid),f'e{eid}@example.test','2026-09-01T00:00:00Z'))
                conn.execute('INSERT INTO cards(uid,employee_id) VALUES(?,?)',(uid,eid))
                conn.execute("UPDATE card_ownership_history SET effective_at='2026-09-01T00:00:00Z' WHERE uid=?",(uid,))
                # These weekly fields are deliberately populated, just as in the real UI.
                conn.execute("INSERT INTO schedule_profiles(employee_id,pay_type,start_time,end_time,weekday_mask) VALUES(?,'hourly','09:00','17:00',31)",(eid,))
            conn.execute("INSERT INTO registered_clocks(clock_id,device_key,active) VALUES('NS-01',?,1)",(key,))
            for eid,day,start in [(3,'2026-10-20','09:00'),(4,'2026-10-21','09:00'),(5,'2026-10-22','09:00')]:
                conn.execute('INSERT INTO schedule_shifts(employee_id,work_date,start_time,end_time) VALUES(?,?,?,?)',(eid,day,start,'17:00'))
        def swipe(uid,day,h,code):
            event=code*32
            data=json.dumps({'event_id':event,'clock_id':'NS-01','card_uid':uid,'swiped_at':utc(day,h)}).encode()
            req=urllib.request.Request(base+'/api/v1/swipes',data=data,headers={'Content-Type':'application/json','X-Clock-ID':'NS-01','X-Device-Key':key})
            with urllib.request.urlopen(req,timeout=5) as resp:return json.load(resp)
        a=swipe('AA001122','2026-10-20','10:00:00','a')
        check('Unscheduled hourly swipe is IN',a.get('classification')=='IN')
        with sqlite3.connect(db) as conn:
            check('Unscheduled hourly late minutes zero',conn.execute("SELECT late_minutes FROM attendance_events WHERE event_id=?",('a'*32,)).fetchone()[0]==0)
            check('Unscheduled hourly does not queue late notice',conn.execute("SELECT count(*) FROM email_queue WHERE recipient='e2@example.test'").fetchone()[0]==0)
        b=swipe('BB001122','2026-10-20','09:05:00','b')
        c=swipe('CC001122','2026-10-21','09:06:00','c')
        d=swipe('DD001122','2026-10-22','09:12:00','d')
        check('At 5-minute boundary no late classification',b.get('classification')=='IN')
        check('6 minutes past dated shift classified late',c.get('classification')=='IN_LATE')
        check('12 minutes past dated shift classified late',d.get('classification')=='IN_LATE')
        with sqlite3.connect(db) as conn:
            for code,mins in [('b',0),('c',6),('d',12)]:
                check('Recorded lateness '+str(mins),conn.execute('SELECT late_minutes FROM attendance_events WHERE event_id=?',(code*32,)).fetchone()[0]==mins)
            check('No late notice for within-grace dated shift',conn.execute("SELECT count(*) FROM email_queue WHERE recipient='e3@example.test'").fetchone()[0]==0)
            # A 6-minute dated shift may queue notifications if configured; no assertion on default template enablement.
        print(json.dumps({'result':'PASS','count':len(checks),'checks':checks},indent=2))
    finally:
        proc.terminate();proc.communicate(timeout=5)
