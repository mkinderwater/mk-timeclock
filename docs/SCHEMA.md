# Current schema ownership

The fresh Primary/server schema has **70 tables, 20 triggers, two views and seven explicit indexes**, unchanged from the accepted RC3 benchmark. SQLite's implicit indexes are not included in that count. The agent has its separate local queue/cache/rejection schema.

No whole table was proven obsolete in this pass. Reference counts are not deletion evidence: audit/history records, expiring authorization rows, configuration and queued operations have different lifecycles. All source swipes, review/export revisions and audit histories are retained.

The current schema is declared directly. Schedule mode, signoff context, email tokens, time-off fields and agent cache mode no longer depend on older-schema ALTER branches. This is not a historical database migration. Reopening a current database preserves its records. Never use a reset to make an upgrade appear to work.

Table definitions remain with their owning module. Signoff revision/delete history triggers have one definition in reconciliation. Email-token creation belongs to the server schema; the mailer retains its existing current-record token/identity repair behavior. Current attendance views are still rebuilt from their definitions at startup; this is distinct from a historical-schema upgrade.

## Tables

| Table | Definition owner |
| --- | --- |
| `account_authorization` | `src/server.c` |
| `account_change_audit` | `src/server.c` |
| `admin_export_audit` | `src/server.c` |
| `adoption_codes` | `src/server.c` |
| `adoption_requests` | `src/server.c` |
| `attendance_absence_audit` | `src/reconciliation.inc` |
| `attendance_absences` | `src/reconciliation.inc` |
| `attendance_events` | `src/server.c` |
| `backup_download` | `src/server.c` |
| `backup_download_audit` | `src/server.c` |
| `backup_restore` | `src/server.c` |
| `backup_restore_audit` | `src/server.c` |
| `backup_settings` | `src/server.c` |
| `card_ownership_history` | `src/server.c` |
| `card_replacement_audit` | `src/server.c` |
| `cards` | `src/server.c` |
| `clock_dim_settings` | `src/server.c` |
| `clock_duplicate_settings` | `src/server.c` |
| `clock_late_settings` | `src/server.c` |
| `clock_names` | `src/server.c` |
| `email_queue` | `src/server.c` |
| `email_retry_audit` | `src/server.c` |
| `email_templates` | `src/server.c` |
| `employee_active_history` | `src/server.c` |
| `employee_attendance_modes` | `src/server.c` |
| `employee_change_audit` | `src/server.c` |
| `employee_roles` | `src/server.c` |
| `employees` | `src/server.c` |
| `first_manager_setup` | `src/server.c` |
| `holiday_settings` | `src/server.c` |
| `lite_install_identity` | `src/mailer.c`, `src/server.c` |
| `lite_payroll_queue_failures` | `src/server.c` |
| `lite_period_snapshots` | `src/server.c` |
| `login_sessions` | `src/server.c` |
| `manager_authorization` | `src/server.c` |
| `manager_cards` | `src/server.c` |
| `manager_scan` | `src/server.c` |
| `oled_previews` | `src/server.c` |
| `pay_periods` | `src/reconciliation.inc` |
| `payroll_authorization` | `src/server.c` |
| `payroll_manual_requests` | `src/server.c` |
| `payroll_settings` | `src/server.c` |
| `period_approval_audit` | `src/reconciliation.inc` |
| `period_cycle_settings` | `src/reconciliation.inc` |
| `period_signoff_history` | `src/reconciliation.inc` |
| `period_signoffs` | `src/reconciliation.inc` |
| `punch_correction_history` | `src/reconciliation.inc` |
| `punch_corrections` | `src/reconciliation.inc` |
| `registered_clocks` | `src/server.c` |
| `rejected_uploads` | `src/server.c` |
| `review_link_audit` | `src/server.c` |
| `review_links` | `src/server.c` |
| `review_send_settings` | `src/server.c` |
| `schedule_authorization` | `src/server.c` |
| `schedule_change_audit` | `src/server.c` |
| `schedule_profiles` | `src/server.c` |
| `schedule_shifts` | `src/server.c` |
| `smtp_settings` | `src/server.c` |
| `statutory_holidays` | `src/server.c` |
| `swipe_note_history` | `src/server.c` |
| `swipe_note_links` | `src/note_link.inc` |
| `swipe_notes` | `src/server.c` |
| `swipes` | `src/server.c` |
| `system_actions` | `src/server.c` |
| `template_restore_audit` | `src/server.c` |
| `timeoff_audit` | `src/time_off.inc` |
| `timeoff_links` | `src/time_off.inc` |
| `timeoff_requests` | `src/time_off.inc` |
| `unknown_cards` | `src/server.c` |
| `user_accounts` | `src/server.c` |

## Views

`effective_punches`, `effective_shift_pairs`

## Explicit indexes

`attendance_absences_employee_day`, `attendance_events_employee`, `card_ownership_lookup`, `employee_active_lookup`, `swipes_employee_time`, `timeoff_employee_history`, `timeoff_pending`

The full baseline/candidate comparison verified column order, types, defaults, keys, foreign keys, index SQL, view SQL and trigger SQL. Fresh initialization/restart, token creation, signoff update/delete audit history and queue constraints have executable coverage in `tests/test_cleanup_schema.py`.

Cleanup2 adds only `smtp_settings.sender_name TEXT NOT NULL DEFAULT ''` for the shared outgoing display name. A current RC3 database receives that field on startup, preserving its existing settings. No tables are added.

Cleanup3 adds `employees.vacation_days REAL` (nullable, 0 through 366 in half-day increments), and `send_time TEXT NOT NULL DEFAULT '00:00'` on payroll_settings and review_send_settings. Current RC3 receives these fields on startup. No annual balance, accrual, allocation, enforcement or dispatch table is added. Frozen annual usage/allowance travels in the existing immutable CSV snapshot records.
