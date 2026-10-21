# mk-timeclock Lite: RC3 cleanup candidate 6

The accepted RC3 benchmark is unchanged. This is a separate cleanup candidate with the accepted attendance and payroll behavior, plus the UI and email changes below. It is source, not an ARM image or a deployment.

## Changes in cleanup6

The employee vacation allowance input now aligns with adjacent fields. Its shorter label stays on one line, and its explanatory text is below the input grid, linked to the field with `aria-describedby`. Calendar-year meaning and above-allowance requests are unchanged.

Targeted browser validation checks the vacation and phone inputs have the same top position and height on desktop, and retains the existing phone and workflow acceptance checks. The latest full 44-script baseline remains cleanup4.

## Retained cleanup5 fix

The annual vacation CSV download uses a standard submit button inside an action row, sharing the exact button CSS used by other forms. Its small GET form submits the selected calendar year directly. The incomplete standalone link-button CSS is removed, and the stylesheet cache version is bumped.

Validation for this presentation change: CSS lint, browser acceptance including computed standard-button style equality and a real selected-year CSV download, and 99 scripts parsed from 35 served pages. `validation/current-suite.json` retains the latest full 44-script baseline from cleanup4; `validation/download-button-fix.json` records cleanup5's targeted checks.

## Retained cleanup4 fix

Fixes the certificate page showing None installed after an upgrade. A late installer directory-setup command reset `/etc/timeclock/tls` to root-only after the earlier permissions repair. Its final setup now preserves `root:timeclock` ownership and mode `0710`, allowing the backend to see certificate status/expiry markers. Certificate versions and private keys remain root-only. Re-uploading certificates or resetting data is unnecessary.

For an already-installed clock, apply the repair over SSH and refresh the certificate page:

```bash
sudo chown root:timeclock /etc/timeclock/tls
sudo chmod 0710 /etc/timeclock/tls
```

## Retained cleanup3 features

- Employee creation/edit includes an optional annual vacation allowance in whole or half days. Blank is unspecified; zero is zero. The allowance does not limit requests or approvals.
- The private time-off form shows `Vacation: X of Y days used` for the selected calendar year(s). X counts distinct approved scheduled/estimated working dates, excluding statutory holidays, cancelled requests, pending requests and denied requests. Future approved dates count toward the year. No balance or accrual is calculated.
- Payroll Summary includes annual used/allowed figures, captured as reference records in the immutable saved CSV. A later allowance or vacation change requires a new revision; earlier revisions retain their original figures. The annual vacation CSV also shows used/allowed totals.
- Payroll report and Timesheet links each have a local `Send at` time. Existing day settings, including zero, remain supported. Existing schedules default to `00:00` until changed.
- Automatic sends queue at or after their selected time. Within the existing catch-up windows, missed runs queue when the clock returns. Existing unique event keys suppress duplicates, including a repeated daylight-saving hour. A time skipped by a spring clock change runs at the first available later local time.
- Manual **Send report** and **Create and email** remain immediate. Payroll still requires a closed period; timesheet links retain their existing open-period and employee eligibility rules.

Retains cleanup2's sender name, timezone dropdown, System health borders, CSV download button, scan-button UIDs and trusted-network/VPN backup SMTP None support.

Updating current RC3 adds only the optional employee allowance and two send-time settings. Existing accounts, SMTP settings, attendance and saved revisions are preserved. No reset is needed.

## Build

On the target Linux system, install the build dependencies through the existing installer, or provide a C compiler, SQLite, libcrypt, libcurl, FreeType development files and pkg-config. Run `make`. The installer compiles for the clock; do not copy development-host executables onto a Banana Pi.

## Install and operate

- [Installation and roles](docs/INSTALLATION.md)
- [Manager operations, schedules and email](docs/OPERATIONS.md)
- [Attendance and payroll rules](docs/ATTENDANCE_PAYROLL.md)
- [Backup and restore](docs/BACKUP_RESTORE.md)
- [SQLite durability](docs/SQLITE_DURABILITY.md)
- [Schema ownership and retained data](docs/SCHEMA.md)
- [Testing and limits](docs/TESTING.md)
- [Cleanup inventory and validation](CLEANUP_REPORT.md)

Only fresh/current schemas are supported. Historical schema conversion is removed. The installer does not erase databases. Keep independent backups and use an approved test device for deployment validation.

After building, run `python3 tests/run_current.py`. Browser checks additionally need Playwright and Chromium; see Testing. Hardware, power-loss and installed-system restore acceptance remain outstanding.
