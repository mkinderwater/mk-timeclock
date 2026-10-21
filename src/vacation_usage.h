#ifndef VACATION_USAGE_H
#define VACATION_USAGE_H
/* One approved calendar date counts once, even with overlapping requests.
   Same weekday/dated-shift and holiday rules as the time-off request calendar. */
static int vacation_year_usage(sqlite3 *db,int employee,int year,int *used,double *allowed,int *specified){
 *used=0;*allowed=0;*specified=0;
 char first[16],last[16];snprintf(first,sizeof first,"%04d-01-01",year);snprintf(last,sizeof last,"%04d-12-31",year);
 const char *query=
 "WITH RECURSIVE days(day) AS ("
 " SELECT max(starts,?2) FROM timeoff_requests WHERE employee_id=?1 AND kind='vacation' AND status='approved' AND cancelled_at IS NULL AND starts<=?3 AND ends>=?2"
 " UNION SELECT date(day,'+1 day') FROM days WHERE day<?3 AND EXISTS(SELECT 1 FROM timeoff_requests t WHERE t.employee_id=?1 AND t.kind='vacation' AND t.status='approved' AND t.cancelled_at IS NULL AND date(day,'+1 day') BETWEEN t.starts AND t.ends)) "
 "SELECT e.vacation_days,(SELECT count(*) FROM days WHERE NOT EXISTS(SELECT 1 FROM statutory_holidays h WHERE h.day=days.day) AND "
 "CASE WHEN EXISTS(SELECT 1 FROM schedule_profiles p WHERE p.employee_id=?1 AND p.schedule_mode='recurring_weekly') THEN "
 "((SELECT weekday_mask FROM schedule_profiles WHERE employee_id=?1) & (1 << ((CAST(strftime('%w',days.day) AS INTEGER)+6)%7)))<>0 "
 "WHEN EXISTS(SELECT 1 FROM schedule_shifts sh JOIN schedule_profiles sp ON sp.employee_id=sh.employee_id AND sp.schedule_mode='dated_shifts' WHERE sh.employee_id=?1 AND sh.work_date=days.day) THEN 1 "
 "ELSE CAST(strftime('%w',days.day) AS INTEGER) BETWEEN 1 AND 5 END) FROM employees e WHERE e.id=?1";
 sqlite3_stmt *q=NULL;int ok=sqlite3_prepare_v2(db,query,-1,&q,NULL)==SQLITE_OK;
 if(ok){sqlite3_bind_int(q,1,employee);sqlite3_bind_text(q,2,first,-1,SQLITE_TRANSIENT);sqlite3_bind_text(q,3,last,-1,SQLITE_TRANSIENT);ok=sqlite3_step(q)==SQLITE_ROW;
 if(ok){*specified=sqlite3_column_type(q,0)!=SQLITE_NULL;*allowed=sqlite3_column_double(q,0);*used=sqlite3_column_int(q,1);}}
 sqlite3_finalize(q);return ok;
}
#endif
