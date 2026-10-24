# RC3 cleanup16: Quieter vacation history

- Reduce manager vacation lists to Employee, Dates, Days, Status and Action. Remove the always-visible Notes column.
- Show a single date for one-day bookings and compact numeric day counts. Stat holidays remain separately identified, for example 4 + 1 stat; the full vacation/stat breakdown remains in Details.
- Put return-to-work dates, manager-entry source, notes, decision notes, calendar-change warnings and named holiday explanations under a collapsed Details control below the dates. Do not discard any stored or displayed supporting information. Employee IDs remain visible to distinguish identical names; waiting requests retain Dates have passed flags and decision actions.
- Rename Cancel approval to Cancel. The existing audited cancellation operation, notifications and data remain unchanged.
- Add only shared token-based Details styles; regenerate ui.inc. Retain filters, date sorting, paging and dialog behavior. No schema or payroll calculation changes.

## Validation

All 52 maintained checks pass, including four browser scripts. See validation/cleanup16-suite.json. Browser checks cover five columns, collapsed/expanded Details, the compact holiday count with accessible holiday explanation, shortened action labels, mobile/desktop layout and existing manager workflows. CSS lint and source preflight pass. Server builds with -Wall -Wextra -Werror. No physical Banana Pi acceptance run.

## Upgrade

Extract the source and run sudo ./install-master.sh for a Primary or sudo ./install-slave.sh for a Secondary. No uninstall or database reset is needed.
