#!/usr/bin/env python3
"""Live regression: no historical catch-up or manual generation HTTP routes."""
import datetime as dt, os, socket, sqlite3, subprocess, tempfile, time, urllib.request, urllib.error
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as td:
    dbfile=str(Path(td)/'clock.sqlite')
    sock=socket.socket();sock.bind(('127.0.0.1',0));port=sock.getsockname()[1];sock.close()
    def boot():
        p=subprocess.Popen([str(ROOT/'timeclock-server'),dbfile,str(port)],env={**os.environ,'DEVICE_KEY':'test-key-123456789'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        for _ in range(100):
            try:
                urllib.request.urlopen(f'http://127.0.0.1:{port}/health',timeout=.2).close();return p
            except Exception:time.sleep(.04)
        raise AssertionError('server did not start')
    def stop(p):
        p.terminate()
        try:p.communicate(timeout=2)
        except subprocess.TimeoutExpired:
            p.kill();p.communicate(timeout=3)
    p=boot()
    with sqlite3.connect(dbfile) as db:
        first=db.execute('SELECT starts,ends FROM pay_periods ORDER BY starts').fetchall()
        today=dt.date.today().isoformat()
        assert len(first)==3,first
        assert all(end>=today for start,end in first),first
        assert first[0][0]<=today<=first[0][1],first
        assert len([start for start,end in first if start>today])==2,first
        for a,b in zip(first,first[1:]):assert dt.date.fromisoformat(a[1])+dt.timedelta(days=1)==dt.date.fromisoformat(b[0]),first
        # Genuine historical record is kept; no completed periods are manufactured to fill the gap.
        db.execute('DELETE FROM pay_periods')
        db.execute("INSERT INTO pay_periods(starts,ends,status) VALUES('2020-01-01','2020-01-15','closed')")
    stop(p);p=boot()
    with sqlite3.connect(dbfile) as db:
        periods=db.execute('SELECT starts,ends,status FROM pay_periods ORDER BY starts').fetchall()
        assert ('2020-01-01','2020-01-15','closed') in periods,periods
        assert all(end>=today or (start,end)==('2020-01-01','2020-01-15') for start,end,_ in periods),periods
    # Authenticated manager: both retired endpoints must return 404 (not create records).
    with sqlite3.connect(dbfile) as db:
        db.execute("INSERT INTO employees(id,name) VALUES(1,'Manager')")
        db.execute("INSERT INTO employee_roles VALUES(1,'manager')")
        db.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
        db.execute("INSERT INTO manager_cards(uid,manager_id,active) VALUES('A1B2C3D4',1,1)")
        db.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+600)",('a'*64,))
    for route,method in [('/api/v1/admin/periods/preview','GET'),('/api/v1/admin/periods/generate','POST')]:
        req=urllib.request.Request(f'http://127.0.0.1:{port}{route}',method=method,data=b'' if method=='POST' else None,headers={'Cookie':'tc_session='+'a'*64})
        try:
            urllib.request.urlopen(req,timeout=3)
        except urllib.error.HTTPError as exc:
            assert exc.code==404,(route,exc.code)
        else:raise AssertionError(f'manual route remains accessible: {route}')
    # Changing cadence keeps current history and yields contiguous future dates.
    import urllib.parse
    form=urllib.parse.urlencode({'cycle':'biweekly','anchor':'2026-10-19'}).encode()
    req=urllib.request.Request(f'http://127.0.0.1:{port}/api/v1/admin/periods/cycle',data=form,headers={'Cookie':'tc_session='+'a'*64,'Content-Type':'application/x-www-form-urlencoded'})
    with urllib.request.urlopen(req,timeout=4) as response:assert response.status==200
    stop(p);p=boot()
    with sqlite3.connect(dbfile) as db:
        periods=db.execute('SELECT starts,ends FROM pay_periods WHERE ends>=? ORDER BY starts',(today,)).fetchall()
        assert periods and periods[0][0]<=today<=periods[0][1],periods
        assert len([start for start,end in periods if start>today])>=2,periods
        for a,b in zip(periods,periods[1:]):
            assert dt.date.fromisoformat(a[1])+dt.timedelta(days=1)==dt.date.fromisoformat(b[0]),periods
    stop(p)
    print('PASS fresh current + two future; no past backfill; manual routes 404; cycle change contiguous')
