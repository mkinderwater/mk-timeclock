# RC3 cleanup25: Shift-specific arrivals and upload settling

- Match earlier INs to the same resolved scheduled start. Consecutive overnight shifts and a completed daytime meeting cannot hide the night's late arrival. Lunch returns within a shift remain subsequent INs.
- Recalculate lateness for corrected and manually added INs, retaining it in punches, employee detail and payroll reason text. Persist derived manual/correction lateness separately from the correction's source time/reason; only source edits create correction-history entries. Do not issue another late email merely because a manager corrected a time.
- Restore immediate optional swipe receipts. A final early-departure alert is separate and may follow an immediate OUT receipt; late IN alerts retain receipt suppression. Remove the deferred receipt path.
- Add a saved one-minute receipt settling time for OUT evaluation, in addition to finish plus grace. An overdue buffered lunch OUT survives an intervening timer pass while its return uploads. A substantially delayed return can still arrive after notification; no queue-completion guarantee is inferred.
- New departure_ready and correction late_minutes columns; no new tables. Current source times, audits, saved exports and notification history are retained.

Validation: see validation/cleanup25-suite.json. Live regressions include two consecutive overnight shifts for weekly and dated schedules, a daytime meeting, corrected/manual late INs in API/payroll, immediate receipt and overdue upload/return timing. Browser and physical hardware checks were not run.

## Upgrade

Extract and run sudo ./install-master.sh for a Primary or sudo ./install-slave.sh for a Secondary. No uninstall or database reset is needed. New columns are added automatically. Existing saved export revisions stay immutable.
