#!/usr/bin/env python3
"""Verify audit descriptions and absence indicator against the source queries."""
import pathlib, re, sqlite3
root=pathlib.Path(__file__).resolve().parents[1]
source=(root/'src/server.c').read_text()
ui=(root/'src/reconciliation.inc').read_text()
def query(section):
    match=re.search(r'\{"'+re.escape(section)+r'","[^"]+","([^"]+)"\}',source)
    assert match,section
    return match.group(1)
con=sqlite3.connect(':memory:')
con.executescript('''
CREATE TABLE employees(id INTEGER PRIMARY KEY,name TEXT);
CREATE TABLE timeoff_requests(id INTEGER PRIMARY KEY,employee_id INTEGER,kind TEXT,starts TEXT,ends TEXT);
CREATE TABLE timeoff_audit(id INTEGER PRIMARY KEY,at TEXT,actor_id INTEGER,action TEXT,request_id INTEGER);
CREATE TABLE attendance_absences(id INTEGER PRIMARY KEY,employee_id INTEGER,day TEXT);
CREATE TABLE attendance_absence_audit(id INTEGER PRIMARY KEY,recorded_at TEXT,manager_id INTEGER,action TEXT,absence_id INTEGER,reason TEXT);
INSERT INTO employees VALUES(1,'Karen Manager'),(2,'Sarah Chen'),(3,'David Miller');
INSERT INTO timeoff_requests VALUES(3,2,'vacation','2026-11-02','2026-11-06');
INSERT INTO timeoff_audit VALUES(1,'2026-10-19',1,'approved',3);
INSERT INTO attendance_absences VALUES(2,3,'2026-10-16');
INSERT INTO attendance_absence_audit VALUES(1,'2026-10-19',1,'create',2,'Sick');
''')
a=con.execute(query('timeoff_audit')).fetchone()[1]
b=con.execute(query('attendance_absence_audit')).fetchone()[1]
assert a=='Karen Manager approved Sarah Chen vacation, 2026-11-02 to 2026-11-06',a
assert b=='Karen Manager recorded absence for David Miller on 2026-10-16 (Sick)',b
assert "tc.flag('Punches recorded','check')" in ui
assert 'Check: punches recorded' not in ui
print('RC1 audit wording checks PASS')
