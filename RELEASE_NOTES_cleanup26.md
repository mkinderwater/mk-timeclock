# RC3 cleanup26: Preserve and recalculate corrected departures

- An IN-only correction preserves its paired OUT early flag, pending departure check, and notification history. It no longer erases or cancels a real departure merely because arrival time changed.
- Corrected and manually added OUTs calculate early minutes against the effective paired IN's schedule, with grace, leave/absence and later-return checks. Store the result on the correction row and, for a raw swipe, its attendance row. An on-time correction computes zero rather than retaining the obsolete flag.
- Use correction early minutes in manager punches, private timesheets and payroll reason/status. Overview uses the synchronized raw attendance value.
- Cancel the corrected event's stale pending notices, retain sent emails, and do not enqueue another early notice. The deferred checker cannot replace a manager-reviewed correction.
- Add nullable early_minutes to punch_corrections, with fallback to existing derived values for older correction rows. No new tables. Preserve source swipes, correction audit and saved exports.

Validation: see validation/cleanup26-suite.json. Live regression covers finalized early OUT retained after IN correction, 90-minute corrected OUT, manually added early OUT, API/payroll agreement, repeated evaluator runs without duplicate alerts, and correction to an on-time OUT. Browser and physical hardware checks not run.

## Upgrade

Extract and run sudo ./install-master.sh for a Primary or sudo ./install-slave.sh for a Secondary. No uninstall or database reset is needed. The correction early_minutes column is added automatically. Existing saved export revisions stay immutable.
