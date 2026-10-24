# RC3 cleanup17: Remove historical vacation commentary from Excel

- Omit Previous approved vacation day reference rows when rendering both workbook tabs. Employees with only that reference do not get an empty summary entry or detail link.
- Retain previous-vacation context in the payroll email body. Retain actual in-period vacation, annual usage/allowance, shifts, manager overrides and other records.
- Filter only during XLSX rendering. CSV generation and saved revisions remain unchanged; re-downloading an existing revision uses the cleaner presentation. Totals, two-tab structure, employee bands, sorting, links and AutoFilter remain consistent with the remaining records.
- No schema or attendance changes.

## Validation

All 52 maintained checks pass. Expanded converter and report-delivery assertions were rerun successfully; the delivery scenario passes 42/42 checks. See validation/cleanup17-suite.json and validation/cleanup17-revision-delivery.json. Converter checks cover removed references, a reference-only employee/workbook, retained manager overrides, unchanged hours, updated filter ranges and links, immutable input CSV and deterministic output. A compiled report-body check retains the previous-vacation section in the standard email. Saved-revision emails keep their existing brief attachment notice. SMTP acceptance checks omit the reference from the attached workbook and compare the attachment byte-for-byte with the saved revision download. No physical Banana Pi acceptance run.

## Upgrade

Extract the source and run sudo ./install-master.sh for a Primary or sudo ./install-slave.sh for a Secondary. No uninstall or database reset is needed.
