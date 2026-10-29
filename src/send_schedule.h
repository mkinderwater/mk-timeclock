#ifndef SEND_SCHEDULE_H
#define SEND_SCHEDULE_H
#include "common.h"
/* Local wall-clock schedules. A missed time catches up; queue keys deduplicate. */
static inline int send_schedule_due(const struct tm *local,const char *date,const char *at){
 if(!time_valid(at))return 0;
 char today[11],clock[6];strftime(today,sizeof today,"%Y-%m-%d",local);strftime(clock,sizeof clock,"%H:%M",local);
 int day=strcmp(today,date);return day>0||(day==0&&strcmp(clock,at)>=0);
}
#endif
