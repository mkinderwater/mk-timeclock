# RC3 cleanup19: Name search and company employee IDs

- Add instant, case-insensitive name search on Employees. Match first/last names in either order, retain the search after a save and show a clear no-match message. Include inactive employees; clearing restores all rows.
- Add optional Employee ID to Add/Edit employee and the employee table. Store as text to preserve leading zeros and letters. Trim surrounding whitespace, reject control characters/oversized values, and reject duplicate principal IDs, including collisions with another employee's numeric fallback. Reject the whole save without partial employee/card/audit changes.
- Use the company ID as the public employee identifier, with the clock ID as fallback when blank. Internal primary/foreign keys, card ownership, punch pairing, permissions and audits remain numeric and unchanged. Changing or clearing the public ID does not reassign records.
- Apply the principal ID to payroll CSV, both Excel tabs, standard email labels, annual vacation CSV and vacation manager lists/selectors. Attendance CSV uses the principal ID; attendance JSON retains internal employee_id and adds the principal employee_number string. Saved payroll CSVs and downloads keep their captured identifier; changes can require revised exports. Preserve the existing CSV rendering for unchanged fallback identifiers.
- Add only employees.employee_number TEXT NOT NULL DEFAULT ''. Current installations receive the optional column on startup, with blank defaults. No new tables, indexes or obsolete migration framework.

## Validation

All 54 maintained checks pass after a successful rerun of the mobile browser script, which initially reached the runner deadline. The report retains that initial timeout. Expanded restart/upgrade and company-ID report-delivery checks were rerun successfully; the latter passes 44/44 assertions. See validation/cleanup19-suite.json and validation/cleanup19-revision-delivery.json. New HTTP checks cover creation/edit/clear, zero/leading zeros, duplicates/fallback collisions, authorization, atomic rollback, audits, stable attendance relationships, annual/raw/payroll exports and immutable revisions. Restart and pre-field schema upgrade checks preserve existing records/snapshots. Browser checks cover name filtering/no-match/clear and company ID creation/save/reload, plus existing employee alignment and full-page spacing coverage. SMTP checks compare the saved-revision attachment and company identifiers. Server and mailer build with -Wall -Wextra -Werror. No physical Banana Pi acceptance run.

## Upgrade

Extract the source and run sudo ./install-master.sh for a Primary or sudo ./install-slave.sh for a Secondary. No uninstall or database reset is needed.
