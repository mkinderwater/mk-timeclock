"""Actual XLSX keeps Excel-safe summary text and complete per-row notes."""
import csv,subprocess,tempfile,zipfile,xml.etree.ElementTree as ET
from pathlib import Path
root=Path(__file__).resolve().parents[1]
columns=['Employee','RFID ID','Date','IN','OUT','Worked minutes','Reason / correction','Status','Worked hours','Employee ID']
notes=[f'Note {i}: '+ '\U0001f600'*200 for i in range(120)]
with tempfile.TemporaryDirectory() as td:
    inp=Path(td)/'source.csv';out=Path(td)/'workbook.xlsx'
    with inp.open('w',encoding='utf-8',newline='') as f:
        writer=csv.writer(f);writer.writerow(columns)
        for note in notes:writer.writerow(['Worker','AA','2026-10-02','08:00','09:00','60',note,'Complete','1.00','17'])
        writer.writerow(['Short','BB','2026-10-02','08:00','09:00','60','unchanged','Complete','1.00','18'])
    subprocess.run(['python3',str(root/'timeclock-payroll-xlsx.py'),str(inp),str(out),'2026-10-01','2026-10-15'],check=True)
    with zipfile.ZipFile(out) as z:
        def rows(number):
            tree=ET.fromstring(z.read(f'xl/worksheets/sheet{number}.xml'))
            return [[(c.findtext('{*}is/{*}t','') if c.get('t')=='inlineStr' else c.findtext('{*}v','')) for c in r.findall('{*}c')] for r in tree.find('{*}sheetData')]
        summary,detail=rows(1),rows(2)
        assert 'Hours sum rounded CSV shifts; email period totals use seconds and may differ slightly.' in summary[2][0]
        worker=next(r for r in summary[4:] if r[0]=='Worker')
        assert len(worker[6].encode('utf-16-le'))<=32767*2
        assert worker[6].endswith(' [Truncated; see Daily Detail.]')
        assert '\ufffd' not in worker[6]
        short=next(r for r in summary[4:] if r[0]=='Short')
        assert short[6]=='Complete: unchanged'
        actual=[r[6] for r in detail[4:] if r[-1]=='17']
        assert actual==notes
        assert worker[1]=='120.00'
print('PASS: rounding basis visible; summary capped within Excel limit including emoji; truncation marked; Daily Detail retains all notes')
