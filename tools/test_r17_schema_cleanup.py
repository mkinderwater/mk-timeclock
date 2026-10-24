"""Validate fresh database and removal of redundant indexes on an existing database."""
import os
import sqlite3
import socket
import subprocess
import tempfile
import time
from pathlib import Path

root = Path(__file__).resolve().parent.parent
server = Path(os.getenv('TIMECLOCK_TEST_SERVER', str(root/'timeclock-server')))

def boot(db, port):
    proc = subprocess.Popen([str(server), str(db), str(port)], env={**os.environ, 'DEVICE_KEY': 'test-device-key-1234567890'}, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    for _ in range(60):
        if proc.poll() is not None:
            raise AssertionError(proc.communicate()[1].decode(errors='replace'))
        if db.exists():
            try:
                with sqlite3.connect(db) as connection:
                    if connection.execute("SELECT 1 FROM sqlite_master WHERE type='table' AND name='period_cycle_settings'").fetchone():
                        try:
                            with socket.create_connection(("127.0.0.1", port), timeout=.1):
                                return proc
                        except OSError:
                            pass
            except sqlite3.OperationalError:
                pass
        time.sleep(.1)
    proc.terminate()
    raise AssertionError('Database initialization timed out')

with tempfile.TemporaryDirectory() as temp:
    db = Path(temp) / 'timeclock.db'
    proc = boot(db, 19883)
    proc.terminate()
    proc.communicate(timeout=5)
    with sqlite3.connect(db) as con:
        con.execute('CREATE INDEX cards_uid_nocase ON cards(uid COLLATE NOCASE)')
        con.execute('CREATE INDEX pay_periods_dates ON pay_periods(starts,ends)')
        con.commit()
        assert con.execute('PRAGMA integrity_check').fetchone()[0] == 'ok'
    proc = boot(db, 19884)
    proc.terminate()
    proc.communicate(timeout=5)
    with sqlite3.connect(db) as con:
        indexes = {row[0] for row in con.execute("SELECT name FROM sqlite_master WHERE type='index'")}
        assert not {'cards_uid_nocase','pay_periods_dates'} & indexes, indexes
        assert 'swipes_employee_time' in indexes
        assert 'attendance_events_employee' in indexes
        assert con.execute('PRAGMA integrity_check').fetchone()[0] == 'ok'
        assert con.execute("SELECT cycle FROM period_cycle_settings WHERE id=1").fetchone()[0] == 'semi_monthly'
        assert len(con.execute("SELECT name FROM sqlite_master WHERE type='trigger'").fetchall()) == 8
    print('PASS r17 schema cleanup, live database retains necessary indexes')
