# Manager operations

Managers sign in through HTTPS. Initial setup and protected changes use the existing registered-manager-card authorization; authorization is time-limited, consumed once and audited. Account password changes and emergency recovery retain their existing rules. Manager cards authorize changes; attendance cards record punches. UID collision protections do not distinguish physical clones of the same card UID.

Employees and Card status manage employee records, active cards and replacement. Historical card ownership, employee activation and attendance-mode transitions remain necessary for delayed offline uploads.

Schedules supports recurring weekly schedules, dated shifts and no schedule. Expected shifts support lateness and reminders, not wage calculations. Vacation-only mode is distinct from schedule mode. No configured shift must not manufacture a late arrival. Late grace and repeat taps remain per-clock settings.

Pay periods defines the single pay frequency. Payroll report displays it and separately configures payday anchor, lead days, recipient and automatic delivery. Twice-monthly payday anchors use the 1st or 15th; invalid transitions are rejected even when automatic reports are disabled.

Outgoing email configures the shared SMTP server, sender email address, optional sender name, credentials and None/STARTTLS/SSL transport. Backup email supports None only across a trusted network or VPN. Email queue shows delivery and retry state. Sent means the mail server accepted the message, not that a person read it.

Time off requests records audited manager decisions. Timesheet links remain private, expiring bearer links for a single employee/period. The accepted benchmark includes employee approval and manager override behavior; this cleanup does not remove it. Punch explanations remain separate from punch corrections.

HTTPS certificate, Time sync and Clock management retain their existing staged device/service flows. A shared 15-second page-request deadline includes downloading the response body; shorter diagnostic/adoption deadlines are retained. A timeout does not retry a save or stop server-side SQL. Check current state before resubmitting an uncertain change.

Phone tables scroll within their containers. Daily dates remain intact. Calendar-date displays use ISO dates in the reviewed views; detailed timestamps retain their local date/time presentation.

Time sync presents common local timezones as a dropdown, preserving an existing timezone when it is outside the common list. Scanning an enrollment or replacement card displays its UID on the scan button; select the corresponding save/register action to commit it.

Annual vacation allowance is optional on each employee. Use whole or half days; blank is unspecified and zero is explicit. The current annual allowance applies to the selected calendar year. Employee requests display X of Y days used without limiting requests. X counts approved working dates in the year, including future bookings, excluding holidays and cancelled/pending/denied requests. Overlapping dates count once. Annual CSV uses current settings; saved payroll revisions keep captured values.

Payroll report and Timesheet links have independent `Send at (clock local time)` controls. Zero payroll lead days means payday; zero timesheet days means the period-end day. Timesheet links at 08:00 and payroll at 15:00 on a common processing date can be configured with the existing day offsets. Payroll must still be closed before its automatic report can queue. The server checks links roughly once a minute; the mailer checks payroll on its normal loop. Send time is a queue cutoff, not a guarantee of SMTP acceptance or receipt at that instant.

Times default to 00:00 on update to preserve previous scheduling. Missed sends catch up within the existing review (14-day) and payroll (45-day) windows and enable-date rules. A manager closing payroll after its cutoff allows that report to queue on the next mailer pass. Manual sends bypass scheduled cutoff times. Existing event identities prevent duplicate automatic notices after restarts or a repeated daylight-saving hour. A skipped local time runs when the local clock first reaches a later time.
