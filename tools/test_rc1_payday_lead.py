"""Compile actual C payday calendar and verify lead-days-before logic."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
src=r'''
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include <stdio.h>
#include <string.h>
#include "payroll_dates.h"
static int check(const char*cycle,const char*anchor,const char*day,int lead,int expected){
 time_t x=payroll_day(day);
 int got=payroll_is_payday(cycle,anchor,payroll_shift(x,lead));
 if(got!=expected){fprintf(stderr,"FAIL %s %s %s lead=%d expected=%d got=%d\n",cycle,anchor,day,lead,expected,got);return 1;}
 return 0;
}
int main(void){int fails=0;
 fails+=check("biweekly","2026-10-23","2026-10-20",3,1);
 fails+=check("biweekly","2026-10-23","2026-10-21",3,0);
 fails+=check("biweekly","2026-10-23","2026-11-03",3,1);
 fails+=check("semi_monthly","2026-10-15","2026-10-12",3,1);
 fails+=check("semi_monthly","2026-10-15","2026-10-28",3,1);
 fails+=check("monthly","2026-10-23","2026-11-20",3,1);
 fails+=check("weekly","2026-10-23","2026-10-27",3,1);
 return fails?1:0;
}
'''
with tempfile.TemporaryDirectory() as t:
 p=Path(t)/'t.c';p.write_text(src)
 exe=Path(t)/'check'
 subprocess.run(['cc','-Wall','-Wextra','-Werror','-std=c11','-I',str(root/'src'),str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
print('PASS payday lead-date calendar cases')
