# RC3 cleanup12: Manager Vacation page

- Lead with Waiting for approval. The Record vacation button opens a native dialog with the existing shared vacation fields and a single help sentence. Successful save closes/resets the dialog and refreshes lists.
- Split history into Upcoming (including ongoing leave, earliest start first) and Past (most recent start first), with deterministic tie ordering. Pending requests remain in Waiting for approval regardless of vacation date.
- Add employee and year filters to history. Year filters include overlapping cross-year vacation. Waiting requests stay visible independently of these filters. Denied and cancelled history retain explicit status labels.
- Replace the manager API's hidden 300-row ceiling with 50-row pages, Previous/Next and range counts. Records beyond the former ceiling are reachable. No new tables, columns, indexes or triggers.
- Default end date to start date for one-day requests/entries while retaining an explicitly chosen later end date. Suggested return dates are pre-filled and are optional input: blank/omitted values use the same server-side suggestion. Explicit invalid dates remain rejected. These defaults also apply to the employee form.
- Retain vacation-only enforcement, no allowance enforcement, stat-holiday breakdowns, auditing, entry-email suppression and immutable saved payroll revisions.

## Validation

Server builds with -Wall -Wextra -Werror on x86_64. Maintained suite including four browser scripts: 49/51. New HTTP checks exercise 300+ history rows, paging without lost/duplicate IDs, date buckets, filters, year-crossing leave, invalid filters, authorization, optional return dates, entry email suppression and unchanged schema. Browser checks at 320/390/1280px cover queue-first layout, closed dialog, filters and page resets, sorting, approval leaving the queue, one-day defaults, server-suggested return, dialog close/reset/Escape, and existing vacation/stat workflows. CSS lint and preflight pass.

Two pre-existing overnight private-review tests still fail identically to cleanup8/cleanup11. No tests were skipped or weakened to hide these failures. See validation/cleanup12-suite.json. No physical Banana Pi/ARM acceptance run; agent code is unchanged except version header and was not rebuilt here.

## Upgrade

Back up, extract cleanup12, enter its source directory and rerun sudo ./install-master.sh for the Primary or sudo ./install-slave.sh for a Secondary. No uninstall or database reset is needed.
