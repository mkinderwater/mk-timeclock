#!/usr/bin/env python3
# Primary-only, idempotent, schedule-driven Missing IN/OUT notifications.
import datetime as dt
import pathlib
import sqlite3
import sys
import os
from zoneinfo import ZoneInfo

DB = pathlib.Path('/var/lib/timeclock/server.db')
ENV = pathlib.Path('/etc/timeclock/timeclock.env')
SETTLE_MINUTES = 2
WINDOW_MINUTES = 15

def local_datetime(raw, timezone):
    try:
        d = dt.datetime.fromisoformat(raw.strip().replace('Z', '+00:00'))
        if d.tzinfo is None:
            d = d.replace(tzinfo=dt.timezone.utc)
        return d.astimezone(timezone)
    except (ValueError, TypeError, AttributeError):
        return None

def render(value, args):
    for k,v in args.items():
        value=value.replace('{{'+k+'}}', str(v))
    return value

def run(dbpath=DB, envpath=ENV, now=None):
    if envpath.is_file():
        roles=[line.split('=',1)[1].strip().strip('"\'') for line in envpath.read_text().splitlines() if line.startswith('TIMECLOCK_ROLE=')]
        if roles and roles[-1] != 'primary': return 0
    if not dbpath.is_file(): return 0
    # ZoneInfo uses daylight-saving transitions, unlike datetime.now().astimezone()
    # which returns a fixed-offset tzinfo. Respect TZ for test deployments.
    tz_name=os.environ.get("TZ", "")
    if not tz_name:
        target=str(pathlib.Path("/etc/localtime").resolve())
        prefix="/usr/share/zoneinfo/"
        if not target.startswith(prefix):
            raise RuntimeError("/etc/localtime does not resolve to a named IANA zone")
        tz_name=target[len(prefix):]
    timezone=ZoneInfo(tz_name)
    now=now or dt.datetime.now(timezone)
    if now.tzinfo is None: raise ValueError('Now must have timezone')
    tz=timezone
    now=now.astimezone(tz)
    # Look back one day to support overnight shifts.
    days=[now.date()-dt.timedelta(days=1),now.date()]
    conn=sqlite3.connect(dbpath.as_uri()+'?mode=rw',uri=True,timeout=5)
    try:
        conn.execute('PRAGMA busy_timeout=5000')
        conn.execute('PRAGMA synchronous=NORMAL')
        templates=dict((name,(subject,body)) for name,subject,body in conn.execute("SELECT id,subject,body FROM email_templates WHERE id IN ('missing_in_employee','missing_out_employee') AND enabled=1"))
        # Manager missing-OUT warnings do not depend on employee email settings.
        grace_row=conn.execute('SELECT max(grace_minutes) FROM clock_late_settings').fetchone()
        grace=max(5,grace_row[0] or 5)
        skip_holidays=conn.execute('SELECT skip_alerts FROM holiday_settings WHERE id=1').fetchone()
        skip_holidays=bool(skip_holidays and skip_holidays[0])
        n=0
        for date in days:
            day=date.isoformat()
            if skip_holidays and conn.execute('SELECT 1 FROM statutory_holidays WHERE day=?',(day,)).fetchone():
                continue
            # Explicit hourly shifts and weekly salary schedule, without guessing unscheduled days.
            shifts=conn.execute('''SELECT e.id,e.name,e.email,s.start_time,s.end_time,s.work_date
              FROM employees e JOIN schedule_shifts s ON s.employee_id=e.id
              JOIN schedule_profiles p ON p.employee_id=e.id AND p.schedule_mode='dated_shifts'
              WHERE e.active=1 AND e.attendance_mode='punch_required' AND s.work_date=?
              UNION ALL
              SELECT e.id,e.name,e.email,p.start_time,p.end_time,?
              FROM employees e JOIN schedule_profiles p ON p.employee_id=e.id AND p.schedule_mode='recurring_weekly'
              WHERE e.active=1 AND e.attendance_mode='punch_required' AND (p.weekday_mask & ?) != 0''',
              (day,day,1<<date.weekday())).fetchall()
            for employee,name,email,start,end,day in shifts:
                # Approved time off is an excused absence, not a missing IN punch.
                if conn.execute("SELECT 1 FROM timeoff_requests WHERE employee_id=? AND status='approved' AND starts<=? AND ends>=? LIMIT 1",(employee,day,day)).fetchone():
                    continue
                if not start or not end: continue
                if conn.execute("SELECT 1 FROM attendance_absences WHERE employee_id=? AND day=? AND starts='' AND cancelled_at IS NULL AND reason<>'Unexcused absence' LIMIT 1",(employee,day)).fetchone():
                    continue
                try:
                    sh,sm=map(int,start.split(':')); eh,em=map(int,end.split(':'))
                    start_at=dt.datetime.combine(date,dt.time(sh,sm),tzinfo=tz)
                    end_at=dt.datetime.combine(date,dt.time(eh,em),tzinfo=tz)
                    if end_at <= start_at: end_at+=dt.timedelta(days=1)
                except (ValueError,TypeError): continue
                # Use committed attendance records, including late uploads received so far.
                rows=conn.execute('''SELECT a.kind,s.swiped_at FROM attendance_events a JOIN swipes s ON a.event_id=s.event_id
                  WHERE a.employee_id=? AND s.swiped_at>=? ORDER BY s.swiped_at''',
                  (employee,(start_at-dt.timedelta(hours=12)).astimezone(dt.timezone.utc).isoformat())).fetchall()
                events=[]
                for kind,raw in rows:
                    stamp=local_datetime(raw,tz)
                    if stamp and start_at-dt.timedelta(hours=12)<=stamp<=end_at+dt.timedelta(hours=12):
                        events.append((kind,stamp))
                # IN must be recorded for this shift; unrelated prior-day swipes do not qualify.
                in_events=[x for kind,x in events if kind=='IN' and start_at-dt.timedelta(hours=6)<=x<=end_at]
                for mode,scheduled_at,label in [('missing_in_employee',start_at,'start'),('missing_out_employee',end_at,'end')]:
                    if mode not in templates and mode != 'missing_out_employee': continue
                    due=scheduled_at+dt.timedelta(minutes=grace+SETTLE_MINUTES)
                    if not due<now<=due+dt.timedelta(minutes=WINDOW_MINUTES): continue
                    if mode=='missing_in_employee':
                        # Suppress if any IN near start, including early punches.
                        if in_events: continue
                    else:
                        if not in_events: continue
                        # A later OUT completes the attendance pair. Never remind when it exists.
                        latest_in=max(in_events)
                        if any(kind=='OUT' and latest_in<=at<=now for kind,at in events): continue
                    if mode == 'missing_out_employee':
                        # One prompt notice for the manager who can correct the punch.
                        subject=f'Missing OUT: {name}, {day}'
                        message=(f'{name} has no OUT recorded for the shift ending {day} at {end}.\n'
                                 'Check Timesheets > Punches and corrections. Late uploads may still arrive.')
                        for (address,) in conn.execute("SELECT DISTINCT e.email FROM employees e JOIN employee_roles r ON r.employee_id=e.id AND r.role='manager' WHERE e.active=1 AND e.email LIKE '%@%'"):
                            if not address or any(c in address for c in '\r\n'):continue
                            key=f'missing-out-manager:{employee}:{day}:{start}:{end}:{address}'
                            with conn:
                                cur=conn.execute("INSERT OR IGNORE INTO email_queue(recipient,subject,body,status,next_attempt,event_id) VALUES(?,?,?,'pending',strftime('%Y-%m-%dT%H:%M:%fZ','now'),?)",(address,subject,message,key))
                                n+=cur.rowcount
                    if mode in templates and email and '@' in email and not any(c in email for c in '\r\n'):
                        args={'employee_name':name,'attendance_date':day,'scheduled_start':start,'scheduled_end':end}
                        subject,body=templates[mode]
                        key=f'{mode}:{employee}:{day}:{start}:{end}'
                        with conn:
                            cursor=conn.execute("INSERT OR IGNORE INTO email_queue(recipient,subject,body,status,next_attempt,event_id) VALUES(?,?,?,'pending',strftime('%Y-%m-%dT%H:%M:%fZ','now'),?)",(email,render(subject,args),render(body,args),key))
                            n+=cursor.rowcount
        return n
    finally: conn.close()

if __name__=='__main__':
    try:
        count=run()
        if count: print('Scheduled attendance reminders queued:',count)
    except (sqlite3.Error,OSError,ValueError) as exc:
        print('Attendance reminders failed:',exc,file=sys.stderr)
        sys.exit(1)
