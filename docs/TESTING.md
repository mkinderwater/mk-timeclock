# Testing

Build the three programs, then run `python3 tests/run_current.py`. The current-suite manifest names each maintained check and runs scripts as separate processes with timeouts. It records every outcome and returns failure if any check fails. A selected suite is not the whole legacy test directory.

For browser checks, set `PLAYWRIGHT_MODULE` to a resolvable Playwright module and `CHROMIUM_PATH` to the Chromium executable, then run `python3 tests/test_rc3_mobile_browser.py`. Do not silently skip browser verification and claim it passed.

`tests/test_cleanup_schema.py` checks fresh/current schema, restart stability, message tokens and audit history. Before/after validation also compares the complete baseline and candidate schema, including columns, keys, triggers, views and indexes. No whole table is removed on reference-count evidence alone.

The obsolete older-schema upgrade fixture is retired in favor of fresh/current initialization and real schedule validation. Older workbook expectations (one tab per employee, old color IDs and totals in detail) are replaced by the current two-tab, decimal-hour, record-identity, grouping, filter and link tests. Version/UI-string assertions for the old r23 payroll are replaced by current dispatch SQL, actual SMTP revision delivery, frequency and payday-calendar checks.

Other historical tests and source guards remain until their coverage is reviewed or replaced. Do not run unrestricted pytest collection as a claim of a current clean suite: many old scripts execute at import and carry historical assumptions. See `tests/TEST_POLICY.md`.

Still required on the Banana Pi: representative-data pay-period-list timing against its 15-second deadline; RFID/OLED/buzzer/touch operation; offline dual-clock recovery; power interruption; installed backup/restore. The client deadline does not remove the server's serial-request bottleneck. No hardware or production SMTP acceptance is implied by development checks.

The old RC2 cross-period lock fixture is replaced by current closed-period upload coverage: overnight attribution, out-of-order exceptions, late valid uploads, stale status and immutable/revised CSV exports. RC3 deliberately permits audited post-close changes; do not restore the old lock to satisfy that fixture.

`test_rc3_sender_name.py` checks updating the current RC3 SMTP table, sender-name API validation and local SMTP delivery with an address-only envelope. It also checks enabling backup email through HTTP with None. The browser fixture checks the related controls and scan-button labels.

Cleanup3 acceptance adds `test_rc3_vacation_allowance.py`, `test_rc3_send_time_settings.py` and `test_rc3_send_times.py`. These check real HTTP request/save/export behavior, preserved saved revisions, above-allowance requests, settings upgrades, and actual scheduler functions under a controlled local clock. The scheduler test compiles source harnesses with the regular development dependencies; `CC` and `CFLAGS` can supply nonstandard header/library paths. Browser tests save/reload both time inputs through actual APIs, including a simulated authenticated manager-card scan, and submit vacation above a zero allowance. These do not claim physical RFID or on-device service timing acceptance.

The TLS-status permission test replays all installer commands targeting the TLS status directory in their actual order, so an early correct mode cannot mask a later regression. It checks private-key modes and unchanged certificate/key content. The actual non-root backend probe requires root and a usable secondary UID/GID mapping; single-ID containers explicitly report that probe unavailable while still exercising installer commands and final filesystem modes.

Cleanup5 extends the existing browser check to compare the vacation download button's computed font, dimensions, border radius and colors against the standard submit-button styles. It downloads the selected-year report and checks its filename and CSV content. The existing inline-script and CSS lint checks also pass. The latest full-suite result remains labelled as the cleanup4 baseline; targeted cleanup5 results are recorded separately.
