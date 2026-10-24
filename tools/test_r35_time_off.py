"""r35 source and live-schema safety checks."""
import os
import socket
import sqlite3
import subprocess
import tempfile
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def test_timeoff_routes_and_schema():
    source = (ROOT / "src/server.c").read_text()
    inc = (ROOT / "src/time_off.inc").read_text()
    assert 'timeoff_schema()' in source
    assert 'timeoff_route(fd,method,path,buf)' in source
    assert 'timeoff_manager_decide(fd,session_user(buf)' in source
    assert 'status=\'pending\'' in inc
    assert 'AND status=\'pending\'' in inc
    assert 'timeoff_employee_history' in inc
    assert 'timeoff_audit' in inc
    assert 'timeoff_email(' in inc


def test_schema_and_public_portal():
    exe = ROOT / "timeclock-server"
    if not exe.exists():
        subprocess.run(["make", "timeclock-server"], cwd=ROOT, check=True)
    with tempfile.TemporaryDirectory() as d:
        db = Path(d) / "clock.db"
        s = socket.socket()
        s.bind(("127.0.0.1", 0))
        port = s.getsockname()[1]
        s.close()
        env = dict(os.environ, DEVICE_KEY="0123456789abcdef0123456789abcdef")
        proc = subprocess.Popen([str(exe), str(db), str(port)], cwd=ROOT, env=env,
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            for _ in range(50):
                try:
                    with urllib.request.urlopen(f"http://127.0.0.1:{port}/time-off", timeout=.5) as r:
                        assert b"Send private link" in r.read()
                    break
                except OSError:
                    time.sleep(.05)
            else:
                raise AssertionError("Server did not start")
            with sqlite3.connect(db) as c:
                names = {r[0] for r in c.execute("select name from sqlite_master where type='table'")}
                assert {"timeoff_links", "timeoff_requests", "timeoff_audit"} <= names
                c.execute("insert into employees(name,email) values('Sam','sam@example.test')")
                eid = c.execute("select id from employees where name='Sam'").fetchone()[0]
                c.execute("insert into timeoff_requests(employee_id,kind,starts,ends) values(?,'vacation','2026-11-02','2026-11-03')", (eid,))
                c.commit()
            with urllib.request.urlopen(f"http://127.0.0.1:{port}/time-off/link", data=b"email=unknown%40example.test", timeout=2) as r:
                assert b"If this email is on file" in r.read()
        finally:
            proc.terminate()
            proc.wait(timeout=4)
