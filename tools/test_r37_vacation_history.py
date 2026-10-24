"""Payroll keeps the last APPROVED vacation weekday as historical context."""
from pathlib import Path
import re,sqlite3
PAY=(Path(__file__).resolve().parents[1]/'src/payroll_timesheet.h').read_text()
def test_last_vacation_date_and_cancellation():
    m=re.search(r'const char \*history_sql=\s*(.*?);\s*if\(sqlite3_prepare',PAY,re.S)
    assert m
    query=''.join(re.findall(r'"([^"\n]*)"',m.group(1)))
    db=sqlite3.connect(':memory:')
    db.executescript('''CREATE TABLE employees(id INTEGER PRIMARY KEY,name TEXT,active INTEGER);
    CREATE TABLE cards(employee_id INTEGER,uid TEXT,active INTEGER);
    CREATE TABLE effective_punches(employee_id INTEGER,at TEXT);
    CREATE TABLE timeoff_requests(employee_id INTEGER,kind TEXT,starts TEXT,ends TEXT,status TEXT,cancelled_at TEXT);
    INSERT INTO employees VALUES(1,'Sam',1),(2,'Alex',1),(3,'Sam',1);
    INSERT INTO timeoff_requests VALUES(1,'vacation','2026-09-28','2026-10-02','approved',NULL);
    INSERT INTO timeoff_requests VALUES(1,'vacation','2026-10-12','2026-10-13','approved','2026-10-14');
    INSERT INTO timeoff_requests VALUES(2,'vacation','2026-10-03','2026-10-04','approved',NULL);
    INSERT INTO timeoff_requests VALUES(3,'personal','2026-09-29','2026-09-30','approved',NULL);
    ''')
    got={r[1]:r[3] for r in db.execute(query,('2026-10-16','2026-10-31'))}
    assert got=={1:'2026-10-02',2:None,3:None}
    # Once a previous period ends, current vacation becomes historical.
    got={r[1]:r[3] for r in db.execute(query,('2026-11-01','2026-11-15'))}
    assert got[1]=='2026-10-02'
    db.execute("UPDATE timeoff_requests SET cancelled_at=NULL WHERE starts='2026-10-12'")
    got={r[1]:r[3] for r in db.execute(query,('2026-11-01','2026-11-15'))}
    assert got[1]=='2026-10-13'
    assert 'Previous approved vacation (reference only)' in PAY
    assert 'Previous approved vacation day:' in PAY
