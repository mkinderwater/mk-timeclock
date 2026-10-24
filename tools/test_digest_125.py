"""Current behavior: independent daily exception detection and overnight reporting."""
import datetime as dt
import importlib.util
import os
import pathlib
import sqlite3
import tempfile
import unittest
from unittest.mock import patch
from zoneinfo import ZoneInfo

SCRIPT = pathlib.Path(__file__).resolve().parents[1] / 'timeclock-missing-digest.py'
spec = importlib.util.spec_from_file_location('digest', SCRIPT)
digest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(digest)
TZ = ZoneInfo('America/Edmonton')


class DigestTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.db = pathlib.Path(self.tmp.name)/'server.db'
        self.env = pathlib.Path(self.tmp.name)/'env'
        self.env.write_text('TIMECLOCK_ROLE=primary\n')
        conn = sqlite3.connect(self.db)
        conn.executescript('''
        CREATE TABLE clock_late_settings(clock_id TEXT PRIMARY KEY,grace_minutes INTEGER);
        CREATE TABLE holiday_settings(id INTEGER PRIMARY KEY,skip_alerts INTEGER);
        CREATE TABLE statutory_holidays(day TEXT PRIMARY KEY);
        CREATE TABLE employees(id INTEGER PRIMARY KEY,name TEXT,email TEXT,active INTEGER);
        CREATE TABLE schedule_profiles(employee_id INTEGER,pay_type TEXT,start_time TEXT,end_time TEXT,weekday_mask INTEGER);
        CREATE TABLE schedule_shifts(employee_id INTEGER,work_date TEXT,start_time TEXT,end_time TEXT);
        CREATE TABLE effective_punches(employee_id INTEGER,id TEXT,kind TEXT,at TEXT);
        CREATE TABLE employee_roles(employee_id INTEGER,role TEXT);
        CREATE TABLE user_accounts(employee_id INTEGER,disabled INTEGER);
        CREATE TABLE email_queue(id INTEGER PRIMARY KEY,recipient TEXT,subject TEXT,body TEXT,status TEXT,next_attempt TEXT,event_id TEXT UNIQUE);
        CREATE TABLE email_templates(id TEXT,enabled INTEGER);
        CREATE TABLE timeoff_requests(employee_id INTEGER,status TEXT,cancelled_at TEXT,starts TEXT,ends TEXT);
        CREATE TABLE attendance_absences(employee_id INTEGER,day TEXT,starts TEXT,cancelled_at TEXT,reason TEXT);
        INSERT INTO employees VALUES(1,'Overnight Worker',NULL,1),(2,'Morning Worker','morning@example.com',1),(3,'Manager','manager@example.com',1);
        INSERT INTO employee_roles VALUES(3,'manager');
        INSERT INTO user_accounts VALUES(3,0);
        INSERT INTO schedule_profiles VALUES(1,'hourly','22:00','09:00',31),(2,'hourly','09:00','17:00',31);
        INSERT INTO schedule_shifts VALUES(1,'2026-10-15','22:00','09:00'),(2,'2026-10-15','09:00','17:00');
        INSERT INTO effective_punches VALUES(1,'overnight-in','IN','2026-10-16T04:00:00+00:00');
        INSERT INTO email_templates VALUES('missing_in_employee',0),('missing_out_employee',0);
        ''')
        conn.commit()
        conn.close()

    def run_digest(self, when):
        with patch.dict(os.environ, {'TZ':'America/Edmonton'}):
            return digest.run(self.db,self.env,dt.datetime.fromisoformat(when).astimezone(TZ))

    def test_overnight_missing_out_after_eight_appears_next_morning(self):
        self.assertEqual(self.run_digest('2026-10-16T08:00:00-06:00'),1)  # yesterday's morning worker IN and OUT missing
        with sqlite3.connect(self.db) as c:
            body=c.execute('SELECT body FROM email_queue').fetchone()[0]
            self.assertNotIn('Overnight Worker',body)
            c.execute('DELETE FROM email_queue')
        self.assertEqual(self.run_digest('2026-10-17T08:00:00-06:00'),1)
        with sqlite3.connect(self.db) as c:
            body=c.execute('SELECT body FROM email_queue').fetchone()[0]
            self.assertIn('Overnight Worker: No OUT',body)

    def test_independent_of_disabled_templates_and_no_email(self):
        self.assertEqual(self.run_digest('2026-10-17T08:00:00-06:00'),1)
        self.assertEqual(self.run_digest('2026-10-17T08:00:00-06:00'),0)

    def test_out_suppresses_exception(self):
        with sqlite3.connect(self.db) as c:
            c.execute("INSERT INTO effective_punches VALUES(1,'overnight-out','OUT','2026-10-16T15:05:00+00:00')")
        self.assertEqual(self.run_digest('2026-10-17T08:00:00-06:00'),0)

    def test_slave_no_digest(self):
        self.env.write_text('TIMECLOCK_ROLE=slave\n')
        self.assertEqual(self.run_digest('2026-10-17T08:00:00-06:00'),0)

if __name__ == '__main__':
    unittest.main()
