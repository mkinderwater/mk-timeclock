"""Real converter: employee blocks, internal links, banding and filter metadata."""
import csv,hashlib,subprocess,tempfile,zipfile,xml.etree.ElementTree as ET
from collections import Counter
from decimal import Decimal
from pathlib import Path
root=Path(__file__).resolve().parents[1]
columns=['Employee','RFID ID','Date','IN','OUT','Worked minutes','Reason / correction','Status','Worked hours','Employee ID']
records=[
 ['Zoe & Co','ZZ','2026-10-02','09:00','10:00','60','','Complete','1.00','30'],
 ['Same Name','','','','','0','Reviewed by manager','Manager override without employee confirmation','','20'],
 ['Alice','','','','','0','','Previous approved vacation day: 2026-09-30 (reference only)','','1'],
 ['Same Name','S2','2026-10-01','08:00','09:00','60','','Complete','1.00','20'],
 ['Zoe & Co','','','','','0','','Previous approved vacation day: 2026-09-29 (reference only)','','30'],
 ['Same Name','S1','2026-10-03','08:00','09:30','90','','Complete','1.50','10'],
 ['Alice','AA','2026-10-05','09:00','10:00','60','Correction confirmed','Complete - manager correction','1.00','1'],
 ['Zoe & Co','ZZ','2026-10-02','08:00','08:30','30','','Complete','0.50','30'],
 ['Same Name','','2026-10-02','','','0','','Approved vacation','0.00','10'],
 ['Vacation Reference Only','','','','','0','','Previous approved vacation day: 2026-09-28 (reference only)','','88'],
 ['Reference Only','','','','','0','Agreed leave context','Manager override without employee confirmation','','7'],
]
visible_records=[r for r in records if not r[7].startswith('Previous approved vacation day:')]
def data_rows(z,sheet):
    tree=ET.fromstring(z.read(f'xl/worksheets/sheet{sheet}.xml'))
    rows=[]
    for row in tree.find('{*}sheetData'):
        rows.append([(c.findtext('{*}is/{*}t','') if c.get('t')=='inlineStr' else c.findtext('{*}v','')) for c in row])
    return tree,rows
with tempfile.TemporaryDirectory() as tmp:
    inp=Path(tmp)/'source.csv';out=Path(tmp)/'output.xlsx'
    def convert(rows):
        with inp.open('w',encoding='utf-8',newline='') as f:
            writer=csv.writer(f);writer.writerow(columns);writer.writerows(rows)
        before=hashlib.sha256(inp.read_bytes()).digest()
        subprocess.run(['python3',str(root/'timeclock-payroll-xlsx.py'),str(inp),str(out),'2026-10-01','2026-10-15'],check=True)
        assert before==hashlib.sha256(inp.read_bytes()).digest()
    convert(records)
    first=out.read_bytes()
    with zipfile.ZipFile(out) as z:
        summary,srows=data_rows(z,1);detail,drows=data_rows(z,2)
        assert [e.get('name') for e in ET.fromstring(z.read('xl/workbook.xml')).find('{*}sheets')]==['Payroll Summary','Daily Detail']
        expected_ids=['1','7','10','20','30']
        assert [r[-1] for r in srows[4:]]==expected_ids
        assert [r[-1] for r in drows[4:]]==['1','7','10','10','20','20','30','30']
        for employee in expected_ids:
            rows=[r for r in drows[4:] if r[-1]==employee]
            assert [(not bool(r[0]),r[0],r[2]) for r in rows]==sorted((not bool(r[0]),r[0],r[2]) for r in rows)
        assert len(drows)==4+len(visible_records)  # No subtotal/group/header rows in the data.
        assert b'Previous approved vacation day' not in z.read('xl/worksheets/sheet1.xml')+z.read('xl/worksheets/sheet2.xml')
        assert not detail.findall('.//{*}f')
        assert all('outlineLevel' not in row.attrib for row in detail.find('{*}sheetData'))
        assert detail.find('{*}autoFilter').get('ref')=='A4:H12'
        links={e.get('ref'):e.get('location') for e in summary.find('{*}hyperlinks')}
        assert len(links)==len(expected_ids)
        for index,row in enumerate(srows[4:],5):
            target=links[f'A{index}'];assert target.startswith("'Daily Detail'!A")
            number=int(target.rsplit('A',1)[1]);destination=drows[number-1]
            assert (destination[1],destination[-1])==(row[0],row[-1])
            assert number==5 or drows[number-2][-1]!=row[-1]
        styles=ET.fromstring(z.read('xl/styles.xml'));xfs=list(styles.find('{*}cellXfs'))
        bands={}
        for xmlrow,row in zip(list(detail.find('{*}sheetData'))[4:],drows[4:]):
            fills={xfs[int(c.get('s','0'))].get('fillId') for c in xmlrow}
            assert len(fills)==1,(row,fills)
            fill=fills.pop();assert bands.setdefault(row[-1],fill)==fill
        filldefs=list(styles.find('{*}fills'))
        colors=[filldefs[int(bands[e])].find('{*}patternFill/{*}fgColor') for e in expected_ids]
        assert [c.get('rgb') if c is not None else None for c in colors]==[None,'FFEDF2F7',None,'FFEDF2F7',None]
        summary_totals={r[-1]:Decimal(r[1]) for r in srows[4:]}
        assert summary_totals=={'1':Decimal('1'),'7':Decimal('0'),'10':Decimal('1.5'),'20':Decimal('1'),'30':Decimal('1.5')}
        # Every source record appears exactly once with all detail fields retained.
        def source_record(r):
            return (r[2],r[0],r[3],r[4],str(Decimal(r[8])) if r[7].startswith('Complete') and r[3] and r[4] else '',r[7],r[6],r[9])
        def detail_record(r):
            return tuple([*r[:4],str(Decimal(r[4])) if r[4] else '',*r[5:]])
        assert Counter(map(source_record,visible_records))==Counter(map(detail_record,drows[4:]))
    subprocess.run(['python3',str(root/'timeclock-payroll-xlsx.py'),str(inp),str(out),'2026-10-01','2026-10-15'],check=True)
    assert out.read_bytes()==first
    convert([])
    with zipfile.ZipFile(out) as z:
        summary,srows=data_rows(z,1);detail,drows=data_rows(z,2)
        assert summary.find('{*}hyperlinks') is None
        assert detail.find('{*}autoFilter').get('ref')=='A4:H4'
    convert([records[2]])
    with zipfile.ZipFile(out) as z:
        summary,srows=data_rows(z,1);detail,drows=data_rows(z,2)
        assert summary.find('{*}hyperlinks') is None
        assert detail.find('{*}autoFilter').get('ref')=='A4:H4'
        assert all('Previous approved vacation day:' not in str(row) for row in srows+drows)
print('PASS: employee/date/IN order; per-employee undated tails; whole-block bands; exact AutoFilter; correct name links; unchanged records/totals/source; deterministic and empty exports')
