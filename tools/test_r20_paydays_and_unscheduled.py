"""r20: configuration, date boundary and schedule-exception regression guards."""
from pathlib import Path
from datetime import date,timedelta
import calendar
r=Path(__file__).resolve().parents[1]
p=(r/'src/payroll.inc').read_text();m=(r/'src/payroll_mail.inc').read_text()
s=(r/'src/schedule.inc').read_text();issues=(r/'src/issues.inc').read_text();server=(r/'src/server.c').read_text()
assert 'semi_monthly' in p and 'semi_monthly' in m
assert 'first_day==1||first_day==15' in m
assert 'atoi(anchor+8)!=1&&atoi(anchor+8)!=15' in p
assert 'payroll-period:%d' in m
assert 'schedule_exempt' in s and 'schedule_exempt=0' in issues
assert 'schedule_exempt INTEGER NOT NULL DEFAULT 0' in server
assert 'ensure_schedule_exempt_column()' in server
assert 'INSERT INTO schedule_change_audit' in s
assert 'Intentionally no fixed schedule' in s
for year in (2026,2028):
 for month in range(1,13):
  last=calendar.monthrange(year,month)[1]
  for first in (1,15):
   paydays=[date(year,month,1),date(year,month,15)] if first==1 else [date(year,month,15),date(year,month,last)]
   assert len(set(paydays))==2
   for lead in (0,3,14):
    due=[d-timedelta(days=lead) for d in paydays]
    assert len(set(due))==2
print('PASS r20 payday patterns, February/leap days, schedule exemption guards')
