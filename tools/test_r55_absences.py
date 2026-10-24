from pathlib import Path
import sqlite3

ROOT=Path(__file__).resolve().parents[1]

def test_absence_is_not_a_punch_and_is_audited():
    source=(ROOT/'src/reconciliation.inc').read_text()
    assert 'CREATE TABLE IF NOT EXISTS attendance_absences' in source
    assert 'CREATE TABLE IF NOT EXISTS attendance_absence_audit' in source
    assert 'INSERT INTO attendance_absences' in source
    assert 'INSERT INTO attendance_absence_audit' in source
    assert 'INSERT INTO punch_corrections' in source
    assert 'attendance_absences' not in source[source.index('static void rec_apply('):source.index('static void rec_effective_export(')]

def test_absence_integrations():
    for name in ('src/attendance.inc','timeclock-attendance-reminders.py','timeclock-missing-digest.py','src/payroll_timesheet.h'):
        assert 'attendance_absences' in (ROOT/name).read_text()
    s=(ROOT/'src/payroll_timesheet.h').read_text()
    assert 'Recorded absence:' in s and '0.00' in s

def test_active_and_cancelled_absence_selection():
    c=sqlite3.connect(':memory:')
    c.executescript('CREATE TABLE attendance_absences(employee_id INTEGER,day TEXT,starts TEXT,reason TEXT,cancelled_at TEXT);'
                    "INSERT INTO attendance_absences VALUES(1,'2026-10-19','','Sick',NULL);"
                    "INSERT INTO attendance_absences VALUES(2,'2026-10-19','','Sick','2026-10-18');"
                    "INSERT INTO attendance_absences VALUES(3,'2026-10-19','','Unexcused absence',NULL);"
                    "INSERT INTO attendance_absences VALUES(4,'2026-10-19','09:00','Medical appointment',NULL);")
    q="SELECT employee_id FROM attendance_absences WHERE day=? AND starts='' AND cancelled_at IS NULL AND reason<>'Unexcused absence'"
    assert c.execute(q,('2026-10-19',)).fetchall()==[(1,)]
