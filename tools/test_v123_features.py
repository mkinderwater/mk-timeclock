from pathlib import Path
import datetime as dt
import importlib.util
import os
import sqlite3
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class V123Features(unittest.TestCase):
    def test_clock_approval_distinct(self):
        server=(ROOT/'src/server.c').read_text()
        agent=(ROOT/'src/agent.c').read_text()
        self.assertIn('"result\\\":\\\"APPROVED',server)
        self.assertIn('DISPLAY_APPROVED',agent)
        self.assertIn('return 3;',agent)
    def test_review_email_is_transactional(self):
        src=(ROOT/'src/account_review.inc').read_text()
        self.assertIn('if(ok&&email_now)',src)
        self.assertIn('INSERT OR IGNORE INTO email_queue',src)
        self.assertIn('if(ok)ok=sql("COMMIT")',src)
        self.assertIn('Create and email',src)
    def test_digest_one_per_day(self):
        spec=importlib.util.spec_from_file_location('timeclock_digest',ROOT/'timeclock-missing-digest.py')
        mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
        with tempfile.TemporaryDirectory() as folder:
            db=Path(folder)/'server.db';env=Path(folder)/'env'
            conn=sqlite3.connect(db)
            conn.executescript('''CREATE TABLE email_queue(recipient TEXT,subject TEXT,body TEXT,status TEXT,next_attempt TEXT,event_id TEXT UNIQUE);
                CREATE TABLE employees(id INTEGER PRIMARY KEY,name TEXT,email TEXT,active INTEGER);
                CREATE TABLE employee_roles(employee_id INTEGER,role TEXT);
                CREATE TABLE user_accounts(employee_id INTEGER,disabled INTEGER);
                CREATE TABLE clock_late_settings(clock_id TEXT PRIMARY KEY,grace_minutes INTEGER);
                CREATE TABLE holiday_settings(id INTEGER PRIMARY KEY,skip_alerts INTEGER);
                CREATE TABLE statutory_holidays(day TEXT PRIMARY KEY);
                CREATE TABLE schedule_shifts(employee_id INTEGER,work_date TEXT,start_time TEXT,end_time TEXT);
                CREATE TABLE schedule_profiles(employee_id INTEGER,pay_type TEXT,start_time TEXT,end_time TEXT,weekday_mask INTEGER);
                CREATE TABLE timeoff_requests(employee_id INTEGER,status TEXT,cancelled_at TEXT,starts TEXT,ends TEXT);
                CREATE TABLE attendance_absences(employee_id INTEGER,day TEXT,starts TEXT,cancelled_at TEXT,reason TEXT);
                CREATE TABLE effective_punches(employee_id INTEGER,id TEXT,kind TEXT,at TEXT);
                INSERT INTO schedule_profiles VALUES(1,'hourly','09:00','17:00',31);
                INSERT INTO schedule_shifts VALUES(1,'2026-10-15','09:00','17:00');''')
            conn.execute('INSERT INTO employees VALUES(1,"Employee","employee@example.invalid",1)')
            conn.execute('INSERT INTO employees VALUES(2,"Manager","manager@example.invalid",1)')
            conn.execute('INSERT INTO employee_roles VALUES(2,"manager")')
            conn.execute('INSERT INTO user_accounts VALUES(2,0)')
            conn.execute('INSERT INTO email_queue(event_id) VALUES("missing_in_employee:1:2026-10-15:08:00:16:00")')
            conn.commit();conn.close()
            env.write_text('TIMECLOCK_ROLE=primary\n')
            old=os.environ.get('TZ');os.environ['TZ']='America/Edmonton'
            try:
                now=dt.datetime(2026,10,16,8,0,tzinfo=dt.timezone(dt.timedelta(hours=-6)))
                self.assertEqual(mod.run(db,env,now),1)
                self.assertEqual(mod.run(db,env,now),0)
                env.write_text('TIMECLOCK_ROLE=slave\n')
                self.assertEqual(mod.run(db,env,now),0)
            finally:
                if old is None:os.environ.pop('TZ',None)
                else:os.environ['TZ']=old
if __name__=='__main__':unittest.main()
