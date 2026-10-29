#ifndef PERIOD_REVIEW_H
#define PERIOD_REVIEW_H
/* Any punch-required interval in the local period requires review. Existing
   punches also require review, even when history is incomplete. Before the
   first known mode, use that initial mode for backfilled historical leave. */
#define PERIOD_REVIEW_SQL(employee,starts,ends,pairs) \
 "(EXISTS(SELECT 1 FROM " pairs " ap WHERE ap.employee_id=" employee " AND date(ap.in_at,'localtime') BETWEEN " starts " AND " ends ") OR " \
 "coalesce((SELECT am.mode FROM employee_attendance_modes am WHERE am.employee_id=" employee " AND julianday(am.effective_at)<=julianday(" starts ",'utc') ORDER BY julianday(am.effective_at) DESC,am.rowid DESC LIMIT 1)," \
 "(SELECT am.mode FROM employee_attendance_modes am WHERE am.employee_id=" employee " ORDER BY julianday(am.effective_at),am.rowid LIMIT 1),'punch_required')='punch_required' OR " \
 "EXISTS(SELECT 1 FROM employee_attendance_modes am WHERE am.employee_id=" employee " AND am.mode='punch_required' AND julianday(am.effective_at)>julianday(" starts ",'utc') AND julianday(am.effective_at)<julianday(" ends ",'+1 day','utc')))"
/* Recorded leave in a local period: an uncancelled absence on a day in the
   period, or approved uncancelled time off overlapping it. One definition for
   every review and period-recovery query. */
#define LEAVE_ABSENCE_SQL(starts,ends) \
 "SELECT 1 FROM attendance_absences la WHERE la.cancelled_at IS NULL AND la.day BETWEEN " starts " AND " ends
#define LEAVE_TIMEOFF_SQL(starts,ends) \
 "SELECT 1 FROM timeoff_requests lt WHERE lt.status='approved' AND lt.cancelled_at IS NULL AND lt.starts<=" ends " AND lt.ends>=" starts
/* Any employee. */
#define PERIOD_LEAVE_SQL(starts,ends) \
 "(EXISTS(" LEAVE_ABSENCE_SQL(starts,ends) ") OR EXISTS(" LEAVE_TIMEOFF_SQL(starts,ends) "))"
/* One employee. */
#define EMPLOYEE_LEAVE_SQL(employee,starts,ends) \
 "(EXISTS(" LEAVE_ABSENCE_SQL(starts,ends) " AND la.employee_id=" employee ") OR " \
 "EXISTS(" LEAVE_TIMEOFF_SQL(starts,ends) " AND lt.employee_id=" employee "))"
/* Employee ids with recorded leave in the period. */
#define LEAVE_EMPLOYEES_SQL(starts,ends) \
 "SELECT employee_id FROM attendance_absences WHERE cancelled_at IS NULL AND day BETWEEN " starts " AND " ends \
 " UNION SELECT employee_id FROM timeoff_requests WHERE status='approved' AND cancelled_at IS NULL AND starts<=" ends " AND ends>=" starts
#endif
