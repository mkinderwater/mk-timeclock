/* Shift only report dispatch, not nominal payday or attendance period.
   Holiday entries are maintained under Stat holidays. SQLite errors fail closed:
   retain the nominal date rather than silently assume there are no holidays. */
static inline time_t payroll_report_business_day(sqlite3 *db,time_t day,int enabled){
 if(!enabled||day==(time_t)-1)return day;
 time_t original=day;
 for(int i=0;i<21;i++){
  struct tm d;localtime_r(&day,&d);
  int holiday=0;
  if(d.tm_wday!=0&&d.tm_wday!=6){
   char iso[11];strftime(iso,sizeof iso,"%Y-%m-%d",&d);
   sqlite3_stmt *q=NULL;
   if(sqlite3_prepare_v2(db,"SELECT 1 FROM statutory_holidays WHERE day=?1 LIMIT 1",-1,&q,NULL)!=SQLITE_OK)return original;
   sqlite3_bind_text(q,1,iso,-1,SQLITE_TRANSIENT);
   int rc=sqlite3_step(q);holiday=rc==SQLITE_ROW;
   sqlite3_finalize(q);
   if(rc!=SQLITE_ROW&&rc!=SQLITE_DONE)return original;
  }
  if(d.tm_wday!=0&&d.tm_wday!=6&&!holiday)return day;
  day=payroll_shift(day,-1);
  if(day==(time_t)-1)return original;
 }
 return original;
}

/* Shared scheduled report deadline: the first payday strictly following the
   reporting period, less calendar-day lead and optionally shifted backward. */
static inline time_t payroll_report_deadline(sqlite3 *db,const char *cadence,const char *anchor,
                                              const char *period_end,int lead,int previous_business_day){
 time_t end=payroll_day(period_end);
 if(end==(time_t)-1||lead<0||lead>14)return (time_t)-1;
 for(int offset=1;offset<=70;offset++){
  time_t payday=payroll_shift(end,offset);
  if(payday==(time_t)-1)return (time_t)-1;
  if(payroll_is_payday(cadence,anchor,payday))
   return payroll_report_business_day(db,payroll_shift(payday,-lead),previous_business_day);
 }
 return (time_t)-1;
}
