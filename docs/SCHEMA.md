# Current schema ownership

The fresh Primary/server schema has **73 tables**. The agent has its separate local queue/cache/rejection schema. Audit/history records, authorization rows, configuration and queued operations have different lifecycles; source swipes, review/export revisions and audit histories remain retained.

The current schema is declared by its owning modules. Startup applies required current column additions/removals while preserving attendance records and exports. Never reset a database to make an update appear to work.

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
| `daily_attendance_settings` | `src/server.c` (schema), `src/daily_attendance.inc` (settings/sender) |
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

Current schema and restart behavior are covered by `tests/test_cleanup_schema.py` and `tests/test_rc58_cleanup.py`.

RC5.8 removes `employees.review_timing` and the live `period_signoffs.unconfirmed` field. Existing nonzero confirmation flags are copied into `period_signoff_history` before the live column is removed. Historical signoff/audit tables remain; they are attendance evidence, not an active employee workflow.

`review_links.token` enables authenticated managers to reuse new links. Existing token hashes remain valid for employee access; a hash-only link requires explicit revocation before replacement. Bearer URLs also appear in the local email queue. Employee and period access remain manager-authenticated for link management and token-scoped read-only for employee views.

The old reminder and override email templates are removed. Current template customizations and stored report revisions are preserved.


RC5.11 simplifies `daily_attendance_settings` to `id`, `enabled` and `send_time`. Existing Off stays off; either old enabled daily mode maps to On. Removed daily catch-up fields are not retained. The old optional swipe receipt template is removed, while historical queue/audit evidence remains. No employee columns or new audit/dispatch tables are added.

`email_preparation_failures` stores one latest preparation failure per delivery event, employee/context/reason/time, and latest resolution time/manager. Deliverable messages remain exclusively in `email_queue`.

`clock_health` stores current per-clock heartbeat/backlog/recording state and the active outage episode. Email queue event keys deduplicate manager outage/recovery notices.

RC6.1 adds `clock_health.ip_address` and `clock_health.software_version`, both non-null text defaulting to blank. Current-schema initialization adds these active fields to existing databases. Authenticated heartbeats validate and replace the values; clock removal clears the health row through the existing lifecycle. No extra table or settings are added.
