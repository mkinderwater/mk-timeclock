"""r37 regressions: date validation, overlap, leave reporting, cancellation notices."""
from pathlib import Path
import re, sqlite3

ROOT=Path(__file__).resolve().parents[1]
SRC=(ROOT/'src/time_off.inc').read_text()
PAY=(ROOT/'src/payroll_timesheet.h').read_text()
SERVER=(ROOT/'src/server.c').read_text()

def test_distinct_decision_email_keys_and_copy():
    assert '"timeoff_decision:%d:%s",id,action' in SRC
    assert 'Your %s request for %s to %s was sent to your manager.' in SRC
    assert 'Request a new link</a>' in SRC
    assert 'UI_HEAD_LOGIN("Link expired")' in SRC

def test_dates_and_overlap_database_rules():
    db=sqlite3.connect(':memory:')
    db.execute('CREATE TABLE timeoff_requests(employee_id INTEGER,starts TEXT,ends TEXT,status TEXT,cancelled_at TEXT)')
    db.execute("INSERT INTO timeoff_requests VALUES(9,'2026-11-02','2026-11-04','approved',NULL)")
    overlap="SELECT EXISTS(SELECT 1 FROM timeoff_requests WHERE employee_id=?1 AND status IN ('pending','approved') AND cancelled_at IS NULL AND starts<=?3 AND ends>=?2)"
    assert db.execute(overlap,(9,'2026-11-03','2026-11-05')).fetchone()[0]==1
    assert db.execute(overlap,(9,'2026-11-05','2026-11-06')).fetchone()[0]==0
    db.execute("UPDATE timeoff_requests SET cancelled_at='2026-11-01' WHERE employee_id=9")
    assert db.execute(overlap,(9,'2026-11-03','2026-11-05')).fetchone()[0]==0
    allowed="SELECT date(?1)>=date('now','-3 days') AND julianday(?2)-julianday(?1) BETWEEN 0 AND 365"
    assert db.execute(allowed,('2020-01-01','2020-01-02')).fetchone()[0]==0
    assert db.execute(allowed,('2026-11-01','2096-11-01')).fetchone()[0]==0
    assert db.execute(allowed,('2026-11-01','2026-11-05')).fetchone()[0]==1

def test_payroll_includes_unscheduled_weekdays():
    m=re.search(r'const char \*leave_sql=\s*(.*?);\s*if\(sqlite3_prepare',PAY,re.S)
    assert m, 'leave query missing'
    query=''.join(re.findall(r'"([^"\n]*)"',m.group(1)))
    db=sqlite3.connect(':memory:')
    db.executescript('''CREATE TABLE employees(id INTEGER PRIMARY KEY,name TEXT);
      CREATE TABLE cards(employee_id INTEGER,uid TEXT,active INTEGER);
      CREATE TABLE timeoff_requests(id INTEGER PRIMARY KEY,employee_id INTEGER,kind TEXT,starts TEXT,ends TEXT,status TEXT,cancelled_at TEXT);
      CREATE TABLE effective_punches(employee_id INTEGER,at TEXT);
      CREATE TABLE schedule_profiles(employee_id INTEGER,pay_type TEXT,weekday_mask INTEGER);
      CREATE TABLE schedule_shifts(employee_id INTEGER,work_date TEXT);
      CREATE TABLE statutory_holidays(day TEXT,name TEXT);
      INSERT INTO employees VALUES(1,'Sam');
      INSERT INTO timeoff_requests VALUES(1,1,'vacation','2026-10-07','2026-10-08','approved',NULL);''')
    rows=db.execute(query,('2026-10-01','2026-10-15')).fetchall()
    assert [row[2] for row in rows]==['2026-10-07','2026-10-08']
    db.execute("UPDATE timeoff_requests SET cancelled_at='2026-10-03' WHERE id=1")
    assert not db.execute(query,('2026-10-01','2026-10-15')).fetchall()

def test_welcome_no_bad_address_fallback():
    assert 'Ask your manager to request time off.' in SERVER
    assert 'To request time off, visit {{timeoff_link}}.' in SERVER
