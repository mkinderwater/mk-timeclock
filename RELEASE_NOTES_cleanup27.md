# RC3 cleanup27: Recheck corrected lunch departures

- Corrected and manually added OUTs saved before scheduled finish plus grace stay pending with zero early minutes. The minute timer later checks effective return INs before classifying the departure, preventing a lunch correction from leaving a permanent early flag.
- OUT corrections saved after the cutoff still classify immediately. Pending corrections use saved finish/grace, survive restart, and update correction and raw attendance values together without sending another departure notice.
- Re-editing/voiding resets a correction's pending check. Derived timer updates do not add source correction-history entries. Preserve paired OUT flags on IN-only corrections, saved CSV revisions and source events.
- Add departure_finish/departure_due fields and a partial pending index to punch_corrections. No new tables or timer service.

Validation: see validation/cleanup27-suite.json. Live tests cover raw and manual lunch OUT corrections saved before the return, exact cutoff, later return and final OUT, repeated evaluation, API flags and absence of early emails. Existing corrected true-early departure resolves to 90 minutes after the cutoff. Browser and physical hardware checks not run.

## Upgrade

Extract and run sudo ./install-master.sh for a Primary or sudo ./install-slave.sh for a Secondary. No uninstall or database reset is needed. Pending correction fields are added automatically; saved export revisions stay immutable.
