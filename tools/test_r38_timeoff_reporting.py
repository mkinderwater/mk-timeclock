from pathlib import Path
import re, sqlite3
S=(Path(__file__).resolve().parents[1]/'src/payroll_timesheet.h').read_text()
def test_leave_schedule_and_holidays():
    m=re.search(r'const char \*leave_sql=\s*(.*?);\s*if\(sqlite3_prepare_v2\(db,leave_sql',S,re.S)
    assert m
    query=''.join(re.findall(r'"([^"\n]*)"',m.group(1)))
    db=sqlite3.connect(':memory:')
    db.executescript('''
    CREATE TABLE employees(id INTEGER PRIMARY KEY,name TEXT);
    CREATE TABLE cards(employee_id INTEGER,uid TEXT,active INTEGER);
    CREATE TABLE schedule_profiles(employee_id INTEGER,pay_type TEXT,weekday_mask INTEGER);
    CREATE TABLE schedule_shifts(employee_id INTEGER,work_date TEXT);
    CREATE TABLE statutory_holidays(day TEXT,name TEXT);
    CREATE TABLE effective_punches(employee_id INTEGER,at TEXT);
    CREATE TABLE timeoff_requests(id INTEGER PRIMARY KEY,employee_id INTEGER,kind TEXT,starts TEXT,ends TEXT,status TEXT,cancelled_at TEXT);
    INSERT INTO employees VALUES(1,'Priya'),(2,'Sam'),(3,'Avery'),(4,'Nico');
    INSERT INTO schedule_profiles VALUES(1,'salary',31),(2,'hourly',31),(4,'salary',62);
    INSERT INTO schedule_shifts VALUES(2,'2026-10-10'),(2,'2026-10-13');
    INSERT INTO timeoff_requests VALUES(1,1,'vacation','2026-10-09','2026-10-14','approved',NULL),
      (2,2,'vacation','2026-10-09','2026-10-14','approved',NULL),
      (3,3,'vacation','2026-10-09','2026-10-14','approved',NULL),
      (4,4,'vacation','2026-10-09','2026-10-14','approved',NULL),
      (5,1,'vacation','2026-10-09','2026-10-14','approved','2026-10-15');
    INSERT INTO statutory_holidays VALUES('2026-10-12','Thanksgiving');
    ''')
    rows=list(db.execute(query,('2026-10-09','2026-10-14')))
    dates=lambda eid: {x[2] for x in rows if x[1]==eid}
    assert dates(1)=={'2026-10-09','2026-10-12','2026-10-13','2026-10-14'}
    assert dates(2)=={'2026-10-09','2026-10-10','2026-10-12','2026-10-13','2026-10-14'}
    assert dates(3)=={'2026-10-09','2026-10-12','2026-10-13','2026-10-14'}
    assert dates(4)=={'2026-10-09','2026-10-10','2026-10-13','2026-10-14'}
    assert next(x for x in rows if x[1]==1 and x[2]=='2026-10-12')[8]=='Thanksgiving'
    assert all(x[7]=='scheduled' for x in rows if x[1] in (1,4))
    assert {x[2]:x[7] for x in rows if x[1]==2}=={'2026-10-09':'no shift scheduled','2026-10-10':'scheduled','2026-10-12':'no shift scheduled','2026-10-13':'scheduled','2026-10-14':'no shift scheduled'}
    assert all(x[7]=='no shift scheduled' for x in rows if x[1]==3)

def test_csv_history_alignment_and_heading():
    assert 'sqlite3_str_appendall(csv,",,,,0,,");\n  char historical_status' in S
    assert S.index('if(!count)sqlite3_str_appendall(txt,"No completed')<S.index('Previous approved vacation (reference only)')
    assert 'Stat holiday (not vacation)' in S


def test_leave_email_spacing():
    assert '"  %s (Employee #%d, RFID: %s): %s, %s %s%s%s%s\\n"' not in S or 'is_holiday?"stat holiday, not vacation:"' in S
    assert 'is_holiday?"stat holiday, not vacation:"' in S
    assert 'is_holiday?"stat holiday, not vacation":"approved"' not in S
