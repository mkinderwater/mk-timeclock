#!/usr/bin/env python3
"""Acceptance of the single two-tab payroll workbook using real converter output."""
import csv, subprocess, tempfile, zipfile, xml.etree.ElementTree as ET
from pathlib import Path
from decimal import Decimal
root=Path(__file__).resolve().parent.parent
columns=['Employee','RFID ID','Date','IN','OUT','Worked minutes','Reason / correction','Status','Worked hours','Employee ID']
rows=[
 ['Sarah Chen','AB','2026-10-02','08:12','16:30','498','Late arrival: 12 min','Complete - late arrival','8.30','1'],
 ['Sarah Chen','AB','2026-10-12','','','','','Stat holiday: Thanksgiving','0','1'],
 ['David Miller','','2026-10-05','','','','Called in','Recorded absence: Sick','0','2'],
 ['Robert Taylor','','2026-10-13','','','','','Approved vacation','0','3'],
 ['Robert Taylor','','2026-10-14','','','','','Approved vacation','0','3'],
 ['Robert Taylor','','2026-10-15','','','','','Approved vacation','0','3'],
 ['Robert Taylor','','2026-10-06','','','','','Approved unpaid','0','3'],
]
with tempfile.TemporaryDirectory() as td:
    inp=Path(td)/'data.csv';out=Path(td)/'payroll.xlsx'
    with inp.open('w',newline='',encoding='utf-8') as f:
        w=csv.writer(f);w.writerow(columns);w.writerows(rows)
    subprocess.run(['python3',str(root/'timeclock-payroll-xlsx.py'),str(inp),str(out),'2026-10-01','2026-10-15'],check=True)
    with zipfile.ZipFile(out) as z:
        names=[n.attrib['name'] for n in ET.fromstring(z.read('xl/workbook.xml')).find('{*}sheets')]
        assert names==['Payroll Summary','Daily Detail'],names
        def parsed(sheet):
            r=ET.fromstring(z.read(f'xl/worksheets/sheet{sheet}.xml'))
            return [[(c.findtext('{*}is/{*}t') if c.attrib.get('t')=='inlineStr' else c.findtext('{*}v')) for c in row.findall('{*}c')] for row in r.find('{*}sheetData').findall('{*}row')]
        summary,detail=parsed(1),parsed(2)
        assert len(summary)==7,summary
        sarah=next(r for r in summary if r[0]=='Sarah Chen')
        david=next(r for r in summary if r[0]=='David Miller')
        robert=next(r for r in summary if r[0]=='Robert Taylor')
        assert Decimal(sarah[1])==Decimal('8.30') and sarah[4]=='1',sarah
        assert david[3]=='1',david
        assert robert[2]=='3' and robert[5]=='1',robert
        assert len(detail)==11, len(detail)
        assert any('late arrival' in str(r).lower() for r in detail)
print('PASS: exact two tabs; summary and details reconcile; classified leave stays separate')
