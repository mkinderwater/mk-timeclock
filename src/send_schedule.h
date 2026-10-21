#ifndef SEND_SCHEDULE_H
#define SEND_SCHEDULE_H
/* Local wall-clock schedules. A missed time catches up; queue keys deduplicate. */
static inline int send_time_valid(const char *s){
 return s&&strlen(s)==5&&s[0]>='0'&&s[0]<='2'&&s[1]>='0'&&s[1]<='9'&&s[2]==':'&&s[3]>='0'&&s[3]<='5'&&s[4]>='0'&&s[4]<='9'&&strcmp(s,"24:00")<0;
}
static inline int send_schedule_due(const struct tm *local,const char *date,const char *at){
 if(!send_time_valid(at))return 0;
 char today[11],clock[6];strftime(today,sizeof today,"%Y-%m-%d",local);strftime(clock,sizeof clock,"%H:%M",local);
 int day=strcmp(today,date);return day>0||(day==0&&strcmp(clock,at)>=0);
}
#endif
