"""Execute the actual server and mailer schedulers against a controlled local clock."""
import json,os,shlex,sqlite3,subprocess,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
common=r'''
#define _GNU_SOURCE
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
static time_t fixture_now;
static time_t fixture_time(time_t *out){if(out)*out=fixture_now;return fixture_now;}
#define time fixture_time
#define main clock_program_main
#include "@SOURCE@"
#undef main
#undef time
static void set_clock(const char *date,int dst){
 struct tm t={0};int y,m,d,h,n;sscanf(date,"%d-%d-%dT%d:%d",&y,&m,&d,&h,&n);
 t.tm_year=y-1900;t.tm_mon=m-1;t.tm_mday=d;t.tm_hour=h;t.tm_min=n;t.tm_isdst=dst;
 setenv("TZ","America/Edmonton",1);tzset();fixture_now=mktime(&t);
}
int main(int argc,char **argv){
 if(argc<3)return 2;set_clock(argv[2],argc>3?atoi(argv[3]):-1);
 @ACTION@
 return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    tmp=Path(tmp);runners={}
    for name,source,action,libs in [
        ('review','src/server.c','if(!init_db(argv[1]))return 3;review_auto_due();sqlite3_close(db);',['-lsqlite3','-lcrypt']),
        ('payroll','src/mailer.c','sqlite3 *db=NULL;if(sqlite3_open(argv[1],&db)!=SQLITE_OK)return 3;payroll_enqueue_due(db);sqlite3_close(db);',['-lsqlite3','-lcurl'])]:
        code=tmp/(name+'.c');code.write_text(common.replace('@SOURCE@',str(root/source)).replace('@ACTION@',action))
        exe=tmp/name
        subprocess.run([os.environ.get('CC','cc'),'-O2','-std=c11','-D_POSIX_C_SOURCE=200809L',*shlex.split(os.environ.get('CFLAGS','')),str(code),'-o',str(exe),*libs],check=True)
        runners[name]=exe
    dbpath=tmp/'clock.db'
    def run(name,date,dst=-1):subprocess.run([str(runners[name]),str(dbpath),date,str(dst)],check=True,capture_output=True,text=True)
    run('review','2026-10-21T07:00')
    with sqlite3.connect(dbpath) as db:
        db.execute("INSERT INTO employees(id,name,email,attendance_mode,vacation_days,created_at) VALUES(1,'Test Manager','manager@example.test','punch_required',NULL,'2020-01-01'),(2,'Worker','worker@example.test','vacation_only',15,'2020-01-01')")
        db.execute("INSERT INTO employee_roles VALUES(1,'manager')")
        db.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
        db.execute("INSERT INTO manager_cards VALUES('A1B2C3D4',1,1)")
        db.execute("UPDATE period_cycle_settings SET cycle='weekly'")
        db.execute("INSERT INTO payroll_settings(id,anchor_payday,lead_days,recipient,enabled,enabled_since,send_time) VALUES(1,'2026-10-21',0,'payroll@example.test',1,'2020-01-01','15:00')")
        db.execute("UPDATE review_send_settings SET days_after=1,send_time='08:00'")
        db.execute("INSERT INTO pay_periods(id,starts,ends,status) VALUES(100,'2026-10-07','2026-10-20','open')")
        db.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status) VALUES(2,'vacation','2026-10-20','2026-10-21','approved')")
    def events():
        with sqlite3.connect(dbpath) as db:return [row[0] for row in db.execute('SELECT event_id FROM email_queue ORDER BY id')]
    run('review','2026-10-21T07:59');assert not events()
    run('review','2026-10-21T08:00');assert events()==['auto-review:100:2','auto-review-summary:100'],events()
    with sqlite3.connect(dbpath) as db:
        row=db.execute("SELECT recipient,subject,body FROM email_queue WHERE event_id='auto-review:100:2'").fetchone()
        assert row[0]=='worker@example.test' and '2026-10-07' in row[1] and '2026-10-20' in row[1] and '/review/' in row[2]
        db.execute("UPDATE pay_periods SET status='closed' WHERE id=100")
    run('review','2026-10-21T09:00');assert len(events())==2  # restart never duplicates
    run('payroll','2026-10-21T14:59');assert len(events())==2
    run('payroll','2026-10-21T15:00');assert events()[-1]=='payroll-period:100',events()
    run('payroll','2026-10-21T17:00');assert len(events())==3
    with sqlite3.connect(dbpath) as db:
        row=db.execute("SELECT recipient,subject,csv_attachment FROM email_queue WHERE event_id='payroll-period:100'").fetchone()
        assert row[0]=='payroll@example.test' and '2026-10-20' in row[1]
        assert 'Annual vacation 2026: 2 of 15 days used' in row[2]
        assert row[2]==db.execute('SELECT csv FROM lite_period_snapshots WHERE period_id=100').fetchone()[0]
        # A manual request bypasses the selected time and automatic-enabled switch.
        db.execute("UPDATE payroll_settings SET enabled=0")
        db.execute("INSERT INTO pay_periods(id,starts,ends,status) VALUES(101,'2026-09-01','2026-09-15','closed')")
        db.execute('INSERT INTO payroll_manual_requests(period_id) VALUES(101)')
    run('payroll','2026-10-21T06:00');assert 'payroll-period:101' in events()
    with sqlite3.connect(dbpath) as db:
        db.execute("UPDATE review_send_settings SET days_after=0")
        db.execute("INSERT INTO pay_periods(id,starts,ends,status) VALUES(102,'2026-10-21','2026-10-21','open')")
    run('review','2026-10-21T07:59');assert 'auto-review:102:2' not in events()
    run('review','2026-10-21T08:00');assert 'auto-review:102:2' in events()  # zero means on period-end day
    with sqlite3.connect(dbpath) as db:
        db.execute("INSERT INTO pay_periods(id,starts,ends,status) VALUES(103,'2026-10-22','2026-10-22','open')")
        db.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status) VALUES(2,'vacation','2026-10-22','2026-10-22','approved')")
    run('review','2026-10-23T06:00');assert 'auto-review:103:2' in events()  # previous-day catch-up before today's send time
    # Spring-forward gap: 02:30 is reached at the first available later local time.
    with sqlite3.connect(dbpath) as db:
        db.execute("UPDATE review_send_settings SET send_time='02:30'")
        db.execute("INSERT INTO attendance_absences(employee_id,day,reason,manager_id) VALUES(2,'2026-03-08','Sick',1),(2,'2026-11-01','Sick',1)")
        db.execute("INSERT INTO pay_periods(id,starts,ends,status) VALUES(104,'2026-03-08','2026-03-08','open')")
        db.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status) VALUES(2,'vacation','2026-03-08','2026-03-08','approved')")
    run('review','2026-03-08T01:59');assert 'auto-review:104:2' not in events()
    run('review','2026-03-08T03:00');assert 'auto-review:104:2' in events()
    # Fall-back repeated hour: first occurrence sends; second occurrence is suppressed.
    with sqlite3.connect(dbpath) as db:
        db.execute("UPDATE review_send_settings SET send_time='01:30'")
        db.execute("INSERT INTO pay_periods(id,starts,ends,status) VALUES(105,'2026-11-01','2026-11-01','open')")
        db.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status) VALUES(2,'vacation','2026-11-01','2026-11-01','approved')")
    run('review','2026-11-01T01:30',1);assert 'auto-review:105:2' in events()
    count=len(events());run('review','2026-11-01T01:30',0);assert len(events())==count
print('PASS: actual schedulers with controlled local clock; 08:00 links then 15:00 payroll, zero-day options, before/at cutoff, restart dedup, past-day catch-up, manual bypass, correct recipients/source and DST gap/repeat')
