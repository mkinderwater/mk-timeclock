/* Shared payday/date rules. All dates are local calendar days at noon. */
static time_t payroll_day(const char*s){
 int y=0,m=0,d=0; if(sscanf(s,"%d-%d-%d",&y,&m,&d)!=3)return (time_t)-1;
 struct tm t={0};t.tm_year=y-1900;t.tm_mon=m-1;t.tm_mday=d;t.tm_hour=12;t.tm_isdst=-1;
 time_t x=mktime(&t);struct tm check;localtime_r(&x,&check);
 return check.tm_year==y-1900&&check.tm_mon==m-1&&check.tm_mday==d?x:(time_t)-1;
}
static time_t payroll_shift(time_t t,int delta){struct tm m;localtime_r(&t,&m);m.tm_mday+=delta;m.tm_isdst=-1;return mktime(&m);}
static inline int payroll_is_payday(const char*cadence,const char*anchor,time_t date){
 time_t ref=payroll_day(anchor);if(ref==(time_t)-1)return 0;
 struct tm a,b;localtime_r(&ref,&a);localtime_r(&date,&b);
 if(!strcmp(cadence,"semi_monthly")){
  if(a.tm_mday==1)return b.tm_mday==1||b.tm_mday==15;
  if(a.tm_mday==15){struct tm last=b;last.tm_mon++;last.tm_mday=0;last.tm_isdst=-1;time_t x=mktime(&last);localtime_r(&x,&last);return b.tm_mday==15||b.tm_mday==last.tm_mday;}
  return 0;
 }
 if(!strcmp(cadence,"monthly")){
  struct tm last=b;last.tm_mon++;last.tm_mday=0;last.tm_isdst=-1;time_t x=mktime(&last);localtime_r(&x,&last);
  return b.tm_mday==(a.tm_mday<last.tm_mday?a.tm_mday:last.tm_mday);
 }
 if(!strcmp(cadence,"weekly")||!strcmp(cadence,"biweekly")){
  int step=!strcmp(cadence,"weekly")?7:14;
  /* Compare calendar days, never elapsed wall-clock seconds across DST. */
  /* Gregorian civil-day serial, independent of timezone and DST. */
  int ya=a.tm_year+1900,ma=a.tm_mon+1,da=a.tm_mday;
  int yb=b.tm_year+1900,mb=b.tm_mon+1,db=b.tm_mday;
  ya-=ma<=2;yb-=mb<=2;
  int era_a=(ya>=0?ya:ya-399)/400, era_b=(yb>=0?yb:yb-399)/400;
  unsigned yoe_a=(unsigned)(ya-era_a*400),yoe_b=(unsigned)(yb-era_b*400);
  unsigned doy_a=(unsigned)((153*(ma+(ma>2?-3:9))+2)/5+da-1),doy_b=(unsigned)((153*(mb+(mb>2?-3:9))+2)/5+db-1);
  int serial_a=era_a*146097+(int)(yoe_a*365+yoe_a/4-yoe_a/100+doy_a);
  int serial_b=era_b*146097+(int)(yoe_b*365+yoe_b/4-yoe_b/100+doy_b);
  return (serial_b-serial_a)%step==0;
 }
 return 0;
}
