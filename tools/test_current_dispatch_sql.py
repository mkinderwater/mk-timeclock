"""Run the mailer's actual candidate-selection SQL against dispatch boundaries."""
import re,sqlite3
from pathlib import Path
root=Path(__file__).resolve().parents[1]
source=(root/'src/payroll_mail.inc').read_text()
query=re.search(r'if\(sqlite3_prepare_v2\(db,"(SELECT p.id,p.starts,p.ends FROM pay_periods.+?)",-1,&periods',source).group(1)
with sqlite3.connect(':memory:') as db:
    db.executescript('''CREATE TABLE pay_periods(id INTEGER,starts TEXT,ends TEXT,status TEXT);
    CREATE TABLE email_queue(event_id TEXT UNIQUE,status TEXT);
    CREATE TABLE payroll_manual_requests(period_id INTEGER PRIMARY KEY);
    CREATE TABLE lite_period_snapshots(period_id INTEGER,revision INTEGER);
    CREATE TABLE payroll_settings(id INTEGER,enabled_since TEXT);
    INSERT INTO payroll_settings VALUES(1,'2026-10-01');
    INSERT INTO pay_periods VALUES(1,'2026-07-01','2026-07-15','closed'),(2,'2026-10-01','2026-10-15','closed'),(3,'2026-10-16','2026-10-31','open');''')
    def eligible(enabled):return [r[0] for r in db.execute(query,('2026-11-02',enabled))]
    assert eligible(1)==[2] and eligible(0)==[]
    db.execute('INSERT INTO payroll_manual_requests VALUES(1)');assert eligible(0)==[1]
    db.execute("INSERT INTO email_queue VALUES('payroll-period:1','sent')");assert eligible(0)==[]
    db.execute('INSERT INTO lite_period_snapshots VALUES(1,2)');assert eligible(0)==[1]
    db.execute("INSERT INTO email_queue VALUES('payroll-period:1:revision:2','pending')");assert eligible(0)==[]
    db.execute("UPDATE payroll_settings SET enabled_since='2026-11-01'");assert eligible(1)==[]
print('PASS: current mailer SQL enforces closed/automatic boundaries, historical manual delivery and revision deduplication')
