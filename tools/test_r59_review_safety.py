"""r59: review must never confirm unpaid incomplete attendance."""
import datetime as dt
import ctypes
import ctypes.util
import json
import os
from pathlib import Path
import socket
import sqlite3
import subprocess
import tempfile
import time
import urllib.error
import urllib.request

ROOT=Path(__file__).resolve().parents[1]
TOKEN='e'*64

def test_missing_out_is_blocked_and_employee_can_report_issue():
    with tempfile.TemporaryDirectory() as td:
        db=Path(td)/'clock.db'
        with socket.socket() as so:
            so.bind(('127.0.0.1',0));port=so.getsockname()[1]
        proc=subprocess.Popen([str(ROOT/'timeclock-server'),str(db),str(port)],cwd=ROOT,
            env={**os.environ,'DEVICE_KEY':'0123456789abcdef0123456789abcdef'},
            stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        try:
            for _ in range(60):
                try:
                    urllib.request.urlopen(f'http://127.0.0.1:{port}/health',timeout=.2).close();break
                except OSError:time.sleep(.05)
            else:raise AssertionError('Server did not start')
            today=dt.date.today();end=today-dt.timedelta(days=1);start=end-dt.timedelta(days=13)
            lib=ctypes.CDLL(ctypes.util.find_library('crypt'));lib.crypt.argtypes=[ctypes.c_char_p,ctypes.c_char_p];lib.crypt.restype=ctypes.c_char_p
            hashvalue=lib.crypt(TOKEN.encode(),b'$6$rounds=5000$payreview2026$').decode()
            with sqlite3.connect(db) as c:
                c.execute("INSERT INTO employees(name,email) VALUES('Sam','sam@example.test')")
                c.execute("INSERT INTO employees(name,email) VALUES('Manager','manager@example.test')")
                c.execute("INSERT INTO employee_roles(employee_id,role) VALUES(2,'manager')")
                c.execute('INSERT INTO pay_periods(starts,ends) VALUES(?,?)',(str(start),str(end)))
                c.execute('INSERT INTO swipes(event_id,clock_id,card_uid,employee_id,swiped_at) VALUES(?,?,?,?,?)',('in','clock','A1',1,f'{end}T09:00:00Z'))
                c.execute("INSERT INTO attendance_events(event_id,employee_id,kind) VALUES('in',1,'IN')")
                c.execute("INSERT INTO review_links(employee_id,period_id,token_hash,expires_at,created_by,created_at) VALUES(1,1,?,CAST(strftime('%s','now') AS INTEGER)+86400,2,CAST(strftime('%s','now') AS INTEGER))",(hashvalue,))
            base=f'http://127.0.0.1:{port}/review/{TOKEN}'
            with urllib.request.urlopen(base+'/detail') as r:
                detail=json.load(r)
            assert detail['entries'][0]['kind']=='IN'
            req=urllib.request.Request(base+'/sign',data=b'ack=I+confirm+I+reviewed+my+hours',method='POST')
            try:
                urllib.request.urlopen(req)
            except urllib.error.HTTPError as e:
                assert e.code==409
                assert 'Fix missing or unmatched punches' in e.read().decode()
            else:raise AssertionError('Unpaired punch was confirmed')
            with sqlite3.connect(db) as c:
                assert c.execute('SELECT count(*) FROM period_signoffs').fetchone()[0]==0
            req=urllib.request.Request(base+'/issue',data=b'note=My+OUT+punch+is+missing',method='POST')
            with urllib.request.urlopen(req) as r:
                assert json.load(r)['status']=='queued'
            with sqlite3.connect(db) as c:
                assert c.execute("SELECT count(*) FROM email_queue WHERE subject LIKE 'Timesheet review needs attention:%'").fetchone()[0]==1
        finally:
            proc.terminate();proc.wait(timeout=5)
            proc.stderr.close()


def test_review_page_and_timeoff_defaults():
    review=(ROOT/'src/account_review.inc').read_text()
    timeoff=(ROOT/'src/time_off.inc').read_text()
    assert 'Fix with your manager first' in review
    assert 'Yes, these hours are correct' in review
    assert 'Something is wrong' in review
    assert 'newrequest.elements.requested_days.value=j.days' in timeoff
    assert 'suggested_return' in timeoff
    assert "name='ack'" not in timeoff
