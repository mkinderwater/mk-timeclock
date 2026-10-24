"""Validate actual converter output for precision, identities, notes and bad text."""
import csv,io,subprocess,tempfile,zipfile,xml.etree.ElementTree as ET
from pathlib import Path
from decimal import Decimal
root=Path(__file__).resolve().parents[1]
columns=['Employee','RFID ID','Date','IN','OUT','Worked minutes','Reason / correction','Status','Worked hours','Employee ID']
rows=[
 ['Same Name','AA','2026-10-02','08:00','16:30','509','','Complete','8.50','101'],
 ['Same Name','AB','2026-10-03','08:00','09:00','59','','Complete','1.00','101'],
 ['Same Name','BB','2026-10-01','08:00','09:00','59','','Complete','1.00','202'],
 ['Same Name','','','','','0','Manager reviewed, said "yes"','Manager override without employee confirmation; mailbox unavailable','','101'],
 ['Same Name','','','','','0','','Previous approved vacation day: 2026-09-30 (reference only)','','202'],
 ['Invalid BYTE\x01','','2026-10-04','','','0','','Recorded absence: Sick','0','303'],
]
def parsed(z,index):
    tree=ET.fromstring(z.read(f'xl/worksheets/sheet{index}.xml'))
    return [[(c.findtext('{*}is/{*}t','') if c.get('t')=='inlineStr' else c.findtext('{*}v','')) for c in r.findall('{*}c')] for r in tree.find('{*}sheetData')]
with tempfile.TemporaryDirectory() as tmp:
    inp=Path(tmp)/'input.csv';out=Path(tmp)/'output.xlsx'
    text=io.StringIO(newline='');w=csv.writer(text);w.writerow(columns);w.writerows(rows)
    inp.write_bytes(text.getvalue().encode().replace(b'BYTE',b'bad\xfftext'))
    subprocess.run(['python3',str(root/'timeclock-payroll-xlsx.py'),str(inp),str(out),'2026-10-01','2026-10-15'],check=True)
    with zipfile.ZipFile(out) as z:
        for n in z.namelist():
            if n.endswith('.xml'):ET.fromstring(z.read(n))
        summary,detail=parsed(z,1),parsed(z,2)
        assert summary[3][-1]==detail[3][-1]=='Employee ID'
        people={r[-1]:r for r in summary[4:]}
        assert set(people)=={'101','202','303'},people
        assert people['101'][0]==people['202'][0]=='Same Name'
        assert Decimal(people['101'][1])==Decimal('9.50')
        assert Decimal(people['202'][1])==Decimal('1.00')
        assert 'Manager override without employee confirmation' in people['101'][6]
        assert 'Manager reviewed, said "yes"' in people['101'][6]
        assert 'mailbox unavailable' in people['101'][6]
        shifts=[r for r in detail[4:] if r[2] and r[3]]
        assert next(r for r in shifts if r[0]=='2026-10-02')[4]=='8.50'
        assert sum(Decimal(r[4]) for r in shifts if r[-1]=='101')==Decimal(people['101'][1])
        dates=[r[0] for r in detail[4:]]
        assert dates==['2026-10-04','2026-10-02','2026-10-03','','2026-10-01'],dates
        assert people['303'][0]=='Invalid bad\ufffdtext\ufffd',people['303']
print('PASS: saved decimal hours reconcile; same-name IDs visible; override notes retained; undated rows last per employee; invalid UTF-8/control characters produce valid XML')
