from pathlib import Path
import sqlite3

ROOT = Path(__file__).resolve().parents[1]
SRC = (ROOT/'src/time_off.inc').read_text()
SERVER = (ROOT/'src/server.c').read_text()

def test_employee_and_manager_day_fields():
    for x in ('requested_days','return_date','acknowledged_at','timeoff_day_preview','timeoff_day_count'):
        assert x in SRC
    assert 'Number(f.get(\'requested_days\'))!==dayCount' in SRC
    assert 'document.getElementById(\'status\')' in SRC
    assert SRC.count('sqlite3_str_appendf(o,",\\"requested_days\\":%d,\\"return_date\\":",sqlite3_column_int(q,8))') == 2

def test_unused_templates_hidden():
    assert 'rows.filter(x=>x.implemented)' in SERVER

def test_calendar_example():
    db=sqlite3.connect(':memory:')
    db.executescript("""CREATE TABLE statutory_holidays(day TEXT PRIMARY KEY,name TEXT);
    CREATE TABLE schedule_profiles(employee_id INTEGER PRIMARY KEY,pay_type TEXT,weekday_mask INTEGER);
    CREATE TABLE schedule_shifts(employee_id INTEGER,work_date TEXT);
    INSERT INTO schedule_profiles VALUES(1,'salary',31);
    INSERT INTO statutory_holidays VALUES('2026-10-12','Thanksgiving');
    INSERT INTO schedule_shifts VALUES(2,'2026-10-17');""")
    query='''WITH RECURSIVE dates(day) AS (SELECT date(?1) UNION ALL SELECT date(day,'+1 day') FROM dates WHERE day<date(?2))
    SELECT SUM(CASE WHEN EXISTS(SELECT 1 FROM statutory_holidays h WHERE h.day=dates.day) THEN 0
    WHEN EXISTS(SELECT 1 FROM schedule_shifts sh WHERE sh.employee_id=?3 AND sh.work_date=dates.day) THEN 1
    WHEN EXISTS(SELECT 1 FROM schedule_profiles p WHERE p.employee_id=?3 AND p.pay_type='salary') THEN
    CASE WHEN (SELECT weekday_mask FROM schedule_profiles WHERE employee_id=?3) & (1 << ((CAST(strftime('%w',dates.day) AS INTEGER)+6)%7)) != 0 THEN 1 ELSE 0 END
    WHEN CAST(strftime('%w',dates.day) AS INTEGER) BETWEEN 1 AND 5 THEN 1 ELSE 0 END) FROM dates'''
    assert db.execute(query,('2026-10-12','2026-10-16',1)).fetchone()[0]==4
    assert db.execute(query,('2026-10-12','2026-10-17',2)).fetchone()[0]==5
