"""RC3 additive CSV preview and revision delivery SQL contract."""
import re, sqlite3
from pathlib import Path
root=Path(__file__).resolve().parents[1]
ui=(root/'src/server.c').read_text()
assert "id='holidaycsv'" in ui
assert 'toAdd.push([date,name])' in ui and 'for(const [d,n] of toAdd)addRow(d,n)' in ui
assert 'Review the rows and click Save holidays to commit.' in ui
mail=(root/'src/payroll_mail.inc').read_text()
query=re.search(r'if\(sqlite3_prepare_v2\(db,"(SELECT p.id,p.starts,p.ends FROM pay_periods.+?)",-1,&periods',mail).group(1)
db=sqlite3.connect(':memory:')
db.executescript('''
CREATE TABLE pay_periods(id INTEGER PRIMARY KEY,starts TEXT,ends TEXT,status TEXT);
CREATE TABLE payroll_manual_requests(period_id INTEGER PRIMARY KEY);
CREATE TABLE email_queue(event_id TEXT UNIQUE,status TEXT);
CREATE TABLE lite_period_snapshots(period_id INTEGER,revision INTEGER);
CREATE TABLE payroll_settings(id INTEGER,enabled_since TEXT);
INSERT INTO pay_periods VALUES(7,'2026-09-01','2026-09-15','closed');
INSERT INTO email_queue VALUES('payroll-period:7','sent');
INSERT INTO lite_period_snapshots VALUES(7,1);
INSERT INTO lite_period_snapshots VALUES(7,2);
INSERT INTO payroll_manual_requests VALUES(7);
''')
assert db.execute(query,('2026-10-20',0)).fetchall()==[(7,'2026-09-01','2026-09-15')]
db.execute("INSERT INTO email_queue VALUES('payroll-period:7:revision:2','sent')")
assert db.execute(query,('2026-10-20',0)).fetchall()==[]
db.execute('INSERT INTO lite_period_snapshots VALUES(7,3)')
assert db.execute(query,('2026-10-20',0)).fetchall()==[(7,'2026-09-01','2026-09-15')]
print('PASS: holiday import staged; revision 2 queued once; revision 3 can be requested')

cleanup=re.search(r'sqlite3_exec\(db,"(DELETE FROM payroll_manual_requests[^";]+)",NULL,NULL,NULL\)',mail).group(1)
db.execute('DELETE FROM email_queue')
db.execute('DELETE FROM lite_period_snapshots WHERE revision=3')
db.execute("INSERT INTO email_queue VALUES('payroll-period:7','pending')")
assert db.execute(query,('2026-10-20',0)).fetchall()==[]
db.execute(cleanup)
assert db.execute('SELECT period_id FROM payroll_manual_requests').fetchall()==[(7,)]
db.execute("UPDATE email_queue SET status='sent'")
assert db.execute(query,('2026-10-20',0)).fetchall()==[(7,'2026-09-01','2026-09-15')]
db.execute(cleanup)
assert db.execute('SELECT period_id FROM payroll_manual_requests').fetchall()==[(7,)]
db.execute("INSERT INTO email_queue VALUES('payroll-period:7:revision:2','pending')")
db.execute(cleanup)
assert db.execute('SELECT period_id FROM payroll_manual_requests').fetchall()==[]
print('PASS: pending original retains revision 2 request; original sent still retains it; revision 2 queued clears it')
