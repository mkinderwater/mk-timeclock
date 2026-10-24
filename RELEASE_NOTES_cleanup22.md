# RC3 cleanup22: Late grace and page order

- Classify an arrival as late only when it exceeds the clock's grace, comparing exact seconds. With five minutes, 08:05:00 is on time and 08:05:01 is late. Once exceeded, late minutes count from the scheduled start. Preserve exact source punch times and recorded hours.
- Use the same decision for recorded late status and late notification, removing duplicate schedule lookup. Newly processed punches follow this rule; existing historical attendance events and saved exports are unchanged.
- Move the backup list below email settings, upload and restore.
- Sort the Punches and corrections employee dropdown alphabetically by displayed name, ignoring case, with internal ID as a stable tie breaker.
- No schema changes.

## Validation

53/53 maintained non-browser checks passed (validation/cleanup22-suite.json). Includes live grace-boundary/default/zero-grace and notification checks, dated shifts, historical approval and immutable export coverage. CSS lint and preflight passed. Employee sort was executed with mixed-case names; backup section order checked. Server compiled with -Wall -Wextra -Werror. Browser and physical Banana Pi checks were not run for this release.

## Upgrade

Extract the source and run sudo ./install-master.sh for a Primary or sudo ./install-slave.sh for a Secondary. No uninstall or database reset is needed.
