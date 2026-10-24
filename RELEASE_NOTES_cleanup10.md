# RC3 cleanup10: Vacation workflow

Based on the supplied cleanup8 archive. Replace the general time-off form with a Vacation page and vacation-only submissions.

- Employees use Request vacation: dates, calculated working days, return date and optional notes. No leave-type selector or editable day total.
- Managers approve/deny requests or Record approved vacation for verbal/email approvals and historical dates, including before the clock was installed. Inactive employees can be selected for historical recording.
- Manager recording saves as Approved, audits the manager and time, and sends no entry email. The annual allowance remains informational.
- Sickness and other missed attendance remain under Punches and corrections > Recorded absences, with links explaining the distinction.
- Existing non-vacation records keep their original type in history. New personal/unpaid/other submissions are rejected server-side. Existing private links and routes remain valid.
- Existing tables and audit records are reused. No new tables, columns, indexes or triggers. Old default email wording is refreshed; custom subjects/bodies and enabled settings remain intact.
- Historical vacation affects annual totals and payroll context, flags affected exports for a new revision, and never overwrites saved revisions.
- Fix a repeated-decision crash caused by finalizing the same SQLite statement twice. Repeating approval now returns an error without crashing or queuing another notification.

## Validation

Server and mailer compile cleanly with -Werror. Maintained suite including browsers: 45/47. The vacation workflow, old record retention, API restrictions, audit rollback, notifications, duplicate decisions, template upgrades, schema comparison, annual totals and frozen exports pass. Forms tested at 320/390/1280px; CSS lint and preflight pass; 99 scripts from 35 served pages parse.

Two existing tests, test_current_closed_period_upload_http.py and test_rc3_closed_period_attendance.py, still fail with missing overnight private-review detail. Both reproduce identically on unchanged cleanup8. See validation/cleanup10-suite.json. No physical BPI/ARM validation; agent not rebuilt here.

## Upgrade

Back up, extract this source package, enter its directory, then rerun sudo ./install-master.sh for the Primary or sudo ./install-slave.sh for a Secondary. Do not uninstall or reset the database.
