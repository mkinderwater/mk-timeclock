# RC3 cleanup8

Fix Save holidays reporting `r is not defined` after a successful API request. The shared API helper already parses the response and throws on errors; remove the obsolete second response parse. Existing holiday data is unchanged. Includes cleanup7 spacing and CSV help.

Validation: execute the actual save handler with successful and rejected API responses, checking submitted calendar and feedback. CSS lint and preflight pass. Compilation remains unavailable because SQLite headers are missing; browser/device and full-suite checks not run.
