#!/usr/bin/env python3
"""Behavioral checks against a fresh r62 Lite server database."""
import os
from pathlib import Path
import socket
import sqlite3
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="lite-mode-") as td:
    dbfile = Path(td) / 'fresh.db'
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        port = s.getsockname()[1]
    env = dict(os.environ, DEVICE_KEY='test-device-key-0123456789')
    proc = subprocess.Popen([str(ROOT / 'timeclock-server'), str(dbfile), str(port)], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        for _ in range(40):
            if dbfile.exists():
                try:
                    db = sqlite3.connect(dbfile)
                    db.execute('SELECT attendance_mode FROM employees LIMIT 1').fetchall()
                    break
                except sqlite3.Error:
                    pass
            time.sleep(.1)
        else:
            raise AssertionError('server did not create a fresh schema')
        assert proc.poll() is None, 'server exited'
        assert db.execute('PRAGMA integrity_check').fetchone()[0] == 'ok'
        db.execute("INSERT INTO employees(name,attendance_mode) VALUES('Punch Worker','punch_required')")
        db.execute("INSERT INTO employees(name,attendance_mode) VALUES('Vacation Worker','vacation_only')")
        ids = db.execute('SELECT id,attendance_mode FROM employees ORDER BY id').fetchall()
        db.execute("INSERT INTO cards(uid,employee_id) VALUES('AABBCCDD',?)", (ids[0][0],))
        db.execute("INSERT INTO cards(uid,employee_id) VALUES('11223344',?)", (ids[1][0],))
        db.commit()
        # The real card-cache selection includes both known attendance modes.
        source=(ROOT / 'src/server.c').read_text()
        card_sql=source.split('if(sqlite3_prepare_v2(db,"SELECT upper(c.uid)',1)[1].split('"',1)[0]
        rows=db.execute('SELECT upper(c.uid)'+card_sql).fetchall()
        assert [(r[0],r[2]) for r in rows]==[('11223344','vacation_only'),('AABBCCDD','punch_required')],rows
        for employee_id,mode in ids:
            db.execute("INSERT INTO schedule_profiles(employee_id,pay_type,start_time,end_time,weekday_mask) VALUES(?,'salary','08:00','16:00',31)",(employee_id,))
        db.commit()
        for job in ('timeclock-attendance-reminders.py','timeclock-missing-digest.py'):
            script=(ROOT / job).read_text()
            assert script.count("e.attendance_mode='punch_required'")>=2,job
            rows=db.execute("SELECT e.id FROM employees e JOIN schedule_profiles p ON p.employee_id=e.id AND p.pay_type='salary' WHERE e.active=1 AND e.attendance_mode='punch_required' AND (p.weekday_mask & ?) != 0",(1,)).fetchall()
            assert rows==[(ids[0][0],)],(job,rows)
        try:
            db.execute("INSERT INTO employees(name,attendance_mode) VALUES('Invalid','unrecognized')")
            raise AssertionError('invalid mode accepted')
        except sqlite3.IntegrityError:
            pass
        print('PASS: fresh schema, both card modes cached, vacation-only reminder exclusion, mode constraints')
    finally:
        proc.terminate()
        try: proc.wait(timeout=3)
        except subprocess.TimeoutExpired: proc.kill(); proc.wait()
