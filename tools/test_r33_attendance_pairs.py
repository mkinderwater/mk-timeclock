"""Simple attendance: OUT to IN gaps are not paid or classified as breaks."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]


def test_multiple_pairs_and_same_name_employees():
    code = r'''
#include <sqlite3.h>
#include <stdio.h>
#include <string.h>
#include "payroll_timesheet.h"
int main(void) {
 sqlite3 *db=NULL;
 if(sqlite3_open(":memory:",&db)!=SQLITE_OK)return 1;
 const char *sql=
 "CREATE TABLE employees(id INTEGER PRIMARY KEY,name TEXT,active INTEGER DEFAULT 1);"
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
 "INSERT INTO employees(id,name) VALUES(1,'Same Name'),(2,'Same Name');"
 "INSERT INTO cards VALUES('AABBCC11',1,1),('BBCCDD22',2,1);"
 "INSERT INTO swipes VALUES('a','AABBCC11'),('c','AABBCC11'),('e','AABBCC11'),('g','BBCCDD22');"
 "INSERT INTO effective_punches VALUES"
 "(1,'a','2026-10-01T08:00:00Z','IN',0),"
 "(1,'b','2026-10-01T10:00:00Z','OUT',0),"
 "(1,'c','2026-10-01T10:15:00Z','IN',0),"
 "(1,'d','2026-10-01T12:15:00Z','OUT',0),"
 "(1,'e','2026-10-01T12:45:00Z','IN',0),"
 "(1,'f','2026-10-01T16:30:00Z','OUT',0),"
 "(2,'g','2026-10-01T09:00:00Z','IN',0),"
 "(2,'h','2026-10-01T10:00:00Z','OUT',0);";
 if(sqlite3_exec(db,sql,NULL,NULL,NULL)!=SQLITE_OK){fprintf(stderr,"schema: %s\n",sqlite3_errmsg(db));return 2;}
 char *body=NULL,*csv=NULL;
 if(!payroll_build_timesheet(db,"2026-10-01","2026-10-15",&body,&csv)){fprintf(stderr,"timesheet: %s\n",sqlite3_errmsg(db));return 3;}
 /* 2:00 + 2:00 + 3:45 = 7:45, never 8:30 */
 if(!strstr(body,"Period total: 7h 45m"))return 4;
 if(!strstr(body,"Period total: 1h 00m"))return 5;
 if(strstr(body,"Period total: 8h 30m"))return 6;
 if(!strstr(csv,",120,")||!strstr(csv,",225,")||!strstr(csv,",60,"))return 7;
 int count=0;
 for(char *p=body;(p=strstr(p,"Period total:"));p+=13)count++;
 if(count!=2)return 8;
 if(!strstr(body,"RFID ID: AABBCC11")||!strstr(body,"RFID ID: BBCCDD22")||!strstr(csv,"AABBCC11")||!strstr(csv,"BBCCDD22"))return 9;
 sqlite3_free(body);sqlite3_free(csv);sqlite3_close(db);return 0;
}
'''
    with tempfile.TemporaryDirectory() as td:
        src = pathlib.Path(td)/'check.c'
        exe = pathlib.Path(td)/'check'
        src.write_text(code)
        subprocess.run(['cc','-Wall','-Wextra','-Werror','-std=c11','-I',str(ROOT/'src'),str(src),'-lsqlite3','-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
