"""Exercise fresh/current schema, durable triggers and restart without upgrades."""
import json,os,re,socket,sqlite3,subprocess,tempfile,time,urllib.request
from pathlib import Path
root=Path(__file__).resolve().parents[1]
def boot(path):
    with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    p=subprocess.Popen([str(root/'timeclock-server'),str(path),str(port)],cwd=root,env={**os.environ,'DEVICE_KEY':'fresh-schema-test-key'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        for _ in range(100):
            try:urllib.request.urlopen(f'http://127.0.0.1:{port}/health',timeout=.2).close();break
            except OSError:time.sleep(.04)
        else:raise AssertionError('server failed to initialize')
    finally:p.terminate();p.communicate(timeout=5)
with tempfile.TemporaryDirectory() as tmp:
    path=Path(tmp)/'clock.db';boot(path)
    with sqlite3.connect(path) as db:
        names={r[0] for r in db.execute("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'")}
        assert len(names)==70,names
        assert {'punch_correction_history','period_signoff_history','lite_period_snapshots','card_ownership_history','employee_active_history','employee_attendance_modes','timeoff_requests'}<=names
        def cols(table):return {r[1] for r in db.execute(f'PRAGMA table_info({table})')}
        assert {'requested_days','return_date','acknowledged_at','cancelled_at'}<=cols('timeoff_requests')
        assert {'unconfirmed','reason','leave_fingerprint'}<=cols('period_signoffs')
        assert 'schedule_mode' in cols('schedule_profiles')
        assert 'message_token' in cols('email_queue')
        assert 'cadence' not in cols('payroll_settings')
        before=db.execute("SELECT type,name,sql FROM sqlite_master WHERE name NOT LIKE 'sqlite_%' ORDER BY type,name").fetchall()
        identity=db.execute('SELECT value FROM lite_install_identity').fetchone()[0]
        db.execute("INSERT INTO employees(id,name) VALUES(1,'Schema Manager')")
        db.execute("INSERT INTO employee_roles VALUES(1,'manager')")
        db.execute("INSERT INTO schedule_profiles(employee_id,pay_type) VALUES(1,'salary')")
        assert db.execute('SELECT schedule_mode FROM schedule_profiles').fetchone()[0]=='dated_shifts'
        db.execute("INSERT INTO pay_periods(starts,ends) VALUES('2001-01-01','2001-01-15')")
        pid=db.execute('SELECT last_insert_rowid()').fetchone()[0]
        db.execute("INSERT INTO period_signoffs(period_id,employee_id,role,signed_by,reason,leave_fingerprint) VALUES(?,1,'manager',1,'Original review','original')",(pid,))
        db.execute("UPDATE period_signoffs SET reason='Revised review',leave_fingerprint='revised'")
        assert db.execute('SELECT reason,leave_fingerprint FROM period_signoff_history').fetchall()==[('Original review','original')]
        db.execute('DELETE FROM period_signoffs')
        assert db.execute('SELECT reason FROM period_signoff_history ORDER BY id').fetchall()==[('Original review',),('Revised review',)]
        for i in range(2):db.execute("INSERT INTO email_queue(recipient,subject,body,event_id) VALUES('a@example.test','test','body',?)",(f'test-token-{i}',))
        tokens=db.execute('SELECT message_token FROM email_queue').fetchall()
        assert len({r[0] for r in tokens})==2 and all(len(r[0])==32 for r in tokens)
        db.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,requested_days,return_date,acknowledged_at) VALUES(1,'vacation','2001-01-02','2001-01-03',2,'2001-01-04','2001-01-01T12:00:00Z')")
    boot(path)
    with sqlite3.connect(path) as db:
        assert db.execute("SELECT type,name,sql FROM sqlite_master WHERE name NOT LIKE 'sqlite_%' ORDER BY type,name").fetchall()==before
        assert db.execute('SELECT value FROM lite_install_identity').fetchone()[0]==identity
        assert db.execute('SELECT message_token FROM email_queue').fetchall()==tokens
        assert db.execute('SELECT count(*) FROM period_signoff_history').fetchone()[0]==2
        assert db.execute('PRAGMA integrity_check').fetchone()[0]=='ok'
    # Execute the queue schema actually compiled into the agent, without hardware.
    source=(root/'src/agent.c').read_text()
    schema=re.search(r'if\(!sql\("(PRAGMA journal_mode=WAL;PRAGMA synchronous=FULL;CREATE TABLE[^"\n]+)"\)\)',source).group(1)
    with sqlite3.connect(':memory:') as queue:
        queue.executescript(schema)
        queue.execute("INSERT INTO card_cache(uid,active,attendance_mode) VALUES('ABCDEF01',1,1)")
        try:queue.execute("INSERT INTO card_cache(uid,active,attendance_mode) VALUES('ABCDEF02',1,2)")
        except sqlite3.IntegrityError:pass
        else:raise AssertionError('invalid cache attendance mode accepted')
print('PASS: fresh/current server and agent schemas; restart preserves identity/tokens/audit; update/delete review history and queue mode constraints')
