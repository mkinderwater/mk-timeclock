# RC3 cleanup21: Safe ID allocation and historical approval requirements

- For a new employee with a blank/omitted company ID, allocate the next clock ID above existing internal IDs, skipping numbers already reserved as company IDs. Walk sorted canonical numeric reservations once, including long consecutive ranges. Preserve distinct leading-zero IDs, explicit duplicate rejection, atomic card/audit assignment and all existing internal IDs.
- Insert new employees in the selected attendance mode directly, avoiding a spurious initial punch-required history entry before a vacation-only update.
- Determine approval from recorded mode history for the clock-local period, not the current profile. Any punch-required interval overlapping the period requires approval. A mixed-mode period needs one normal approval, and actual recorded punches always require review. Genuine vacation-only periods with leave but no punches remain exempt.
- Apply the same shared SQL policy to manager detail/list counts, employee private views/direct approval, manager signing, close guards and stale-signoff checks. Use request-local cached pairs in period lists. Keep automatic-link eligibility based on the same historical mode. Restore override commentary for historical worked shifts even after the employee changes to vacation-only.
- Before the first known mode, use its initial value for backfilled historical leave. If no mode history exists, require approval rather than guessing an exemption. Local midnight boundaries use UTC history timestamps correctly.
- Preserve saved payroll CSV revisions, source events, existing signoffs/audits and current database tables. No new tables, columns, indexes or migrations.

## Validation

All 56 maintained checks pass, including four browser scripts. See validation/cleanup21-suite.json. New live regressions reproduce blank-ID collisions, 1,200+ consecutive reservations and the 17-hour approval bypass. Cover both mode-change directions, mixed periods, local midnight boundaries, missing history, actual punches, list/detail/private/direct-close agreement, override commentary, stale approvals and immutable exports. Browser coverage verifies the historical 17-hour private view still offers approval and the manager screen still blocks closing after a mode switch. Existing vacation-only and company-ID tests remain enabled. Server and mailer build with -Wall -Wextra -Werror. No physical Banana Pi acceptance run.

## Upgrade

Extract the source and run sudo ./install-master.sh for a Primary or sudo ./install-slave.sh for a Secondary. No uninstall or database reset is needed.
