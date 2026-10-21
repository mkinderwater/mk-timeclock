# Attendance and payroll

IN starts recorded elapsed time; OUT ends it. Unpaired punches are flagged, not completed by invention. Time between OUT and the next IN is not attendance. There are no break deductions, overtime-pay calculations or decisions about payable hours.

Source swipes, effective corrections, approvals and audit histories have different purposes and remain separate. Manager corrections are audited. Changes after a period closes can invalidate reviews and require another saved export revision; closure does not silently discard a late upload or correction.

Saved CSV revisions remain immutable. Downloads and email attachments use the saved CSV. A converter update can change workbook presentation without changing that CSV's contents. Manual revised-report requests survive a pending original email and clear only when the latest revision has been queued.

The workbook has two tabs:

- Payroll Summary contains employee hours, leave counts, exceptions and links to the employee's detail.
- Daily Detail remains one record per row with no subtotal/blank separator rows. Employee blocks follow Summary name/ID order, then date and IN time. Undated reference/override rows finish each block. Alternate blocks are shaded and the header has AutoFilter.

Workbook hours sum the CSV's rounded per-shift hours. Email period totals use seconds and can differ slightly. Summary notes are capped within Excel's text limit, with a visible truncation marker; full individual notes remain in Daily Detail. Invalid UTF-8/XML text is replaced for workbook rendering without rewriting the source CSV.

Stat holidays remain visible during approved vacation even on an unscheduled weekday. A holiday row is not a vacation day or worked/payable time. Historical activation data prevents later hires from acquiring earlier holiday rows.

Annual vacation reporting is informational. Payroll Summary shows calendar-year approved usage against the employee's configured annual allowance, including approved future bookings. These figures are frozen in annual reference records in the saved CSV. For a pay period crossing New Year, separate figures are shown for each year. Reference rows add no hours or period leave days. The source CSV still has the same columns; Daily Detail retains one row per source record and no totals.

Older saved CSV revisions without annual reference records leave the new Summary column blank. They are not filled from live employee settings. Annual CSV is a live report: its summary counts each approved date once and includes employees with an explicit allowance even when usage is zero. It is not an immutable payroll revision.
