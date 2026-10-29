#!/usr/bin/env python3
"""Produce payroll XLSX using Python standard library only (no installed Excel needed)."""
import csv, io, sys, zipfile, textwrap, xml.sax.saxutils as sax
from decimal import Decimal, InvalidOperation

def esc(s):
    clean=''.join(c if c in '\t\n\r' or ' ' <= c <= '\ud7ff' or '\ue000' <= c <= '\ufffd' or '\U00010000' <= c <= '\U0010ffff' else '\ufffd' for c in str(s))
    return sax.escape(clean, {'"': '&quot;', "'": '&apos;'})

def cell(ref, value, style=0, numeric=False):
    style_attr = f' s="{style}"' if style else ''
    if numeric:
        return f'<c r="{ref}"{style_attr}><v>{value}</v></c>'
    return f'<c r="{ref}" t="inlineStr"{style_attr}><is><t>{esc(value)}</t></is></c>'

def col(n):
    s=''
    while n:
        n,k=divmod(n-1,26);s=chr(65+k)+s
    return s

def sheet(rows, widths, freeze=4, filter_range=None, links=None):
    out=['<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
         '<worksheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">',
         '<sheetViews><sheetView workbookViewId="0"><pane ySplit="%d" topLeftCell="A%d" activePane="bottomLeft" state="frozen"/></sheetView></sheetViews>'%(freeze,freeze+1),
         '<cols>'+''.join(f'<col min="{i}" max="{i}" width="{w}" customWidth="1"/>' for i,w in enumerate(widths,1))+'</cols><sheetData>']
    wrapped={0:5,2:6}
    for ri, row in enumerate(rows,1):
        lines=1
        if ri>=4:
            for ci, entry in enumerate(row):
                if entry and not entry[2]:
                    width=max(1,int(widths[ci]*.9))
                    lines=max(lines,sum(max(1,len(textwrap.wrap(line,width=width))) for line in str(entry[0]).split('\n')))
        height=f' ht="{min(409,lines*15+4)}" customHeight="1"' if ri>=4 else ''
        out.append(f'<row r="{ri}"{height}>')
        for ci, item in enumerate(row,1):
            if item is None:continue
            value,style,numeric=item
            out.append(cell(f'{col(ci)}{ri}',value,wrapped.get(style,style) if ri>=4 and not numeric else style,numeric))
        out.append('</row>')
    out.append('</sheetData>')
    if filter_range:
        out.append(f'<autoFilter ref="{esc(filter_range)}"/>')
    if links:
        out.append('<hyperlinks>'+''.join(f'<hyperlink ref="{esc(ref)}" location="{esc(location)}"/>' for ref,location in links.items())+'</hyperlinks>')
    out.append('<pageMargins left="0.4" right="0.4" top="0.6" bottom="0.6" header="0.3" footer="0.3"/></worksheet>')
    return ''.join(out).encode()

def item(v='',style=0,numeric=False):return (v,style,numeric)

def summary_notes(notes):
    text='; '.join(notes)
    # Count UTF-16 units conservatively, including supplementary characters.
    encoded=text.encode('utf-16-le')
    if len(encoded)<=32767*2:
        return text
    marker=' [Truncated; see Daily Detail.]'
    prefix=encoded[:(32767-len(marker))*2].decode('utf-16-le',errors='ignore')
    return prefix+marker

def worked_hours(row):
    # Preserve the frozen CSV's hours, including its rounding. Whole minutes
    # discard seconds and cannot reproduce this authoritative report value.
    try:
        hours=Decimal(row.get('Worked hours') or '')
    except InvalidOperation:
        raise ValueError('Invalid worked hours') from None
    if not hours.is_finite() or hours < 0:
        raise ValueError('Invalid worked hours')
    return hours

def make(raw, target, start, end, preview=False):
    # Historical vacation is email context, not a record for this pay period.
    # Filter during rendering so saved CSV revisions remain untouched.
    data=[r for r in csv.DictReader(io.StringIO(raw))
          if not (r.get('Status') or '').strip().casefold().startswith('previous approved vacation day:')]
    groups={}
    for r in data:
        person=r.get('Employee','').strip() or 'Unknown employee'
        uid=r.get('RFID ID','').strip()
        # Source export is consolidated; group on name + RFID (not name alone).
        # Manager records or card replacement can have different UIDs; they are handled
        # as separate groups rather than risking merging different people.
        groups.setdefault((person,r.get('Employee ID','') or uid),[]).append(r)
    # A single payroll handoff: one employee summary and one chronological detail.
    # Both tabs derive from the exact same frozen CSV source.
    def category(row):
        status=(row.get('Status') or '').strip()
        low=status.casefold()
        if low.startswith('stat holiday'): return 'stat'
        if low.startswith('recorded absence:'):
            reason=status.split(':',1)[1].strip().split(' (',1)[0].casefold()
            return 'sick' if reason=='sick' else 'other'
        if low.startswith('approved '):
            kind=low[9:].split(' - ',1)[0].strip()
            if kind.startswith('vacation'): return 'vacation'
            return 'other'
        return 'attendance' if low.startswith('complete') else 'other'

    summary=[[item('Payroll Summary',1)],
             [item('Attendance period',2),item(f'{start} to {end}')],
             [item('PREVIEW, not a saved revision' if preview else 'Recorded hours and leave days; payroll determines compensation. Hours sum rounded CSV shifts; email period totals use seconds and may differ slightly.')],
             [item('Employee',2),item('Worked hours',2),item('Vacation days',2),
              item('Sick days',2),item('Stat holidays',2),item('Other absence / leave days',2),item('Exceptions / notes',2),item('Annual vacation (used / allowed)',2),item('Employee ID',2)]]
    detail=[[item('Daily Detail',1)],
            [item('Attendance period',2),item(f'{start} to {end}')],
            [item('PREVIEW, not a saved revision' if preview else 'Complete attendance and recorded leave; no automatic pay calculations.')],
            [item('Date',2),item('Employee',2),item('IN',2),item('OUT',2),
             item('Worked hours',2),item('Type / status',2),item('Notes / corrections',2),item('Employee ID',2)]]
    employees=sorted(groups.items(),key=lambda row:(row[0][0].casefold(),row[0][1]))
    for (person,emp_id),records in employees:
        hours=Decimal(0); vacation=0;sick=0;stat=0;other=0;exceptions=[]
        for r in records:
            kind=category(r);status=r.get('Status','');note=r.get('Reason / correction','')
            completed=kind=='attendance' and bool(r.get('Date') and r.get('IN') and r.get('OUT'))
            if completed:
                try: hours+=worked_hours(r)
                except ValueError: exceptions.append('Invalid worked hours')
            elif kind=='vacation':vacation+=1
            elif kind=='sick':sick+=1
            elif kind=='stat':stat+=1
            elif kind=='other' and (status.lower().startswith(('recorded absence:','approved '))):other+=1
            if note or any(t in status.casefold() for t in ('late arrival','check','missing','unconfirmed','manager override','without employee confirmation','clocked in, no out yet','long shift')):
                marker=': '.join(x.strip() for x in (status,note) if x.strip())
                if marker and marker not in exceptions:exceptions.append(marker)
        summary.append([item(person,9),item(hours,4,True),
                        item(vacation,3,True),item(sick,3,True),item(stat,3,True),
                        item(other,3,True),item(summary_notes(exceptions)),item('; '.join(r.get('Status','').removeprefix('Annual vacation ') for r in records if r.get('Status','').startswith('Annual vacation '))),item(records[0].get('Employee ID',''))])
    first_rows={}
    for block,((person,emp_id),records) in enumerate(employees):
        first_rows[(person,emp_id)]=len(detail)+1
        shade=7 if block%2 else 0
        hour_style=8 if block%2 else 4
        for r in sorted(records,key=lambda r:(not bool(r.get('Date')),r.get('Date') or '',r.get('IN') or '')):
            kind=category(r);status=r.get('Status','');note=r.get('Reason / correction','')
            complete=kind=='attendance' and bool(r.get('IN') and r.get('OUT'))
            hours=None
            if complete:
                try:hours=worked_hours(r)
                except ValueError:note='; '.join(x for x in (note,'Invalid worked hours') if x)
            detail.append([item(r.get('Date',''),shade),item(person,shade),
                           item(r.get('IN',''),shade),item(r.get('OUT',''),shade),
                           item(hours,hour_style,True) if hours is not None else item('',shade),
                           item(status,shade),item(note,shade),item(r.get('Employee ID',''),shade)])

    if len(summary)==4:summary.append([item('No records for this pay period')])
    if len(detail)==4:detail.append([item('No recorded shifts or leave for this pay period')])
    links={f'A{row}':f"'Daily Detail'!A{first_rows[key]}" for row,(key,records) in enumerate(employees,5)}
    filter_range=f'A4:H{4+len(data)}'
    sheets=[('Payroll Summary',sheet(summary,[33,20,20,18,19,28,65,42,16],links=links)),
            ('Daily Detail',sheet(detail,[18,33,15,15,20,44,70,16],filter_range=filter_range))]
    names=[name for name,_ in sheets]
    ctype=['<?xml version="1.0" encoding="UTF-8" standalone="yes"?>',
           '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">',
           '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>',
           '<Default Extension="xml" ContentType="application/xml"/>',
           '<Override PartName="/xl/workbook.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml"/>',
           '<Override PartName="/xl/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml"/>']
    for i in range(len(sheets)):ctype.append(f'<Override PartName="/xl/worksheets/sheet{i+1}.xml" ContentType="application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml"/>')
    ctype.append('</Types>')
    book='<?xml version="1.0" encoding="UTF-8" standalone="yes"?><workbook xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main" xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships"><sheets>'+''.join(f'<sheet name="{esc(name)}" sheetId="{i}" r:id="rId{i}"/>' for i,name in enumerate(names,1))+'</sheets><calcPr fullCalcOnLoad="1"/></workbook>'
    rels='<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'+''.join(f'<Relationship Id="rId{i}" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet" Target="worksheets/sheet{i}.xml"/>' for i in range(1,len(sheets)+1))+'<Relationship Id="rIdStyles" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/></Relationships>'
    fonts=['<font><sz val="11"/><name val="Aptos"/></font>',
           '<font><b/><sz val="15"/><color rgb="FF193B55"/></font>',
           '<font><b/><sz val="11"/><color rgb="FF193B55"/></font>',
           '<font><u/><sz val="11"/><name val="Aptos"/><color rgb="FF0563C1"/></font>']
    fills=['<fill><patternFill patternType="none"/></fill>',
           '<fill><patternFill patternType="gray125"/></fill>',
           '<fill><patternFill patternType="solid"><fgColor rgb="FFEDF2F7"/><bgColor indexed="64"/></patternFill></fill>']
    xfs=['<xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"/>',
         '<xf numFmtId="0" fontId="1" fillId="0" borderId="0" xfId="0"/>',
         '<xf numFmtId="0" fontId="2" fillId="0" borderId="0" xfId="0"/>',
         '<xf numFmtId="1" fontId="0" fillId="0" borderId="0" xfId="0"/>',
         '<xf numFmtId="2" fontId="0" fillId="0" borderId="0" xfId="0"/>',
         '<xf numFmtId="0" fontId="0" fillId="0" borderId="0" xfId="0"><alignment wrapText="1" vertical="top"/></xf>',
         '<xf numFmtId="0" fontId="2" fillId="0" borderId="0" xfId="0"><alignment wrapText="1" vertical="top"/></xf>',
         '<xf numFmtId="0" fontId="0" fillId="2" borderId="0" xfId="0"><alignment wrapText="1" vertical="top"/></xf>',
         '<xf numFmtId="2" fontId="0" fillId="2" borderId="0" xfId="0"/>',
         '<xf numFmtId="0" fontId="3" fillId="0" borderId="0" xfId="0"><alignment wrapText="1" vertical="top"/></xf>']
    styles='<?xml version="1.0" encoding="UTF-8" standalone="yes"?><styleSheet xmlns="http://schemas.openxmlformats.org/spreadsheetml/2006/main">' + f'<fonts count="{len(fonts)}">'+''.join(fonts)+'</fonts>'+f'<fills count="{len(fills)}">'+''.join(fills)+'</fills>'+'<borders count="1"><border/></borders><cellStyleXfs count="1"><xf numFmtId="0" fontId="0" fillId="0" borderId="0"/></cellStyleXfs>'+f'<cellXfs count="{len(xfs)}">'+''.join(xfs)+'</cellXfs></styleSheet>'
    with zipfile.ZipFile(target,'w',zipfile.ZIP_DEFLATED,compresslevel=5) as z:
        def fixed_write(name, data):
            info=zipfile.ZipInfo(name, date_time=(2020,1,1,0,0,0))
            info.compress_type=zipfile.ZIP_DEFLATED
            z.writestr(info,data,compress_type=zipfile.ZIP_DEFLATED,compresslevel=5)
        fixed_write('[Content_Types].xml',''.join(ctype))
        fixed_write('_rels/.rels','<?xml version="1.0" encoding="UTF-8"?><Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships"><Relationship Id="rId1" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="xl/workbook.xml"/></Relationships>')
        fixed_write('xl/workbook.xml',book);fixed_write('xl/_rels/workbook.xml.rels',rels);fixed_write('xl/styles.xml',styles)
        for i,(_,content) in enumerate(sheets,1):fixed_write(f'xl/worksheets/sheet{i}.xml',content)

if __name__=='__main__':
    if len(sys.argv) not in (5,6):sys.exit('usage: xlsx.py input.csv output.xlsx start-date end-date [--preview]')
    if len(sys.argv)==6 and sys.argv[5]!='--preview':sys.exit('unknown option')
    with open(sys.argv[1],encoding='utf-8-sig',errors='replace',newline='') as f: source=f.read()
    make(source,sys.argv[2],sys.argv[3],sys.argv[4],preview=len(sys.argv)==6)
