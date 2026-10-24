import subprocess,tempfile,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def test_mailer_invokes_xlsx_worker():
    with tempfile.TemporaryDirectory() as d:
        p=Path(d);c=p/'check.c';exe=p/'check';out=p/'book.xlsx'
        c.write_text('#define main other_main\n#include "'+str(ROOT/'src/mailer.c')+'"\n#undef main\n'
        'int main(int argc,char**argv){(void)argc;unsigned char *b=NULL;size_t n=0;'
        'if(make_payroll_book("Employee,RFID ID,Date,IN,OUT,Worked minutes,Reason / correction,Status,Worked hours,Employee ID\\nSam Ortiz,E115305B,2026-10-01,08:00,16:00,480,,Complete,8.00,12\\n",'
        '"2026-10-01","2026-10-15",&b,&n)!=0)return 2;'
        'FILE*f=fopen(argv[1],"wb");if(!f)return 3;fwrite(b,1,n,f);fclose(f);free(b);return 0;}')
        subprocess.run(['cc','-O2','-Wall','-Wextra','-Werror','-std=c11','-D_POSIX_C_SOURCE=200809L',str(c),'-lsqlite3','-lcurl','-o',str(exe)],check=True)
        subprocess.run([str(exe),str(out)],cwd=ROOT,check=True)
        with zipfile.ZipFile(out) as z:
            assert z.testzip() is None
            assert b'Sam Ortiz' in z.read('xl/worksheets/sheet2.xml')
