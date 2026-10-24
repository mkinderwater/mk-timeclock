"""Standalone acceptance checks for payroll XLSX and MIME attachment."""
import csv, subprocess, tempfile, zipfile, xml.etree.ElementTree as ET
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]

def test_excel_content():
    with tempfile.TemporaryDirectory() as tmp:
        csvpath=Path(tmp)/'pay.csv';xlsx=Path(tmp)/'pay.xlsx'
        with csvpath.open('w',newline='',encoding='utf-8') as f:
            writer=csv.writer(f)
            writer.writerow(['Employee','RFID ID','Date','IN','OUT','Worked minutes','Reason / correction','Status','Worked hours','Employee ID'])
            writer.writerows([
              ['Alex Lee','A1','2026-10-01','08:00','12:00','240','','Complete','4.00','1'],
              ['Alex Lee','A2','2026-10-01','13:00','17:00','240','','Complete','4.00','1'],
              ['Alex Lee','A2','2026-10-02','','','0','','Approved vacation','0.00','1'],
              ['Alex Lee','B1','2026-10-01','08:00','09:00','60','','Complete','1.00','2']])
        subprocess.run(['python3',str(ROOT/'timeclock-payroll-xlsx.py'),str(csvpath),str(xlsx),'2026-10-01','2026-10-15'],check=True)
        with zipfile.ZipFile(xlsx) as z:
            assert z.testzip() is None
            for name in z.namelist():
                if name.endswith(('.xml','.rels')):ET.fromstring(z.read(name))
            book=z.read('xl/workbook.xml').decode()
            assert 'name="Summary"' in book and 'name="Alex Lee"' in book and 'name="Alex Lee (2)"' in book
            sheet=z.read('xl/worksheets/sheet2.xml').decode()
            assert 'SUM(D5:D7)' in sheet
            assert '<v>480</v>' in sheet
            assert '<v>8.0</v>' in sheet or '<v>8</v>' in sheet
            assert 'Approved vacation' in sheet

def test_binary_mime():
    with tempfile.TemporaryDirectory() as tmp:
        root=Path(tmp);harness=root/'harness.c';binary=root/'harness'
        source=str(ROOT/'src/mailer.c')
        harness.write_text('#define main mailer_original_main\n#include "'+source+'"\n#undef main\nint main(int argc,char **argv){(void)argc;\n'
          'FILE*f=fopen(argv[1],"rb");if(!f)return 2;fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);'
          'unsigned char*b=malloc((size_t)n);if(!b)return 3;if(fread(b,1,(size_t)n,f)!=(size_t)n){free(b);fclose(f);return 5;}fclose(f);'
          'char *m=build_message("clock@example.com","payroll@example.com","October Payroll","Hello",b,(size_t)n,"Payroll_2026-10-01_to_2026-10-15.xlsx");'
          'if(!m)return 4;fwrite(m,1,strlen(m),stdout);free(m);free(b);return 0;}',encoding='utf-8')
        subprocess.run(['cc','-O2','-Wall','-Wextra','-Werror','-std=c11','-D_POSIX_C_SOURCE=200809L',str(harness),'-lsqlite3','-lcurl','-o',str(binary)],check=True)
        from email.parser import BytesParser
        from email import policy
        csvpath=root/'s.csv';xlsx=root/'s.xlsx'
        csvpath.write_text('Employee,RFID ID,Date,IN,OUT,Worked minutes,Reason / correction,Status,Worked hours,Employee ID\nA,A1,2026-10-01,08:00,16:00,480,,Complete,8.00,1\n')
        subprocess.run(['python3',str(ROOT/'timeclock-payroll-xlsx.py'),str(csvpath),str(xlsx),'2026-10-01','2026-10-15'],check=True)
        msg=BytesParser(policy=policy.default).parsebytes(subprocess.check_output([str(binary),str(xlsx)]))
        assert msg.is_multipart()
        parts=list(msg.iter_attachments());assert len(parts)==1
        assert parts[0].get_filename()=='Payroll_2026-10-01_to_2026-10-15.xlsx'
        assert parts[0].get_payload(decode=True)==xlsx.read_bytes()
