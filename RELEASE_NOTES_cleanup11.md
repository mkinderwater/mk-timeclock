# RC3 cleanup11: Stat holidays during vacation

- Request previews, employee history and the manager approval list show vacation/stat totals, such as 4 vacation days + 1 stat holiday, with named holiday dates.
- Stat-only selections explain that there are no vacation working days and name the affected holidays. The server applies this to employee requests and manager entry.
- Employee timesheets show one stat row with Not counted as vacation, without an overlapping vacation row. Vacation dates follow current weekly/dated schedules with the existing weekday fallback.
- Current history counts recalculate after calendar changes. The stored original requested count remains intact; differing counts are explained in history. Existing approved records are not deleted when the current vacation count becomes zero.
- Holiday dates remain visible even on non-working days. Annual usage excludes holidays. Payroll keeps its existing Stat holiday (not vacation) rows. Saved export revisions remain immutable and affected current exports are flagged for revision.

No tables, columns, indexes or triggers changed. Existing requests, approvals and audit records are retained. Includes the vacation-only workflow from cleanup10.

## Validation

Server compiles with -Wall -Wextra -Werror on x86_64. Maintained suite including three browser scripts: 47/49. New HTTP tests cover named stat-only errors, pending/approved request breakdowns, calendar edits, original counts, Saturday schedules, zero-day current records, annual totals and immutable exports. New browser tests cover both forms, history, named errors, one-row holiday display and subtext at 390/1280px; existing 320/390px workflows also pass. CSS lint and source preflight pass.

The two existing overnight private-review tests still fail exactly as on cleanup8/cleanup10. See validation/cleanup11-suite.json. No physical BPI/ARM test. Agent code is unchanged apart from the version header and was not rebuilt here.

## Upgrade

Back up, extract, enter the cleanup11 source directory and rerun sudo ./install-master.sh for the Primary, or sudo ./install-slave.sh for a Secondary. No uninstall or database reset is needed.
