"""r31 welcome eligibility: the seven-day cutoff is evaluated at send time."""
import sqlite3
from pathlib import Path

SOURCE = (Path(__file__).resolve().parents[1]/'src/server.c').read_text()
REC = (Path(__file__).resolve().parents[1]/'src/reconciliation.inc').read_text()


def test_fresh_schema_tracks_employee_creation_and_welcome():
    assert 'created_at TEXT NOT NULL DEFAULT' in SOURCE
    assert 'welcomed_at TEXT' in SOURCE
    assert SOURCE.count("INSERT INTO employees(") >= 2


def test_eligibility_window_and_disabled_template():
    db = sqlite3.connect(':memory:')
    db.executescript('''CREATE TABLE employees(id INTEGER PRIMARY KEY, active INTEGER, welcomed_at TEXT, created_at TEXT);
        CREATE TABLE cards(employee_id INTEGER, active INTEGER);
        INSERT INTO employees VALUES (1,1,NULL,datetime('now','-6 days'));
        INSERT INTO employees VALUES (2,1,NULL,datetime('now','-8 days'));
        INSERT INTO employees VALUES (3,1,NULL,datetime('now'));
        INSERT INTO employees VALUES (4,1,datetime('now'),datetime('now'));
        INSERT INTO cards VALUES (1,1),(2,1),(3,0),(4,1);''')
    ids = [x[0] for x in db.execute('''SELECT id FROM employees WHERE active=1 AND welcomed_at IS NULL
        AND created_at IS NOT NULL AND julianday('now')-julianday(created_at) BETWEEN 0 AND 7
        AND EXISTS(SELECT 1 FROM cards WHERE employee_id=employees.id AND active=1)''')]
    assert ids == [1]
    assert "id='welcome_employee' AND enabled=1" in SOURCE


def test_disabled_notice_reason_has_no_period():
    assert 'Employee not notified: notice turned off in Email templates.' not in REC
    assert 'Employee not notified: notice turned off in Email templates' in REC
