# RC6

Consolidates RC5.24, including authenticated Slave adoption completion, rejected-key recovery and evenly spaced ruler labels.

Annual vacation CSV now places the employee summary first and compact booking details below. Remaining days decrease in vacation-date order, including future approved bookings. Duplicate dates, repeated annual totals and approval audit columns are removed. Employee IDs, manager notes, estimate warnings, year clipping and overlap deduplication remain. Blank allowances remain unspecified; negative balances remain visible. No schema changes.

See README.md and docs/OPERATIONS.md for report semantics and historical limits.

Validation: production exporter C compiled with warnings treated as errors and passed real SQLite/CSV acceptance scenarios. Setup key/recovery, ruler geometry and generator checks passed. Full application compilation and browser/hardware acceptance remain unverified because development headers and a Chromium executable are unavailable here.
