from pathlib import Path
import sqlite3, subprocess, tempfile, re
root=Path(__file__).resolve().parents[1]
s=(root/'src/server.c').read_text()
assert s.count("event_id=?1 OR event_id LIKE ?1 || ':%'")==2
assert 'welcome:%ld%%' not in s[s.index('static void welcome_resend('):s.index('static void management_save(')]
with sqlite3.connect(':memory:') as db:
    db.execute('CREATE TABLE email_queue(event_id TEXT,status TEXT)')
    q="SELECT 1 FROM email_queue WHERE (event_id=?1 OR event_id LIKE ?1 || ':%') AND status='pending' LIMIT 1"
    db.execute("INSERT INTO email_queue VALUES ('welcome:50:resend:abc','pending')")
    assert db.execute(q,('welcome:5',)).fetchone() is None
    assert db.execute(q,('welcome:50',)).fetchone() is not None
    db.execute("INSERT INTO email_queue VALUES ('welcome:5','pending')")
    assert db.execute(q,('welcome:5',)).fetchone() is not None
    db.execute("UPDATE email_queue SET status='sent' WHERE event_id='welcome:5'")
    assert db.execute(q,('welcome:5',)).fetchone() is None
    db.execute("INSERT INTO email_queue VALUES ('welcome:5:resend:xyz','pending')")
    assert db.execute(q,('welcome:5',)).fetchone() is not None
script=(root/'timeclock-tls-apply.sh').read_text()
assert '/^-----BEGIN CERTIFICATE-----[ \\t]*$/' in script
assert '/^-----END CERTIFICATE-----[ \\t]*$/' in script
assert "'^-----BEGIN CERTIFICATE-----[[:blank:]]*$'" in script
assert re.search(r'UI_VERSION \?= 1\.9\.5-RC1-welcome-tls2', (root/'Makefile').read_text())
assert '1.9.5-RC1-welcome-tls2' in (root/'src/ui.inc').read_text()
print('PASS: scoped welcome pending; whitespace PEM markers; CSS version')
