# Manager operations

Managers sign in through HTTPS. Initial setup and protected changes use the existing registered-manager-card authorization; authorization is time-limited, consumed once and audited. Account password changes and emergency recovery retain their existing rules. Manager cards authorize changes; attendance cards record punches. UID collision protections do not distinguish physical clones of the same card UID.

Employees and Card status manage employee records, active cards and replacement. Historical card ownership, employee activation and attendance-mode transitions remain necessary for delayed offline uploads.

Schedules supports recurring weekly schedules, dated shifts and no schedule. Expected shifts support lateness and reminders, not wage calculations. Vacation-only mode is distinct from schedule mode. No configured shift must not manufacture a late arrival. Late grace and repeat taps remain per-clock settings. An arrival is late only when it exceeds grace, using exact seconds. Stored late minutes then count from the scheduled start, rounded up; zero means on time. Later grace changes do not reclassify recorded punches. Late emails show the actual scheduled start, not a time reconstructed from rounded late minutes.

Pay periods defines the single pay frequency. Payroll report displays it and separately configures payday anchor, lead days, recipient and automatic delivery. Twice-monthly payday anchors use the 1st or 15th; invalid transitions are rejected even when automatic reports are disabled.

A completed shift belongs entirely to the pay period containing its IN date in the clock’s local timezone. An OUT after midnight or after the period ends completes the original shift; hours are not split and no midnight punches are added. For example, September 15 at 22:00 through September 16 at 06:00 contributes all eight hours to September 1–15 and none to September 16–30. Elapsed duration uses the actual timestamps, including daylight-saving changes. A missing OUT remains an exception; the system does not invent an end time.

Outgoing email configures the shared SMTP server, sender email address, optional sender name, credentials and None/STARTTLS/SSL transport. Backup email supports None only across a trusted network or VPN. Email queue shows delivery and retry state. Sent means the mail server accepted the message, not that a person read it.

Vacation records audited manager decisions. Timesheet links remain private, expiring bearer links for a single employee/period. The accepted benchmark includes employee approval and manager override behavior; this cleanup does not remove it. Punch explanations remain separate from punch corrections.

HTTPS certificate, Time sync and Clock management retain their existing staged device/service flows. A shared 15-second page-request deadline includes downloading the response body; shorter diagnostic/adoption deadlines are retained. A timeout does not retry a save or stop server-side SQL. Check current state before resubmitting an uncertain change.

Phone tables scroll within their containers. Daily dates remain intact. Calendar-date displays use ISO dates in the reviewed views; detailed timestamps retain their local date/time presentation.

Time sync presents common local timezones as a dropdown, preserving an existing timezone when it is outside the common list. Scanning an enrollment or replacement card displays its UID on the scan button; select the corresponding save/register action to commit it.

Annual vacation allowance is optional on each employee. Use whole or half days; blank is unspecified and zero is explicit. The current annual allowance applies to the selected calendar year. Employee requests display X of Y days used without limiting requests. X counts approved working dates in the year, including future bookings, excluding holidays and cancelled/pending/denied requests. Overlapping dates count once. Annual CSV uses current settings; saved payroll revisions keep captured values.

Payroll report and Timesheet links have independent `Send at (clock local time)` controls. Zero payroll lead days means payday; zero timesheet days means the period-end day. Timesheet links at 08:00 and payroll at 15:00 on a common processing date can be configured with the existing day offsets. Payroll must still be closed before its automatic report can queue. The server checks links roughly once a minute; the mailer checks payroll on its normal loop. Send time is a queue cutoff, not a guarantee of SMTP acceptance or receipt at that instant.

Times default to 00:00 on update to preserve previous scheduling. Missed sends catch up within the existing review (14-day) and payroll (45-day) windows and enable-date rules. A manager closing payroll after its cutoff allows that report to queue on the next mailer pass. Manual sends bypass scheduled cutoff times. Existing event identities prevent duplicate automatic notices after restarts or a repeated daylight-saving hour. A skipped local time runs when the local clock first reaches a later time.


## Vacation

Employees request vacation through their private link using dates, a return date and optional notes. Working days are calculated, not manually entered. Managers approve or deny requests on Vacation. Employee requests and decisions retain the configured notifications.

For already-approved or historical vacation, managers use Record vacation. Select the employee and enter the same dates, return date and optional approval details. Past/future dates and inactive employees are supported. The entry is directly Approved, audited and identified as manager-recorded, without request or approval emails. Allowances do not block recording. Cancellation uses the existing audit and notification workflow. Historical changes require revised exports; saved revisions are never overwritten.

Only vacation can be newly entered through these forms and APIs. Existing personal, unpaid and other leave records retain their original types in history. Use Punches and corrections → Recorded absences for sickness and other missed-attendance explanations.

Unchanged default email wording is refreshed to the vacation terminology on upgrade. Custom subjects/bodies and enabled settings are preserved.


## Stat holidays during vacation

Dates in the clock's holiday calendar are excluded from vacation working days. Forms and both request histories show separate vacation/stat totals and name the holidays. For a stat-only selection, the error identifies the date and holiday instead of saying only that no working days were found.

Employee timesheets show a stat once as Stat holiday: NAME, with Not counted as vacation beneath it. The holiday is not also shown as vacation. Working-day display follows the employee's weekly or dated schedule, with the existing weekday fallback. Payroll continues to label overlap as Stat holiday (not vacation), with no worked hours added.

When holidays change later, current request/history counts and annual vacation usage are recalculated. The stored originally requested day count is retained and a difference is shown in history. A previously approved request whose dates all become holidays remains in history with zero vacation days. Existing saved exports are unchanged, and affected periods require a revised export.


## Manager Vacation page

Waiting for approval appears first and remains visible independently of history filters. The Record vacation button opens a dialog, rather than displaying a large empty form on page load. Record already-approved vacation there; the entry is audited without an entry email. The dialog has one Close button at the top; Escape also dismisses it. Successful save closes and resets the dialog and refreshes the lists.

History is split into Upcoming, including ongoing leave, with earliest start first; and Past, with the most recent start first. Filter history by employee and year. The year filter includes any vacation overlapping that year. Pending requests remain in Waiting for approval even if their vacation dates are past. A Dates have passed flag appears when the ending date is before today in the clock’s local timezone; requests ending today or later are not flagged. Managers can still approve or deny the request. Denied/cancelled records remain in their date bucket with explicit status labels.

Lists use 50-record pages with Previous/Next controls when needed. There is no hidden 300-record cutoff. The GET /api/v1/admin/time-off API supports view=waiting/upcoming/past/all, employee_id, year and offset; each response includes total, offset, limit and has_more. API paging is read-only and requires manager sign-in.

Starting-date changes default the ending date to the same day for one-day bookings, while retaining an explicit later end date. The return date is suggested and no longer required input; leaving it blank accepts the server's suggestion. Explicit invalid return dates are still rejected. These date defaults also apply to the employee request form.

Vacation lists show Employee, Dates, Days, Status and Action. Single-day bookings show one date; Days uses a compact count, with additional stat holidays labelled separately (for example, 4 + 1 stat). Expand Details below the dates for return-to-work dates, manager-entry source, holiday explanations, calendar changes and notes. Approved rows use Cancel to cancel the approval through the existing audited decision workflow. Employee IDs remain visible to distinguish identical names.

Payroll Excel workbooks omit Previous approved vacation day reference rows. That context remains in the payroll email body and stored CSV source. Re-downloaded workbooks use this cleaner presentation without modifying the saved revision's CSV or totals. Actual vacation days within the pay period remain in the workbook.

Pay-period approval follows recorded attendance-mode history in the clock's local timezone. A relevant employee needs normal approval if any part of the period was punch-required, or if actual punches are recorded. Mixed-mode periods require one normal approval. Genuine vacation-only periods with leave but no punches show Not required and have no employee approval button or manager override. Changing the current profile cannot remove a past requirement, or impose approval on a period that was entirely vacation-only. Before the first known mode, its initial value applies to backfilled historical leave; missing mode history requires approval. Existing signoffs/audits are retained, and historical worked-shift overrides remain in reports. Unmatched punches still require correction. Vacation requests still need their own approval or an audited already-approved entry. Later attendance/vacation changes still invalidate affected approvals and require revised exports as applicable; saved revisions remain immutable. Existing closed periods with missing required approvals show Changed after close rather than acquiring automatic signoffs.

Employees supports live, case-insensitive name search, including inactive records. Clear the search to show everyone. Add/Edit employee has an optional Employee ID for the company's identifier. IDs are text, preserving leading zeros and letters; surrounding whitespace is trimmed. Blank uses the clock's assigned ID. The chosen identifier appears in the employee list, vacation list/selectors, payroll CSV/Excel/email and annual vacation CSV. Attendance CSV uses it as employee_id; attendance JSON retains its numeric internal employee_id and adds employee_number for the displayed identifier. Internal links, cards, punches and audits always retain employees.id. Explicit duplicate displayed IDs, including another employee's fallback ID, are rejected atomically. When creating an employee with a blank company ID, the clock chooses its next internal ID above existing records, skipping canonical numeric company IDs already assigned elsewhere. Leading-zero IDs remain distinct text. Existing internal IDs are never renumbered. Clearing the field restores its fallback if that ID is not reserved by another employee. Changing it can require a revised historical export; saved CSVs and their downloads keep the captured identifier. One optional employees.employee_number text column is added during upgrade; no new tables are needed.

## Early departures

A valid OUT saves the scheduled finish and grace for a deferred check. An after-midnight IN inside yesterday's overnight shift, through its finish plus grace, belongs to yesterday's shift for dated and weekly schedules. No schedule means no early-departure flag. The same per-clock grace used for arrivals applies to departures: with a 16:30 finish and five minutes of grace, 16:25:00 is within grace and 16:24:59 is flagged as Left early: 6 minutes. Once grace is exceeded, minutes are measured from the scheduled finish, rounded up.

The server checks departures on its minute timer after the scheduled finish plus grace. A later IN recorded before that cutoff suppresses the earlier OUT, so lunch and errands do not create early flags or emails. Only the final eligible OUT is assessed. The finish and grace saved at OUT are used even if settings change while waiting. It does not infer a break, deduct time, extend hours to the scheduled finish, or prevent approval. Actual paired elapsed seconds remain authoritative. Duplicate and out-of-order events do not create departure flags. Existing punches are not retrospectively classified when upgrading or changing grace.

On Punches and corrections, use Manager note beside a flagged IN or OUT to explain an approved late arrival or early departure. Notes retain manager identity and history in the existing swipe-note audit. They appear with the punch and in payroll detail; adding a note does not change punch times or hours. Saved exports remain immutable; create a revised export to include a later note. Early flags are visible in Overview, Punches and corrections and the employee timesheet, and in payroll reason text. Early departure uses editable employee and manager email templates, with the same employee notification switch and holiday suppression as late arrival. The employee email can include a private seven-day explanation link; it is never included in manager email. Optional swipe receipts are immediate. An early-departure email is a separate alert after the shift check; with receipts enabled, an employee can receive both. Late-arrival emails still suppress the generic IN receipt.

## Arrival and departure consistency

Both paths share schedule selection, approved full-day leave/excused-absence eligibility, exact-second grace comparison and rounded deviation minutes. They also share notification rendering, recipient selection, opt-in, template controls, holiday suppression, private explanation-link rules. Arrival flags are stored at IN; departure flags are stored after the deferred check. Neither is recalculated from the current grace setting. Manager notes are available for either direction, and both deviations appear in payroll reason and status text.

Late arrival applies to the first genuine IN for the shift's work date. Early departure is assessed after the finish plus grace, using the final OUT without a later return before that cutoff. A partial-day excused absence covering the entire interval from scheduled start to IN, or OUT to scheduled finish, suppresses the respective flag. Changing an IN recalculates lateness. A changed or manually added OUT saved before finish plus grace remains pending and unflagged until the timer checks later returns at that cutoff. An OUT correction saved after the cutoff is classified immediately from the effective shift and known returns. An IN-only correction preserves its paired OUT's flag and notices. Corrections and voids withdraw pending attendance notices for the changed event. Recalculation sends no additional attendance notice. Saved exports and sent emails remain unchanged. The clock continues to report OUT for device feedback. No payroll or break policy is inferred from these flags.

## Shift matching and offline uploads

The first IN check compares resolved scheduled starts rather than calendar dates. An after-midnight arrival belongs to its previous overnight shift and cannot hide the following night's arrival. A completed meeting ending before the scheduled shift starts does not count as that shift's first arrival.

An uploaded OUT cannot be evaluated until both its scheduled finish plus grace and one minute after receipt have passed. This holds an already-overdue buffered OUT through an intervening timer pass so a following return IN can arrive. This is a settling interval, not proof that a Secondary has uploaded its entire queue; a return delivered later can still arrive after a notice has been sent.

Corrections and manually added punches recalculate lateness for INs and early departure for completed OUTs from effective punch times. Their manager correction/addition audit and normal correction notification remain; recalculation does not send another late-arrival or early-departure notice. Existing saved export revisions remain immutable.

Corrected/manual departure checks save the scheduled finish and grace interval when the correction is made. They use the same minute timer as normal departures but do not issue departure emails. Re-editing or voiding the correction replaces/cancels its pending check. A completed check updates both the correction's derived minutes and its raw attendance row when present; timesheets and payroll use the correction result. The source correction audit is not duplicated by a timer-only derived update.
