from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def test_lateness_separator_in_email_and_csv():
    s=(ROOT/'src/payroll_timesheet.h').read_text()
    assert 'const char *late_marker=strstr(reason," Late arrival:")' in s
    assert 'sqlite3_mprintf("%.*s; %s"' in s
    assert 'sqlite3_free(formatted_reason)' in s
    assert 'payroll_csv_value(csv,reason)' in s

def test_absence_privacy_hint_and_workbook_fill():
    import csv, io, tempfile, zipfile, importlib.util, xml.etree.ElementTree as ET
    source=(ROOT/'src/reconciliation.inc').read_text()
    assert "Don't enter medical details." in source
    spec=importlib.util.spec_from_file_location('workbook',ROOT/'timeclock-payroll-xlsx.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    contents=io.StringIO();writer=csv.writer(contents)
    writer.writerow(['Employee','RFID ID','Date','IN','OUT','Worked minutes','Reason / correction','Status','Worked hours','Employee ID'])
    writer.writerow(['Alex','AA11','2026-10-07','','',0,'','Recorded absence: Sick','0.00',1])
    with tempfile.TemporaryDirectory() as folder:
        output=Path(folder)/'payroll.xlsx'
        module.make(contents.getvalue(),output,'2026-10-01','2026-10-15')
        with zipfile.ZipFile(output) as archive:
            sheet=ET.fromstring(archive.read('xl/worksheets/sheet2.xml'))
            ns={'s':'http://schemas.openxmlformats.org/spreadsheetml/2006/main'}
            row=sheet.find('.//s:sheetData/s:row[@r="5"]',ns)
            cells={c.get('r').rstrip('0123456789'):c for c in row}
            assert 'Recorded absence: Sick' in ''.join(cells['G'].itertext())
            assert cells['A'].get('s')=='6'
            assert cells['D'].find('s:v',ns) is None
