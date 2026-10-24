#!/usr/bin/env python3
"""Focused contract for pending time-off navigation counts."""
import sqlite3
from pathlib import Path
root = Path(__file__).resolve().parents[1]
issues=(root/'src/issues.inc').read_text()
ui=(root/'src/ui.inc').read_text()
timeoff=(root/'src/time_off.inc').read_text()
assert "FROM timeoff_requests WHERE status='pending' AND cancelled_at IS NULL" in issues
assert 'issue_item(o,&n,"time-off",c,t)' in issues
assert "data-key='time-off'" in ui
assert "timeclock:issues-changed" in ui and "refreshIssues()" in ui
assert "window.dispatchEvent(new Event('timeclock:issues-changed'))" in timeoff
c=sqlite3.connect(':memory:')
c.executescript("CREATE TABLE timeoff_requests(status TEXT,cancelled_at TEXT); INSERT INTO timeoff_requests VALUES('pending',NULL),('pending',NULL),('approved',NULL),('denied',NULL),('pending','2026-01-01');")
q="SELECT count(*) FROM timeoff_requests WHERE status='pending' AND cancelled_at IS NULL"
assert c.execute(q).fetchone()[0]==2
c.execute("UPDATE timeoff_requests SET status='approved' WHERE rowid=1")
assert c.execute(q).fetchone()[0]==1
c.execute("UPDATE timeoff_requests SET status='denied' WHERE rowid=2")
assert c.execute(q).fetchone()[0]==0
print('PASS: pending time-off count 2 -> 1 -> 0 and nav refresh contract')
