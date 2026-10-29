#ifndef COMMS_WARNING_H
#define COMMS_WARNING_H
/* Monotonic milliseconds: no wall-clock/NTP adjustment can trigger a chirp.
   Busy reminders remain due until the next quiet moment. No catch-up bursts. */
struct comms_warning {long long seen_ok,next;};
static int comms_warning_due(struct comms_warning*s,long long now,long long last_ok,int busy){
 if(last_ok>s->seen_ok){s->seen_ok=last_ok;s->next=last_ok+180000;}
 if(busy||now<s->next)return 0;
 s->next=now+300000;return 1;
}
#endif
