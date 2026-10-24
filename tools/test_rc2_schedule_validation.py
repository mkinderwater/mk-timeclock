#!/usr/bin/env python3
"""Exercise schedule validation through an authenticated running server."""
import json, os, socket, sqlite3, subprocess, tempfile, time, urllib.request, urllib.parse, urllib.error
from pathlib import Path
root=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='rc2-schedule-validation-') as tmp:
    dbpath=Path(tmp)/'db.sqlite'
    with socket.socket() as sock:
        sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
    proc=subprocess.Popen([str(root/'timeclock-server'),str(dbpath),str(port)],env={**os.environ,'DEVICE_KEY':'schedule-test-key'},stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    try:
        base=f'http://127.0.0.1:{port}'
        for _ in range(100):
            try:urllib.request.urlopen(base+'/health',timeout=.2).close();break
            except OSError:time.sleep(.05)
        else:raise AssertionError('server not ready')
        with sqlite3.connect(dbpath) as db:
            db.execute("INSERT INTO employees(id,name,email) VALUES(1,'Manager','manager@example.test')")
            db.execute("INSERT INTO employee_roles(employee_id,role) VALUES(1,'manager')")
            db.execute("INSERT INTO manager_cards(uid,manager_id,active) VALUES('A1B2C3D4',1,1)")
            db.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
            db.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+600)",('d'*64,))
            db.execute("INSERT INTO employees(id,name,email,schedule_exempt) VALUES(5,'Sophie Snow','sophie@example.test',1)")
            db.execute("INSERT INTO schedule_shifts(employee_id,work_date,start_time,end_time) VALUES(5,'2026-11-03','09:00','17:00')")
        def call(path,values=None):
            data=urllib.parse.urlencode(values).encode() if values else None
            req=urllib.request.Request(base+path,data=data,headers={'Cookie':'tc_session='+'d'*64,'Content-Type':'application/x-www-form-urlencoded'})
            try:
                with urllib.request.urlopen(req,timeout=5) as response:return response.status,json.load(response)
            except urllib.error.HTTPError as exc:return exc.code,json.load(exc)
        def save(mode,mask,**kw):return call('/api/v1/admin/schedules/save',dict(op='profile',employee_id='5',type=mode,start='08:30',end='16:30',mask=str(mask),**kw))
        status,_=save('',10);assert status==400
        status,_=save('recurring_weekly',0);assert status==400
        status,_=save('recurring_weekly',10,exempt='1');assert status==200
        status,result=call('/api/v1/admin/schedules');assert status==200
        sophie=next(x for x in result['employees'] if x['id']==5)
        assert sophie['schedule_mode']=='recurring_weekly' and not sophie['schedule_exempt']
        status,_=save('none',10,exempt='0');assert status==200
        status,result=call('/api/v1/admin/schedules');assert next(x for x in result['employees'] if x['id']==5)['schedule_exempt']
        # Overnight recurring work is anchored to the day it starts.
        status,reply=call('/api/v1/admin/schedules/save',dict(op='profile',employee_id='5',type='recurring_weekly',start='22:00',end='06:00',mask='10'))
        assert status==200,(status,reply)
        status,reply=call('/api/v1/admin/schedules')
        saved=next(x for x in reply['employees'] if x['id']==5)
        assert (saved['schedule_mode'],saved['start'],saved['end'],saved['mask'])==('recurring_weekly','22:00','06:00',10)
        status,reply=call('/api/v1/admin/schedules/save',dict(op='profile',employee_id='5',type='recurring_weekly',start='22:00',end='22:00',mask='10'))
        assert status==400 and 'must be different' in reply['error'],(status,reply)
        status,reply=call('/api/v1/admin/schedules/save',dict(op='profile',employee_id='5',type='recurring_weekly',start='22:00',end='06:00',mask='0'))
        assert status==400 and 'at least one working day' in reply['error'],(status,reply)
        status,reply=call('/api/v1/admin/schedules/save',dict(op='profile',employee_id='5',type='',start='22:00',end='06:00',mask='10'))
        assert status==400 and 'Choose a schedule type' in reply['error'],(status,reply)
        status,reply=call('/api/v1/admin/schedules/save',dict(op='profile',employee_id='5',type='recurring_weekly',start='',end='06:00',mask='10'))
        assert status==400 and 'Enter a start and end time' in reply['error'],(status,reply)
        status,reply=call('/api/v1/admin/schedules/save',dict(op='profile',employee_id='5',type='recurring_weekly',start='25:00',end='06:00',mask='10'))
        assert status==400 and 'Enter a valid time' in reply['error'],(status,reply)
        status,_=save('dated_shifts',10);assert status==200
        with sqlite3.connect(dbpath) as db:
            assert db.execute("SELECT count(*) FROM schedule_shifts WHERE employee_id=5 AND work_date='2026-11-03'").fetchone()[0]==1
            assert db.execute('SELECT schedule_exempt FROM employees WHERE id=5').fetchone()[0]==0
        print('PASS: overnight recurring save/reload; precise validation; mode-owned exemption; dated shifts retained')
    finally:
        proc.terminate();proc.wait(timeout=5)
