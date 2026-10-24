# RC3 cleanup24: Final departure and overnight schedule fixes

- Defer OUT classification and early-departure notices to the server's minute tick after scheduled finish plus grace. Later INs before that cutoff suppress intermediate OUTs such as lunch and errands. Optional swipe receipts for scheduled OUTs are also deferred, preserving one employee notice.
- Store scheduled finish and due time on each scheduled OUT. Use the saved grace interval during evaluation. Add a partial index for outstanding checks; each pass processes at most 100 due records. No new tables or timer services.
- Resolve an after-midnight IN against the previous day's overnight shift when it falls within that shift through finish plus grace, for dated and weekly schedules. Use the same shift for arrival and departure.
- Suppress a flag when an excused partial-day absence covers the complete flagged interval. Full-day approved leave/excused absence rules remain.
- Manager corrections/voids clear affected automatic flags and cancel pending attendance notices; corrected INs also clear their paired OUT's flag/check. Sent emails, source swipes, audit history and saved export revisions are preserved.
- Rename grace controls to Save grace and Grace. Document deferred timing.

## Validation

54/54 maintained non-browser checks passed. Extended live regressions use the production deferred evaluator with an explicit test time: lunch sequence, exact due boundary, repeated checks, real early departure, dated/weekly midnight arrivals, overnight finish text, covered partial absence, correction before/after evaluation, suppressed unsent notices, unchanged hours and frozen exports. See validation/cleanup24-suite.json. Browser and physical hardware checks were not run.

## Existing records

New deferred fields default to zero on upgrade. Previously recorded cleanup23 early flags and sent emails are not retrospectively rewritten. A manager correction clears the affected automatic flags. Existing saved reports stay immutable.

## Upgrade

Extract and run sudo ./install-master.sh for a Primary or sudo ./install-slave.sh for a Secondary. No uninstall or database reset is needed. Pending departure checks resume after restart.
