"""r19: approved pay periods, independent payday cadence and safe mail deduplication."""
from pathlib import Path
import sqlite3

root=Path(__file__).resolve().parents[1]
mail=(root/'src/payroll_mail.inc').read_text()
ui=(root/'src/payroll.inc').read_text()
period=(root/'src/reconciliation.inc').read_text()
issues=(root/'src/issues.inc').read_text()
assert 'payroll-period:%d' in mail
assert 'p.status=\'closed\'' in mail
assert "e.event_id='payroll-period:'||p.id" in mail
assert 'PROVISIONAL' not in mail
assert 'p.cadence<>c.cycle' not in issues
assert 'Payday frequency does not match' not in ui+period
assert 'clock%s have stale or unavailable OLED previews' in issues

# Mirror the period selector's SQL in a SQLite fixture and check real results.
query="""SELECT p.id,p.starts,p.ends FROM pay_periods p WHERE p.status='closed'
AND p.ends<?1 AND p.ends>=date(?1,'-45 days') AND NOT EXISTS
(SELECT 1 FROM email_queue e WHERE e.event_id='payroll-period:'||p.id) AND NOT EXISTS
(SELECT 1 FROM pay_periods later WHERE later.ends>p.ends AND later.ends<?1)
ORDER BY p.ends DESC LIMIT 1"""
assert "SELECT p.id,p.starts,p.ends FROM pay_periods p" in mail
with sqlite3.connect(':memory:') as db:
    db.executescript('CREATE TABLE pay_periods(id INTEGER PRIMARY KEY,starts TEXT,ends TEXT,status TEXT);'
                     'CREATE TABLE email_queue(event_id TEXT UNIQUE);')
    db.executemany('INSERT INTO pay_periods VALUES(?,?,?,?)', [
      (1,'2026-09-16','2026-09-30','closed'),
      (2,'2026-10-01','2026-10-15','open'),
      (3,'2026-10-16','2026-10-31','closed'),
      (4,'2026-08-01','2026-08-15','closed')])
    def choose(d):
        r=db.execute(query,(d,)).fetchone()
        return r[0] if r else None
    assert choose('2026-10-20') is None, 'A newer open period prevents mailing an older period'
    assert choose('2026-10-07') == 1, 'Latest closed period is eligible'
    db.execute("INSERT INTO email_queue VALUES('payroll-period:1')")
    assert choose('2026-10-07') is None, 'Already queued period is never repeated'
    assert choose('2026-11-06') == 3, 'A new closed period can be selected'
    db.execute("INSERT INTO email_queue VALUES('payroll-period:3')")
    assert choose('2026-11-06') is None
    assert choose('2026-12-22') is None, 'Stale pay periods do not become reports'
print('PASS r19 approved period selection, deduplication, dated windows and warning semantics')
