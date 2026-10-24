"""Fresh-schema and audit-route regression, no older version migrations."""
import os
import sqlite3
import subprocess
import tempfile
import time
import urllib.request
import urllib.error
from pathlib import Path
root=Path(__file__).resolve().parent.parent
server=Path(os.getenv('TIMECLOCK_TEST_SERVER',str(root/'timeclock-server')))
with tempfile.TemporaryDirectory() as d:
    db=Path(d)/'server.db'
    env={**os.environ,'DEVICE_KEY':'test-device-key-1234567890'}
    p=subprocess.Popen([str(server),str(db),'19881'],env=env,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        for _ in range(40):
            if db.exists() and db.stat().st_size: break
            time.sleep(.1)
        con=sqlite3.connect(db)
        assert con.execute('PRAGMA integrity_check').fetchone()[0]=='ok'
        for table in ('schedules','notification_rules','manager_assignments'):
            assert not con.execute('SELECT 1 FROM sqlite_master WHERE name=?',(table,)).fetchone(),table
        assert 'rule_id' not in [r[1] for r in con.execute('PRAGMA table_info(email_queue)')]
        for table in ('admin_export_audit','backup_restore_audit','punch_correction_history','swipe_note_history'):
            assert con.execute('SELECT 1 FROM sqlite_master WHERE name=?',(table,)).fetchone(),table
        con.close()
        try:
            urllib.request.urlopen('http://127.0.0.1:19881/api/v1/admin/audit',timeout=2)
        except urllib.error.HTTPError as err:
            assert err.code in (401,403),err.code
        else: raise AssertionError('audit endpoint accessible without login')
        print('PASS v0.9.116 fresh schema and audit authorization')
    finally:
        p.terminate()
        p.communicate(timeout=4)
