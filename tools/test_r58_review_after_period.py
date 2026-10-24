"""Automated review notices follow ended/open periods, not payroll paydays."""
import datetime as dt
import os
from pathlib import Path
import signal
import sqlite3
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]

def boot(path):
    p=subprocess.Popen([str(ROOT/'timeclock-server'),str(path),'19978'],env={**os.environ,'DEVICE_KEY':'0123456789abcdef0123456789abcdef'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    time.sleep(.25)
    if p.poll() is not None: raise AssertionError(p.stderr.read().decode())
    p.send_signal(signal.SIGTERM);p.wait(timeout=4);p.stderr.close()

def test_review_period_not_payday_and_idempotence():
    with tempfile.TemporaryDirectory() as tmp:
        path=Path(tmp)/'clock.db';boot(path)
        today=dt.date.today();end=today-dt.timedelta(days=1)
        start=end-dt.timedelta(days=13)
        c=sqlite3.connect(path)
        c.execute("INSERT INTO employees(name,email) VALUES('Manager','manager@example.com')")
        c.execute("INSERT INTO employee_roles VALUES(1,'manager')")
        for name,email,enabled in [('Lee','lee@example.com',1),('Sam','sam@example.com',1),('Opted out','out@example.com',0),('No email','',1)]:
            c.execute('INSERT INTO employees(name,email,auto_review_enabled) VALUES(?,?,?)',(name,email,enabled))
        for employee in (2,3,4,5):
            for kind,h in [('IN',9),('OUT',17)]:
                c.execute("INSERT INTO swipes(event_id,clock_id,card_uid,employee_id,swiped_at) VALUES(?,?,?,?,?)",(f'{employee}-{kind}','clock','AB12',employee,f'{end}T{h:02}:00:00Z'))
                c.execute("INSERT INTO attendance_events(event_id,employee_id,kind) VALUES(?,?,?)",(f'{employee}-{kind}',employee,kind))
        c.execute('INSERT INTO pay_periods(starts,ends) VALUES(?,?)',(str(start),str(end)))
        c.execute("INSERT INTO payroll_settings(id,anchor_payday,recipient) VALUES(1,?,?)",(str(today+dt.timedelta(days=13)),'payroll@example.com'))
        # Employee already confirmed for their current punches: no automatic link.
        c.execute("INSERT INTO period_signoffs(period_id,employee_id,role,signed_by,punch_count,approved_seconds) VALUES(1,3,'employee',3,2,28800)")
        c.commit();c.close()
        boot(path);boot(path)
        c=sqlite3.connect(path)
        rows=c.execute("SELECT recipient,event_id FROM email_queue WHERE event_id LIKE 'auto-review:%' ORDER BY event_id").fetchall()
        assert rows==[('lee@example.com','auto-review:1:2')],rows
        assert c.execute("SELECT count(*) FROM email_queue WHERE event_id='auto-review-summary:1'").fetchone()[0]==1
        assert c.execute('SELECT count(*) FROM review_links').fetchone()[0]==1
        assert c.execute('SELECT days_after FROM review_send_settings').fetchone()[0]==1
        c.close()

def test_fresh_review_setting_persists():
    with tempfile.TemporaryDirectory() as tmp:
        path=Path(tmp)/'clock.db';boot(path)
        c=sqlite3.connect(path)
        assert c.execute('SELECT days_after FROM review_send_settings').fetchone()[0]==1
        c.execute('UPDATE review_send_settings SET days_after=5');c.commit();c.close()
        boot(path)
        c=sqlite3.connect(path)
        assert c.execute('SELECT days_after FROM review_send_settings').fetchone()[0]==5
        c.close()
