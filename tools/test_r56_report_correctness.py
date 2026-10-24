from pathlib import Path
import csv,io,importlib.util,tempfile,zipfile,xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1]
def test_absence_cancel_statement_reset_and_clock_times():
    src=(ROOT/'src/reconciliation.inc').read_text()
    segment=src[src.index('if(!strcmp(action,"cancel"))'):src.index('if(!form_value(body,"employee_id"')]
    assert 'sqlite3_changes(db)==1;}sqlite3_finalize(q);q=NULL;' in segment
    assert 'start[3]>=\'0\'&&start[3]<=\'5\'' in src
    assert 'end[3]>=\'0\'&&end[3]<=\'5\'' in src
    assert '<24' in src

def test_report_has_complete_row_fields_and_corrected_out():
    # Confirm the report's public field structure and current user-facing labels.
    source=(ROOT/'src/payroll_timesheet.h').read_text()
    assert 'Manager added OUT:' in source
    assert 'Late arrival:' in source
    header='Employee,RFID ID,Date,IN,OUT,Worked minutes,Reason / correction,Status,Worked hours,Employee ID\r\n'
    record='"Alex Chen","AA11",,,,0,"No response",Manager override without employee confirmation,,12\r\n'
    rows=list(csv.reader(io.StringIO(header+record)))
    assert len(rows[0])==len(rows[1])==10
    assert rows[1][5]=='0' and rows[1][6]=='No response' and rows[1][9]=='12'

def test_workbook_styles_no_false_vacation_and_preserves_employee():
    file=ROOT/'timeclock-payroll-xlsx.py'
    spec=importlib.util.spec_from_file_location('paybook56',file)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    output=io.StringIO();w=csv.writer(output)
    w.writerow(['Employee','RFID ID','Date','IN','OUT','Worked minutes','Reason / correction','Status','Worked hours','Employee ID'])
    w.writerows([
        ['Alex Chen','AA11','2026-10-01','08:12','16:00',468,'Late arrival: 12 min','Complete - late arrival','7.80','12'],
        ['Alex Chen','AA11','2026-10-02','08:00','16:00',480,'Manager added OUT: Forgot to tap out','Complete - manager correction','8.00','12'],
        ['Alex Chen','AA11','','','',0,'No response','Manager override without employee confirmation','','12'],
        ['Alex Chen','AA11','2026-10-12','','',0,'','Stat holiday: Thanksgiving','0.00','12'],
        ['Alex Chen','AA11','2026-10-08','','',0,'','Reference only: previous vacation','0.00','12'],
        ['Alex Chen','AA11','2026-10-14','','',0,'','Recorded absence: Medical appointment (13:00 to 15:00)','0.00','12'],
    ])
    with tempfile.TemporaryDirectory() as d:
        out=Path(d)/'sample.xlsx';module.make(output.getvalue(),out,'2026-10-01','2026-10-15')
        with zipfile.ZipFile(out) as z:
            book=z.read('xl/workbook.xml').decode()
            assert book.count('Alex Chen')==1
            xml=ET.fromstring(z.read('xl/worksheets/sheet2.xml'))
            ns={'x':'http://schemas.openxmlformats.org/spreadsheetml/2006/main'}
            rows=xml.findall('.//x:sheetData/x:row',ns)
            by_status={}
            for row in rows[4:]:
                cells={c.get('r').rstrip('0123456789'):c for c in row}
                status=''.join(cells.get('G',ET.Element('empty')).itertext())
                if status:by_status[status]=cells
            for label,expected in {
                'Complete - late arrival':'8',
                'Complete - manager correction':'9',
                'Manager override without employee confirmation':None,
                'Stat holiday: Thanksgiving':'7',
                'Reference only: previous vacation':None,
                'Recorded absence: Medical appointment (13:00 to 15:00)':'6',
            }.items():
                assert label in by_status,label
                assert by_status[label]['A'].get('s')==expected,(label,by_status[label]['A'].get('s'))
            assert sum(int(float(by_status[label]['D'].findtext('x:v',default='0',namespaces=ns))) for label in ['Complete - late arrival','Complete - manager correction'])==948
            assert 'Forgot to tap out' in z.read('xl/worksheets/sheet2.xml').decode()
