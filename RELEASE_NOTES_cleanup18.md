# RC3 cleanup18: Vacation-only approval exemption

- Exempt current vacation-only employees from employee timesheet approval and manager pay-period signoff/override. The manager list shows Not required, omits the action, and excludes that row from approval warnings. The private record remains viewable with an explicit no-approval message and no approval button.
- Enforce the exemption server-side: reject unnecessary employee/manager signoff submissions, exempt vacation-only activity from closing's missing-signoff check, and ignore their stale signoffs during closure. Do not create artificial approved signoffs. Keep existing audits and signoffs as history.
- Stop emitting manager-override commentary for vacation-only employees in newly generated reports. Saved CSV revisions remain unchanged. Current attendance mode controls the exemption; recorded punches and unmatched-punch safety checks remain intact.
- Keep approval requirements for punch-required employees, including leave-only periods. Vacation requests still need explicit approval; already-approved manager entry remains audited. Vacation-only leave still appears in payroll and subsequent changes require revised exports.
- No new tables, columns, indexes or migrations.

## Validation

All 53 maintained checks pass, including four browser scripts. Two fixtures with obsolete vacation-only approval expectations were updated and rerun after the initial suite; the report retains the initial failures and rerun results. See validation/cleanup18-suite.json. New HTTP coverage checks mixed and vacation-only periods, direct signoff rejection, stale pre-upgrade overrides, closing without artificial approvals, retained punch-worker approval requirements, pending vacation decisions, payroll records, unchanged schema and immutable/revised exports. Browser coverage checks the private approval button is hidden and manager rows show Not required without an override. Existing leave-only employee approval and reapproval coverage now uses punch_required, matching the clarified rule. The vacation-only link test now checks rejection without creating a signoff. Server and mailer build with -Wall -Wextra -Werror. No physical Banana Pi acceptance run.

## Upgrade

Extract the source and run sudo ./install-master.sh for a Primary or sudo ./install-slave.sh for a Secondary. No uninstall or database reset is needed.
