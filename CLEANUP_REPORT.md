# RC3 cleanup candidate 6

Current version: `1.9.5-RC3-cleanup6`. Employee creation/edit uses the shorter `Vacation allowance (days)` label. Its calendar-year explanation sits below the form grid and is associated with the input using aria-describedby. This removes both label wrapping and the extra help-text row from the grid cell, aligning its input with the neighboring phone field.

Targeted verification is recorded in validation/form-alignment-fix.json. The existing browser acceptance compares actual desktop input positions/heights while retaining its phone-layout and workflow checks. The full 44-script baseline remains cleanup4; no full cleanup6 rerun is claimed. No database or payroll calculation changes.

Earlier release evidence follows.

## Cleanup5 history

Current version: `1.9.5-RC3-cleanup5`. Replaces the annual vacation report's partially styled link with a native submit button in an action row. The selected year is submitted by a standard GET form to the existing CSV endpoint. Removed the custom `a.button` rules and bumped the stylesheet version to invalidate old browser CSS.

Targeted validation passed: existing phone/browser acceptance, including an actual selected-year CSV download and equality of all principal computed styles against a normal submit button; 99 inline scripts parsed across 35 served pages; CSS lint. All three programs compile with `-Werror`. The full 44-script report retained in current-suite.json is the cleanup4 baseline, not a newly claimed full cleanup5 run. No database, CSV contents, payroll calculations or installed clock were changed by this fix.

Earlier release evidence follows.

## Cleanup4 history

Current version: `1.9.5-RC3-cleanup4`. Fixes a later installer command undoing the TLS status-directory permissions established earlier in installation. Nginx's root process could still read the installed certificate, while the timeclock backend could not traverse the parent directory to check its active/expiry markers.

The final installer command now uses `root:timeclock` ownership with mode `0710`. No certificate files, private keys, settings or database content are changed. The regression check replays every installer operation on the TLS directory in source order against an isolated filesystem fixture and verifies the final mode, private-directory/key modes and preserved contents. A separate backend-identity access probe runs where UID/GID mappings permit it; this development container maps only root, so that identity probe is explicitly reported unavailable. No live clock deployment is claimed.

Validation: 44/44 maintained scripts passed, including browser checks. All three programs compiled with `-Werror`. Physical Banana Pi upgrade verification remains outstanding.

Earlier cleanup reports follow for context.

## Cleanup3 history

Current version: `1.9.5-RC3-cleanup3`. Builds on cleanup2 with annual vacation allowance/usage and independent local send times. No device was deployed or live database modified.

Validation: 44/44 maintained scripts pass, including browser acceptance. All three programs compile with `-Werror` on the development Linux host. Physical Banana Pi/service timing verification remains outstanding.

Three current schema additions: nullable `employees.vacation_days`, `payroll_settings.send_time` and `review_send_settings.send_time`. No tables, indexes, triggers or views were added. Updates preserve existing data and initialize send times to `00:00`; allowances start unspecified. The original-RC3 sender-name addition remains supported.

Vacation usage has one calendar implementation shared by employee display, annual CSV and payroll export. It counts approved vacation dates once even for overlapping legacy records, clips calendar years, follows schedule/holiday rules and excludes pending/denied/cancelled requests. The allowance is informational, including when zero or exceeded. Payroll carries annual reference records in its frozen CSV; workbook totals remain on Payroll Summary and Daily Detail remains a filtered employee-grouped data table with no subtotal rows.

Scheduling uses one local-clock cutoff helper in both dispatchers. Existing deduplication, recipient/template rules, catch-up windows and manual-send behavior are retained. Tests execute the actual server and mailer scheduler functions with a controlled clock, including same-day ordering, before/at cutoff, restarts, zero-day options, manual bypass and DST gap/repeat. HTTP tests cover current settings upgrade, time validation and manager-card authorization. Browser acceptance saves/reloads both send times and creates/edits allowances, then submits a request above a zero allowance.

The following preserves cleanup2 and cleanup1 review evidence. Their validation counts and schema-equivalence claims describe those earlier passes only. Current results are in `validation/current-suite.json` and `validation/schema-change.json`.

## Cleanup2 history

Current version: `1.9.5-RC3-cleanup2`. Built on cleanup1, with the UI and sender-name changes listed in README. No deployment or live database modification was performed.

The sole schema addition is `smtp_settings.sender_name TEXT NOT NULL DEFAULT ''`. Current RC3 updates add that field without resetting data; fresh databases declare it directly. No tables, indexes, triggers or views were added. The retained cleanup1 schema-equivalence report describes the earlier cleanup pass only.

Validation: 41/41 maintained scripts pass, including browser acceptance. All three programs compile with `-Werror` on the development Linux host; no ARM or physical hardware execution is claimed. The maintained suite now includes sender-name API and actual SMTP delivery tests (ASCII, quotes, backslashes, accented names and blank), header-injection and length rejection, preserving current SMTP settings on update, and enabling backup email through HTTP with None. Browser acceptance includes sender-name save/reload, the timezone dropdown, the health table's final border, the CSV download button and all three scan buttons. The SMTP revision fixture still checks exact saved-workbook bytes, and backup SMTP still checks exact archive bytes.

The following records the original cleanup1 removals and coverage decisions.

## Cleanup1 history

## Result

Separate source candidate: `1.9.5-RC3-cleanup1`. The accepted RC3 benchmark is unchanged. This pass removes confirmed debris and simplifies fresh/current initialization; it does not remove attendance, review, payroll, backup or hardware features. No device was deployed or live database modified.

## Removed and simplified

- Removed unused workbook tab-name helper, imports, formula-emission path, old status-color fills and unused style records. Used styles retain identical appearance; style definitions shrink from 19 to 10 and fills from seven to three.
- Removed unused payroll calendar helper chain. Calendar tests now call the payday/shift functions actually used by delivery.
- Removed old-schema ALTER/inspection branches for schedule mode, signoff fields, time-off fields, email tokens and agent cache mode. Current fields are declared directly. Removed two obsolete index-drop statements.
- Defined signoff history triggers in reconciliation's schema owner; removed duplicate delete-trigger creation. Server owns email-token creation; mailer current-record token/identity repair remains.
- Removed `patch52.py` and `patch52mailer.py`, which targeted an old `/mnt/data/r51_work` tree.
- Consolidated historical root notes and stale operational instructions into the current README and seven focused documentation files. Retained current backup/restore and SQLite durability guidance, including the trusted-network/VPN requirement for unencrypted backup email.
- Corrected environment-example references to nonexistent installer entrypoints and removed a stale version label from the non-destructive stability checker.
- Replaced four obsolete tests only after current coverage passed: older-schema upgrade, old r23 source/version assertions, per-employee workbook tabs/styles, and the old RC2 closed-period lock. Other legacy/source guards remain pending individual coverage review.
- Added an explicit maintained test manifest/runner and current schema, dispatch and closed-period upload checks. The legacy test directory is not represented as fully reviewed or all passing.

## SQL tables retained

All **70 server tables, 20 triggers, two views and seven explicit indexes** remain. Full before/after schema comparison matched column order, types, defaults, keys, foreign keys and executable trigger/view/index definitions. No whole table was proven redundant; write-only audit/history is not treated as dead data. See `docs/SCHEMA.md` for ownership.

## Verification

- Server, mailer and agent compile on the development Linux host with `-Wall -Wextra -Werror`.
- **40/40 maintained checks pass**, including Chromium phone layouts. The SMTP revision fixture also passes its 39 internal assertions.
- Fresh/current schema, restart identity and token preservation, signoff update/delete histories and queue cache constraints pass.
- Baseline/candidate workbook values, hyperlinks, AutoFilter, row heights and every used effective cell style match exactly. Unused style IDs are intentionally renumbered; workbook ZIP bytes change, source CSVs do not.
- CSS lint passes. Retained Python files parse. Current documentation links resolve.
- Raw run evidence is in `validation/current-suite.json` and `validation/schema-equivalence.json`.

## Remaining limits and next review

Historical database upgrades are unsupported; the installer does not erase current data. Verify on a fresh/current test device. This is a development-host result, not ARM execution or physical-device acceptance.

Still outstanding: on-Pi list latency against the 15-second deadline, real RFID/OLED/buzzer/touch, power interruption, dual-clock offline recovery and installed-system backup/restore. The server's serial-request bottleneck is unchanged. Unreviewed legacy tests and residual compatibility-looking paths must be traced before further removal.

## Retired tests and replacements

| Retired test | Current coverage |
| --- | --- |
| `test_rc2_schedule_upgrade.py` | `test_cleanup_schema.py`, `test_rc2_schedule_validation.py`, `test_rc2_recurring_schedule.py` |
| `test_r23_payroll.py` | `test_current_dispatch_sql.py`, `test_rc1_payday_lead.py`, frequency and revision SMTP tests |
| `acceptance/test_payroll_workbook.py` | Two-tab, employee-block, precision, source-record and note-limit workbook tests |
| `test_rc2_cross_period_pairs_http.py` | `test_current_closed_period_upload_http.py`, `test_rc3_closed_period_attendance.py` |

## Removed historical documentation/build artifacts

These remain available in the untouched benchmark package.

- `RC1_TLS_STATUS_PERMISSIONS.md`
- `RC1_GUI_HOTFIX_2.md`
- `FEATURES_v0.9.115.md`
- `TIMEZONE_v0.9.117.md`
- `RC1_SIMPLIFIED_PAY_PERIODS.md`
- `RC1_TABLE_AND_OVERVIEW_FIX.md`
- `RC1_CLOCK_SETTINGS_AUDIT_FIX.md`
- `RC1_WELCOME_TLS_CACHE_FIX.md`
- `RC3_SIMPLIFICATION_STATUS.md`
- `RC1_TIMEOFF_NAV_BADGE.md`
- `RC1_UI_RUNTIME_FIX.md`
- `RC3_HOLIDAY_AND_REVISIONS_STATUS.md`
- `RC1_PAYDAY_TIMING_RESTORED.md`
- `RC1_COSMETIC_CONSISTENCY.md`
- `FIRST_MANAGER_v0.9.129.md`
- `RC1_APPROVAL_DISPLAY_FIX.md`
- `RC3_TWO_TAB_PAYROLL.md`
- `NORTHSTAR_HOURLY_FIXTURE.md`
- `RC2_RECURRING_SCHEDULES.md`
- `RC2_SIGNOFF_CORRECTIONS.md`
- `FONT_v0.9.119.md`
- `LATE_EXPLANATIONS_v0.9.121.md`
- `RC1_CLOCKS_DATES_HOTFIX.md`
- `CLI.md`
- `LITE_1_9_RELEASE.md`
- `RC2_SCHEDULE_VALIDATION.md`
- `INSTALL_ROLES.md`
- `DIMMING_v0.9.118.md`
- `FIRST_MANAGER_GUI.md`
- `FEATURES_v0.9.123.md`
- `ADOPTION.md`
- `RELEASE_VALIDATION.md`
- `PAYROLL_UNIFICATION.md`
- `RC1_PAYROLL_RECOVERY_HOTFIX.md`
- `RC3_MOBILE_REQUESTS_FREQUENCY.md`
- `BOOT_LOGO.md`
- `RC1_EMPLOYEE_EMAIL_ACTIONS.md`
- `HOLIDAY_HISTORY_FIX.md`
- `TODO.md`
- `RC2_RELEASE.md`
- `RC1_ANNUAL_VACATION_REPORT_FIX.md`
- `FEATURES_v0.9.125.md`
- `LITE_BUILD_STATUS.md`
- `MANAGER_ACCOUNTS_AND_REVIEWS.md`
- `ORIGIN_CHANGE_v0.9.130.md`
- `test-checklist.md`
- `RC1_GUI_HOTFIX_3.md`
- `LITE_1_9_3_RELEASE.md`
- `LITE_1_9_1_RELEASE.md`
- `CULL_HOTFIX_1_8_1.md`
- `EMAIL_LINKS_v0.9.122.md`
- `SCHEMA_AUDIT_v0.9.130-r17.md`
- `RC2_CLOSED_PERIOD_INTEGRITY.md`
- `BACKUP_UPLOAD_v0.9.130-r10.md`
- `RC2_CROSS_PERIOD_PAIRING.md`
- `RC1_README.md`
- `CODE_CULL_1_8.md`
- `RC3_WORKBOOK_CORRECTIONS.md`
- `RC1_SHUTDOWN_TEST_HOTFIX.md`
- `ORIGIN_FIX_v0.9.127.md`
- `RC1_BPI_CLEAN_PACKAGE.md`
- `RC2_EMPLOYEE_TIMESHEET_APPROVAL.md`
- `RC1_WELCOME_RESEND.md`
- `todo.md`
- `MULTI-CLOCK.md`
- `LITE_1_9_2_RELEASE.md`
- `PAY_PERIODS_v0.9.130-r16.md`
- `RC1_ANNUAL_VACATION_REPORT.md`
- `CLEANUP_REPORT.md`
- `RC1_CORRECTIONS_STATUS.md`
- `RC2_OVERNIGHT_SCHEDULES.md`
- `RC1_GUI_CORRECTIONS.md`
- `RC2_APPROVAL_SCHEDULE_HOTFIX.md`
- `BUILD-NOTES.txt`
- `RC1_FIX_TESTS.txt`
- `RC1_MANIFEST.txt`
- `docs/README_NOTE_LINK.md`
- `docs/README_NTP.md`
- `docs/README_ATTENDANCE.md`
- `docs/README_SMTP.md`
- `docs/README_EMPLOYEE_CARDS.md`
- `docs/GUI_COPY_AND_FLAGS.md`
- `docs/README_MANAGER_CARDS.md`
- `docs/README_MANAGER_LOGIN.md`
- `docs/CARD_UID_SAFETY.md`
- `docs/README_SCHEDULES.md`
- `docs/oled-screens-v0.9.65.png`
