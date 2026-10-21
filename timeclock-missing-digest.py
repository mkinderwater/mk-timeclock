#!/usr/bin/env python3
"""Primary-only daily manager summary of independently checked attendance exceptions.

Each 08:00 digest covers exceptions becoming due since the previous 08:00.
It is independent of employee reminders, including their enabled settings and mail queue.
"""
import datetime as dt
import os
import pathlib
import sqlite3
import sys
from zoneinfo import ZoneInfo

DB = pathlib.Path('/var/lib/timeclock/server.db')
ENV = pathlib.Path('/etc/timeclock/timeclock.env')
SETTLE_MINUTES = 2


def local_datetime(raw, zone):
    try:
        stamp = dt.datetime.fromisoformat(str(raw).strip().replace('Z', '+00:00'))
        if stamp.tzinfo is None:
            stamp = stamp.replace(tzinfo=dt.timezone.utc)
        return stamp.astimezone(zone)
    except (ValueError, TypeError, AttributeError):
        return None


def clock_zone():
    name = os.environ.get('TZ')
    if not name:
        target = str(pathlib.Path('/etc/localtime').resolve())
        prefix = '/usr/share/zoneinfo/'
        if not target.startswith(prefix):
            raise ValueError('Timezone unavailable')
        name = target[len(prefix):]
    return ZoneInfo(name)


def run(dbpath=DB, envpath=ENV, now=None):
    if envpath.is_file():
        roles = [x.split('=', 1)[1].strip().strip('"\'') for x in envpath.read_text().splitlines()
                 if x.startswith('TIMECLOCK_ROLE=')]
        if roles and roles[-1] != 'primary':
            return 0
    if not dbpath.is_file():
        return 0
    tz = clock_zone()
    now = (now or dt.datetime.now(tz)).astimezone(tz)
    # A delayed timer still reports the completed 08:00-to-08:00 interval.
    end_day = now.date() if now.timetz().replace(tzinfo=None) >= dt.time(8) else now.date() - dt.timedelta(days=1)
    window_end = dt.datetime.combine(end_day, dt.time(8), tzinfo=tz)
    window_start = dt.datetime.combine(end_day - dt.timedelta(days=1), dt.time(8), tzinfo=tz)
    conn = sqlite3.connect(dbpath.as_uri() + '?mode=rw', uri=True, timeout=5)
    try:
        conn.execute('PRAGMA busy_timeout=5000')
        conn.execute('PRAGMA synchronous=NORMAL')
        grace_row = conn.execute('SELECT max(grace_minutes) FROM clock_late_settings').fetchone()
        grace = max(5, grace_row[0] or 5)
        skip_row = conn.execute('SELECT skip_alerts FROM holiday_settings WHERE id=1').fetchone()
        skip_holidays = bool(skip_row and skip_row[0])
        alerts = []
        # Earlier shift starts can have overnight end-times inside the report window.
        for n in range(-2, 1):
            date = end_day + dt.timedelta(days=n)
            day = date.isoformat()
            if skip_holidays and conn.execute('SELECT 1 FROM statutory_holidays WHERE day=?', (day,)).fetchone():
                continue
            shifts = conn.execute('''SELECT e.id,e.name,s.start_time,s.end_time,s.work_date
                FROM employees e JOIN schedule_shifts s ON s.employee_id=e.id
                JOIN schedule_profiles p ON p.employee_id=e.id AND p.schedule_mode='dated_shifts'
                WHERE e.active=1 AND e.attendance_mode='punch_required' AND s.work_date=?
                UNION ALL
                SELECT e.id,e.name,p.start_time,p.end_time,?
                FROM employees e JOIN schedule_profiles p ON p.employee_id=e.id AND p.schedule_mode='recurring_weekly'
                WHERE e.active=1 AND e.attendance_mode='punch_required' AND (p.weekday_mask & ?) != 0''',
                (day, day, 1 << date.weekday())).fetchall()
            for employee, name, start, end, _ in shifts:
                # Approved leave is an authorized absence. Cancellation immediately
                # restores normal missing-punch checks without rewriting shifts.
                if conn.execute("SELECT 1 FROM timeoff_requests WHERE employee_id=? AND status='approved' AND cancelled_at IS NULL AND ? BETWEEN starts AND ends LIMIT 1", (employee, day)).fetchone():
                    continue
                if conn.execute("SELECT 1 FROM attendance_absences WHERE employee_id=? AND day=? AND starts='' AND cancelled_at IS NULL AND reason<>'Unexcused absence' LIMIT 1",(employee,day)).fetchone():
                    continue
                try:
                    sh, sm = map(int, start.split(':'))
                    eh, em = map(int, end.split(':'))
                    start_at = dt.datetime.combine(date, dt.time(sh, sm), tzinfo=tz)
                    end_at = dt.datetime.combine(date, dt.time(eh, em), tzinfo=tz)
                    if end_at <= start_at:
                        end_at += dt.timedelta(days=1)
                except (ValueError, TypeError, AttributeError):
                    continue
                due_in = start_at + dt.timedelta(minutes=grace + SETTLE_MINUTES)
                due_out = end_at + dt.timedelta(minutes=grace + SETTLE_MINUTES)
                if not (window_start < due_in <= window_end or window_start < due_out <= window_end):
                    continue
                # Effective records include approved corrections and manual punches.
                records = conn.execute('''SELECT kind,at FROM effective_punches
                    WHERE employee_id=? AND at>=? AND at<=? ORDER BY at,id''',
                    (employee,
                     (start_at - dt.timedelta(hours=12)).astimezone(dt.timezone.utc).isoformat(),
                     (end_at + dt.timedelta(hours=12)).astimezone(dt.timezone.utc).isoformat())).fetchall()
                events = [(kind, stamp) for kind, raw in records
                          if (stamp := local_datetime(raw, tz)) is not None and stamp <= now]
                ins = [stamp for kind, stamp in events
                       if kind == 'IN' and start_at - dt.timedelta(hours=6) <= stamp <= end_at]
                if window_start < due_in <= window_end and not ins:
                    alerts.append((name, 'No IN', day, start, end))
                if window_start < due_out <= window_end and ins:
                    last_in = max(ins)
                    if not any(kind == 'OUT' and last_in <= stamp <= now for kind, stamp in events):
                        alerts.append((name, 'No OUT', day, start, end))
        if not alerts:
            return 0
        managers = conn.execute('''SELECT DISTINCT e.id,e.email FROM employees e
            JOIN employee_roles r ON r.employee_id=e.id AND r.role='manager'
            JOIN user_accounts u ON u.employee_id=e.id AND u.disabled=0
            WHERE e.active=1 AND e.email LIKE '%@%' ''').fetchall()
        if not managers:
            return 0
        lines = [f'Missed punches for shifts due {window_start:%Y-%m-%d %H:%M} to {window_end:%Y-%m-%d %H:%M}:', '']
        for name, kind, shift_day, start, end in sorted(alerts):
            lines.append(f'- {name}: {kind} (shift {shift_day}, {start} to {end})')
        lines.extend(['', 'Correct them under Timesheets > Punches and corrections. Punches uploaded late may already be recorded.'])
        body = '\n'.join(lines)
        subject = f'Timeclock: Missed punches for {window_end.date()}'
        queued = 0
        with conn:
            for mid, email in managers:
                if '\n' in email or '\r' in email:
                    continue
                key = f'missing_digest:{window_end.date()}:manager:{mid}'
                cur = conn.execute('''INSERT OR IGNORE INTO email_queue(recipient,subject,body,status,next_attempt,event_id)
                    VALUES(?,?,?,'pending',strftime('%Y-%m-%dT%H:%M:%fZ','now'),?)''',
                    (email, subject, body, key))
                queued += cur.rowcount
        return queued
    finally:
        conn.close()


if __name__ == '__main__':
    try:
        count = run()
        if count:
            print('Daily attendance digest queued:', count)
    except (sqlite3.Error, OSError, ValueError) as exc:
        print('Daily attendance digest failed:', exc, file=sys.stderr)
        sys.exit(1)
