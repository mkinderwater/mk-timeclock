import pathlib, sqlite3, subprocess, tempfile
ROOT=pathlib.Path(__file__).resolve().parents[1]
def test_timesheet_shared_source():
    code=r'''
#include <stdio.h>
#include <sqlite3.h>
#include <string.h>
#include "payroll_timesheet.h"
int main(void){
 sqlite3 *db=NULL;sqlite3_open(":memory:",&db);
 const char *schema="CREATE TABLE employees(id INTEGER PRIMARY KEY,name TEXT,active INTEGER DEFAULT 1);"
 "CREATE TABLE effective_punches(employee_id INTEGER,id TEXT,at TEXT,kind TEXT,manual INTEGER);"
 "CREATE TABLE swipes(event_id TEXT,card_uid TEXT);"
 "CREATE TABLE cards(uid TEXT,employee_id INTEGER,active INTEGER);"
 "CREATE TABLE attendance_events(event_id TEXT,exception TEXT,late_minutes INTEGER);"
 "CREATE TABLE swipe_notes(event_id TEXT,note TEXT);"
 "CREATE TABLE attendance_absences(id INTEGER PRIMARY KEY,employee_id INTEGER,day TEXT,reason TEXT,note TEXT,starts TEXT,ends TEXT,cancelled_at TEXT);"
 "CREATE TABLE statutory_holidays(day TEXT,name TEXT);"
 "CREATE TABLE schedule_profiles(employee_id INTEGER,pay_type TEXT,weekday_mask INTEGER);"
 "CREATE TABLE schedule_shifts(employee_id INTEGER,work_date TEXT);"
 "CREATE TABLE timeoff_requests(id INTEGER PRIMARY KEY,employee_id INTEGER,starts TEXT,ends TEXT,kind TEXT,status TEXT,cancelled_at TEXT);"
 "CREATE TABLE punch_corrections(id INTEGER PRIMARY KEY,event_id TEXT,manual_id TEXT,action TEXT,reason TEXT);"
 "INSERT INTO employees(id,name) VALUES(1,'Test Employee');"
 "INSERT INTO cards VALUES('E115305B',1,1);"
 "INSERT INTO swipes VALUES('a','E115305B'),('c','E115305B');"
 "INSERT INTO effective_punches VALUES(1,'a','2026-10-01T08:00:00Z','IN',0);"
 "INSERT INTO effective_punches VALUES(1,'b','2026-10-01T12:00:00Z','OUT',0);"
 "INSERT INTO effective_punches VALUES(1,'c','2026-10-01T13:00:00Z','IN',0);"
 "INSERT INTO effective_punches VALUES(1,'d','2026-10-01T17:00:00Z','OUT',0);"
 "INSERT INTO attendance_events(event_id,exception) VALUES('a','medical appointment');";
 if(sqlite3_exec(db,schema,NULL,NULL,NULL)!=SQLITE_OK){fprintf(stderr,"schema: %s\n",sqlite3_errmsg(db));return 1;}
 char *body=NULL,*csv=NULL;
 if(!payroll_build_timesheet(db,"2026-10-01","2026-10-15",&body,&csv)){fprintf(stderr,"timesheet: %s\n",sqlite3_errmsg(db));return 2;}
 if(!strstr(body,"Test Employee")||!strstr(body,"8h 00m")||!strstr(body,"medical appointment"))return 3;
 if(!strstr(body,"RFID ID: E115305B")||!strstr(csv,"E115305B"))return 5;
 if(!strstr(csv,"medical appointment")||!strstr(csv,",240,"))return 4;
 puts(body);sqlite3_free(body);sqlite3_free(csv);sqlite3_close(db);return 0;
}
'''
    with tempfile.TemporaryDirectory() as td:
        src=pathlib.Path(td)/'check.c';exe=pathlib.Path(td)/'check';src.write_text(code)
        subprocess.run(['cc','-Wall','-Wextra','-Werror','-std=c11','-I',str(ROOT/'src'),str(src),'-lsqlite3','-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True,capture_output=False,text=True)
