"""Approved leave must cease to be an attendance exemption when cancelled."""
import os, socket, sqlite3, subprocess, tempfile, time
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]

def test_upgrade_approved_and_cancelled_leave():
    subprocess.run(['make', 'timeclock-server'], cwd=ROOT, check=True, capture_output=True)
    with tempfile.TemporaryDirectory() as d:
        path = Path(d) / 'clock.db'
        sock=socket.socket();sock.bind(('127.0.0.1',0));port=sock.getsockname()[1];sock.close()
        proc=subprocess.Popen([str(ROOT/'timeclock-server'),str(path),str(port)],cwd=ROOT,env={**os.environ,'DEVICE_KEY':'0123456789abcdef0123456789abcdef'},stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        try:
            for _ in range(80):
                if path.exists():
                    try:
                        with sqlite3.connect(path) as db:
                            db.execute('SELECT 1 FROM timeoff_requests')
                        break
                    except sqlite3.Error: pass
                time.sleep(.05)
            else: raise AssertionError('server did not initialize')
        finally: proc.terminate();proc.wait(timeout=5)
        with sqlite3.connect(path) as db:
            assert 'cancelled_at' in [x[1] for x in db.execute('pragma table_info(timeoff_requests)')]
            db.execute("INSERT INTO employees(name,email) VALUES('Pat','pat@example.org')")
            emp=db.execute('SELECT last_insert_rowid()').fetchone()[0]
            db.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status) VALUES(?,'vacation','2026-11-02','2026-11-03','approved')",(emp,))
            request=db.execute('SELECT last_insert_rowid()').fetchone()[0]
            q="SELECT 1 FROM timeoff_requests WHERE employee_id=? AND status='approved' AND cancelled_at IS NULL AND ? BETWEEN starts AND ends"
            assert db.execute(q,(emp,'2026-11-02')).fetchone()
            db.execute("UPDATE timeoff_requests SET cancelled_at=strftime('%Y-%m-%dT%H:%M:%fZ','now') WHERE id=?",(request,))
            assert not db.execute(q,(emp,'2026-11-02')).fetchone()
            assert db.execute("SELECT status FROM timeoff_requests WHERE id=?",(request,)).fetchone()[0]=='approved'

def test_integration_sources():
    payroll=(ROOT/'src/payroll_timesheet.h').read_text()
    attendance=(ROOT/'src/attendance.inc').read_text()
    digest=(ROOT/'timeclock-missing-digest.py').read_text()
    portal=(ROOT/'src/time_off.inc').read_text()
    assert 'Approved time off (not attendance hours)' in payroll
    assert 'CHECK: punches also recorded' in payroll
    assert 'cancelled_at IS NULL' in payroll
    assert 'cancelled_at IS NULL' in attendance
    assert 'cancelled_at IS NULL' in digest
    assert 'Cancel approval' in portal
