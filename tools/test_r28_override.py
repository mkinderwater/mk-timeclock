import sqlite3
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

def test_override_query_and_late_confirmation():
    source=(ROOT/'src/payroll_timesheet.h').read_text()
    assert 'SELECT s.reason,e.name' in source
    db=sqlite3.connect(':memory:')
    db.executescript('''CREATE TABLE employees(id INTEGER PRIMARY KEY,name TEXT,email TEXT);
    CREATE TABLE pay_periods(id INTEGER PRIMARY KEY,starts TEXT,ends TEXT,reason TEXT);
    CREATE TABLE period_signoffs(period_id INTEGER,employee_id INTEGER,role TEXT,reason TEXT,unconfirmed INTEGER,punch_count INTEGER,approved_seconds INTEGER,signed_at TEXT DEFAULT '');
    CREATE TABLE period_approval_audit(id INTEGER PRIMARY KEY,period_id INTEGER,employee_id INTEGER,action TEXT,reason TEXT,recorded_at TEXT);
    CREATE TABLE cards(uid TEXT,employee_id INTEGER,active INTEGER);''')
    db.execute("INSERT INTO employees VALUES(1,'Alex',NULL)")
    db.execute("INSERT INTO pay_periods VALUES(2,'2026-10-01','2026-10-15','Closure')")
    db.execute("INSERT INTO period_signoffs(period_id,employee_id,role,reason,unconfirmed,punch_count,approved_seconds) VALUES(2,1,'manager','Employee unavailable',1,4,57600)")
    sql=source.split('sqlite3_prepare_v2(db,"',1)[1].split('"',1)[0]
    row=db.execute(sql,('2026-10-01','2026-10-15',1)).fetchone()
    assert row==('Employee unavailable','Alex',None,0,'')
    db.execute("INSERT INTO period_signoffs(period_id,employee_id,role,reason,unconfirmed,punch_count,approved_seconds) VALUES(2,1,'employee','',0,4,57600)")
    assert db.execute(sql,('2026-10-01','2026-10-15',1)).fetchone()[3]==1
    db.execute("UPDATE period_signoffs SET unconfirmed=0 WHERE period_id=2 AND employee_id=1 AND role='manager' AND punch_count=4 AND approved_seconds=57600")
    assert db.execute("SELECT unconfirmed FROM period_signoffs WHERE role='manager'").fetchone()[0]==0

def test_no_address_not_blocked():
    s=(ROOT/'src/reconciliation.inc').read_text()
    assert 'Employee email address required before approving' not in s
    assert 'employee_not_notified' in s
    assert 'else if(ok)bad=1' in s
    assert "tc.err(message,Error(" in s
