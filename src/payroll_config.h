#ifndef MK_TIMECLOCK_PAYROLL_CONFIG_H
#define MK_TIMECLOCK_PAYROLL_CONFIG_H
/* Payroll report settings as one record. Used by the settings page, the
   automatic mailer and the issues page so all three read the same values. */
#include "common.h"

struct payroll_config{
 char cadence[20],anchor[20],recipient[260],send_time[8];
 int lead,enabled,previous_business_day;
};

/* Defaults apply when no settings row exists. Returns 1 if a row was read, 0 if missing, -1 on SQLite error. */
static inline int payroll_config_load(sqlite3 *d,struct payroll_config *c){
 memset(c,0,sizeof *c);
 snprintf(c->cadence,sizeof c->cadence,"semi_monthly");
 snprintf(c->send_time,sizeof c->send_time,"00:00");
 c->lead=3;
 sqlite3_stmt *q=db_prepare(d,
  "SELECT pc.cycle,coalesce(ps.anchor_payday,''),coalesce(ps.lead_days,3),coalesce(ps.recipient,''),"
  "coalesce(ps.enabled,0),coalesce(ps.send_time,'00:00'),coalesce(ps.previous_business_day,0) "
  "FROM period_cycle_settings pc LEFT JOIN payroll_settings ps ON ps.id=pc.id WHERE pc.id=1",NULL);
 int found=-1;
 int rc=q?sqlite3_step(q):SQLITE_ERROR;
 if(rc==SQLITE_ROW){
  snprintf(c->cadence,sizeof c->cadence,"%s",col_text(q,0));
  snprintf(c->anchor,sizeof c->anchor,"%s",col_text(q,1));
  c->lead=sqlite3_column_int(q,2);
  snprintf(c->recipient,sizeof c->recipient,"%s",col_text(q,3));
  c->enabled=sqlite3_column_int(q,4);
  snprintf(c->send_time,sizeof c->send_time,"%s",col_text(q,5));
  c->previous_business_day=sqlite3_column_int(q,6);
  found=1;
 }else if(rc==SQLITE_DONE)found=0;
 sqlite3_finalize(q);
 return found;
}
#endif
