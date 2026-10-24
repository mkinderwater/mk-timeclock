"""RC2 acceptance: stale employee/manager signoffs, leave context, leave-only manager rows."""
import ctypes
import ctypes.util
import datetime
import json
import os
from pathlib import Path
import socket
import sqlite3
import subprocess
import tempfile
import time
import urllib.error
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
crypt = ctypes.CDLL(ctypes.util.find_library('crypt'))
crypt.crypt.argtypes = [ctypes.c_char_p, ctypes.c_char_p]
crypt.crypt.restype = ctypes.c_char_p

with tempfile.TemporaryDirectory(prefix='rc2-signoffs-') as directory:
    path = Path(directory) / 'clock.db'
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    env = {**os.environ, 'DEVICE_KEY': 'rc2-signoff-test-key'}
    server = subprocess.Popen([str(ROOT / 'timeclock-server'), str(path), str(port)], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        host = f'http://127.0.0.1:{port}'
        for _ in range(150):
            try:
                urllib.request.urlopen(host + '/health', timeout=.25).close()
                break
            except OSError:
                time.sleep(.05)
        else:
            raise AssertionError('Server did not start')

        start = str(datetime.date.today() - datetime.timedelta(days=31))
        end = str(datetime.date.today() - datetime.timedelta(days=30))
        day = start
        token2 = '2' * 64
        token3 = '3' * 64
        with sqlite3.connect(path) as db:
            db.execute("INSERT INTO employees(id,name,email) VALUES(1,'Manager','mgr@example.test')")
            db.execute("INSERT INTO employees(id,name,email,attendance_mode) VALUES(2,'Punch Worker','p@example.test','punch_required')")
            db.execute("INSERT INTO employees(id,name,email,attendance_mode) VALUES(3,'Leave Worker','l@example.test','punch_required')")
            db.execute("INSERT INTO employee_roles(employee_id,role) VALUES(1,'manager')")
            db.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
            db.execute("INSERT INTO manager_cards(uid,manager_id,active) VALUES('A1B2C3D4',1,1)")
            db.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+1800)", ('d' * 64,))
            period = db.execute('INSERT INTO pay_periods(starts,ends) VALUES(?,?) RETURNING id', (start, end)).fetchone()[0]
            for emp, token in [(2, token2), (3, token3)]:
                hashed = crypt.crypt(token.encode(), b'$6$rounds=5000$payreview2026$').decode()
                db.execute("INSERT INTO review_links(employee_id,period_id,token_hash,expires_at,created_by,created_at) VALUES(?,?,?,CAST(strftime('%s','now') AS INTEGER)+3600,1,CAST(strftime('%s','now') AS INTEGER))", (emp, period, hashed))
            db.execute("INSERT INTO attendance_absences(employee_id,manager_id,day,reason) VALUES(3,1,?,'Sick')", (day,))
            db.execute("INSERT INTO punch_corrections(employee_id,manual_id,action,effective_at,kind,reason,manager_id) VALUES(2,'test-in-1','add',?,'IN','Initial punch',1)", (day + 'T09:00:00Z',))
            db.execute("INSERT INTO punch_corrections(employee_id,manual_id,action,effective_at,kind,reason,manager_id) VALUES(2,'test-out-1','add',?,'OUT','Initial punch',1)", (day + 'T10:00:00Z',))

        def call(route, params=None, manager=False):
            headers = {'Cookie': 'tc_session=' + 'd'*64} if manager else {}
            body = None
            if params is not None:
                body = urllib.parse.urlencode(params).encode()
                headers['Content-Type'] = 'application/x-www-form-urlencoded'
            req = urllib.request.Request(host + route, data=body, headers=headers)
            try:
                with urllib.request.urlopen(req, timeout=10) as result:
                    return result.status, json.loads(result.read())
            except urllib.error.HTTPError as exc:
                return exc.code, {"raw": exc.read().decode(errors="replace")[:500]}

        def detail(token):
            code, response = call('/review/' + token + '/detail')
            assert code == 200, response
            return response

        def approve(token):
            code, response = call('/review/' + token + '/approve', {})
            assert code == 200, (code, response)

        def people():
            code, response = call('/api/v1/admin/periods/people?period_id=' + str(period), manager=True)
            assert code == 200, (code, response)
            return {p['id']: p for p in response['people']}

        def manager_approve(emp):
            code, response = call('/api/v1/admin/periods', {'action':'sign','starts':start,'ends':end,'employee_id':str(emp),'reason':'Reviewed attendance and leave'}, manager=True)
            assert code == 200, (code, response)

        # Fourth issue: manager can review a leave-only employee before any signoff exists.
        assert 3 in people(), 'Leave-only employee missing from review list'
        assert people()[3]['punches'] == 0

        # Employee approval is idempotent while inputs are unchanged.
        approve(token2)
        with sqlite3.connect(path) as db:
            original_fp = db.execute("SELECT punch_count,approved_seconds,leave_fingerprint FROM period_signoffs WHERE employee_id=2 AND role='employee'").fetchone()
        approve(token2)
        with sqlite3.connect(path) as db:
            assert db.execute("SELECT count(*) FROM period_signoffs WHERE employee_id=2 AND role='employee'").fetchone()[0] == 1

        # Issue 1: a new completed punch pair makes the employee signoff stale, then reapproval updates it.
        with sqlite3.connect(path) as db:
            db.execute("INSERT INTO punch_corrections(employee_id,manual_id,action,effective_at,kind,reason,manager_id) VALUES(2,'test-in-2','add',?,'IN','Another shift',1)", (day + 'T11:00:00Z',))
            db.execute("INSERT INTO punch_corrections(employee_id,manual_id,action,effective_at,kind,reason,manager_id) VALUES(2,'test-out-2','add',?,'OUT','Another shift',1)", (day + 'T12:00:00Z',))
        assert detail(token2)['signed'] is False
        approve(token2)
        assert detail(token2)['signed'] is True
        with sqlite3.connect(path) as db:
            updated_fp = db.execute("SELECT punch_count,approved_seconds,leave_fingerprint FROM period_signoffs WHERE employee_id=2 AND role='employee'").fetchone()
        assert updated_fp[0] > original_fp[0]
        with sqlite3.connect(path) as db:
            assert db.execute("SELECT count(*) FROM period_signoff_history WHERE employee_id=2 AND role='employee'").fetchone()[0] == 1

        # Issue 2: an independent late punch pair invalidates manager signoff, then manager can sign again.
        manager_approve(2)
        with sqlite3.connect(path) as db:
            db.execute("INSERT INTO punch_corrections(employee_id,manual_id,action,effective_at,kind,reason,manager_id) VALUES(2,'test-in-3','add',?,'IN','Late synced pair',1)", (day + 'T13:00:00Z',))
            db.execute("INSERT INTO punch_corrections(employee_id,manual_id,action,effective_at,kind,reason,manager_id) VALUES(2,'test-out-3','add',?,'OUT','Late synced pair',1)", (day + 'T14:00:00Z',))
        assert people()[2]['manager_current'] is False
        code, response = call('/api/v1/admin/periods', {'action':'close','starts':start,'ends':end,'employee_id':'0','reason':'Reviewed records'}, manager=True)
        assert code == 409, (code, response)
        manager_approve(2)
        assert people()[2]['manager_current'] is True
        with sqlite3.connect(path) as db:
            assert db.execute("SELECT count(*) FROM period_signoffs WHERE employee_id=2 AND role='manager'").fetchone()[0] == 1
            assert db.execute("SELECT count(*) FROM period_signoff_history WHERE employee_id=2 AND role='manager'").fetchone()[0] == 1

        # Issue 3: punch-required employee with leave only: signoff invalidates when another absence is added, even with 0 hours.
        approve(token3)
        assert detail(token3)['signed'] is True
        manager_approve(3)
        with sqlite3.connect(path) as db:
            db.execute("INSERT INTO attendance_absences(employee_id,manager_id,day,reason) VALUES(3,1,?,'Bereavement')", (end,))
        assert detail(token3)['signed'] is False
        assert people()[3]['manager_current'] is False
        approve(token3)
        assert detail(token3)['signed'] is True
        manager_approve(3)
        assert people()[3]['manager_current'] is True
        with sqlite3.connect(path) as db:
            assert db.execute("SELECT count(*) FROM period_signoff_history WHERE employee_id=3").fetchone()[0] == 2
        code, response = call('/api/v1/admin/periods', {'action':'close','starts':start,'ends':end,'employee_id':'0','reason':'Reviewed records'}, manager=True)
        assert code == 200, (code, response)
        code, listing = call('/api/v1/admin/periods', manager=True)
        period = next(p for p in listing['periods'] if p['starts']==start and p['ends']==end)
        assert not period['changed_after_close'], period
        with sqlite3.connect(path) as db:
            db.execute("INSERT INTO attendance_absences(employee_id,manager_id,day,reason) VALUES(3,1,?,'Sick')", (end,))
        code, listing = call('/api/v1/admin/periods', manager=True)
        period = next(p for p in listing['periods'] if p['starts']==start and p['ends']==end)
        assert period['changed_after_close'], period
        print('PASS: leave-only row, employee reapproval, manager reapproval, leave changes invalidate both signoffs')
    finally:
        server.terminate()
        server.communicate(timeout=8)
