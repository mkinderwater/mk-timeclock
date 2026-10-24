# RC3 cleanup23: Late arrivals and early departures

- Pass the scheduled start selected during attendance processing directly into the late-email renderer. Employee and manager notices no longer reconstruct it from rounded late minutes.
- Overview and swipe replay responses use stored late_minutes > 0, without comparing against current grace. Changing grace cannot hide an already recorded late arrival.
- Extend live HTTP regression coverage to both recipient bodies, second-level cutoff and zero-grace starts, duplicate replay after a grace change, exactly-once queuing and the authenticated Overview flag after a grace change.
- Store early departure minutes on a valid OUT when the scheduled finish exceeds the recorded OUT by more than the clock grace. Anchor overnight schedules to the paired IN's local date. No schedule, duplicate or out-of-order punches create no early flag. Exact punch times and durations remain unchanged.
- Show Left early in Overview, punches, private timesheets and payroll reason text. Add an audited Manager note action on flagged INs and OUTs using existing swipe notes/history; include OUT notes in payroll. Preserve saved revisions and queued emails.
- Add editable Early departure (employee) and Early departure (manager) templates, enabled by default like late notices. Share notification eligibility, holiday suppression, idempotent queuing and employee-only explanation links. Use scheduled finish directly, including overnight dates; suppress generic receipts only when the departure email was queued.
- Consistency review: share schedule lookup and grace calculation, suppress both flags on approved full-day leave/excused absences, allow audited manager notes for either flagged direction, and show early/combined deviations in payroll status. Preserve paired-IN anchoring for overnight OUTs and first-genuine-IN eligibility for arrivals.
- Add one early_minutes column to attendance_events, default zero, including current-install upgrade. No new tables or historical reclassification.
- Correct README validation status and document recorded-status semantics and early departures.

Validation: 54 maintained non-browser checks; see validation/cleanup23-suite.json. Expanded live parity cases cover approved/cancelled vacation, full/partial/unexcused absence, both manager-note directions and combined payroll status. Browser and physical Banana Pi checks not run.

## Upgrade

Extract and run sudo ./install-master.sh for a Primary or sudo ./install-slave.sh for a Secondary. No uninstall or database reset is needed. The new early_minutes column defaults to zero for existing attendance records. Existing saved exports remain immutable.

## Timing

Early-departure email is queued immediately for each eligible OUT, including one before a later return. Late arrival applies to the first genuine IN on the local work date. These flags do not change punch times or hours.
