import csv, importlib.util, io, pathlib, tempfile, zipfile, xml.etree.ElementTree as ET

ROOT=pathlib.Path(__file__).resolve().parents[1]

def test_status_colours():
    path=ROOT/'timeclock-payroll-xlsx.py'
    spec=importlib.util.spec_from_file_location('payroll_book',path)
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    buf=io.StringIO();w=csv.writer(buf)
    w.writerow(['Employee','RFID ID','Date','IN','OUT','Worked minutes','Reason / correction','Status','Worked hours','Employee ID'])
    w.writerows([
        ['Sam Ortiz','123','2026-10-01','08:00','16:00',480,'','Complete','8.00',11],
        ['Sam Ortiz','123','2026-10-02','','',0,'','Approved vacation','0.00',11],
        ['Sam Ortiz','123','2026-10-03','','',0,'','Stat holiday: Thanksgiving','0.00',11],
        ['Sam Ortiz','123','2026-10-04','09:00','16:00',420,'Bus was late','Complete - late arrival','7.00',11],
    ])
    with tempfile.TemporaryDirectory() as tmp:
        book=pathlib.Path(tmp)/'test.xlsx';module.make(buf.getvalue(),book,'2026-10-01','2026-10-15')
        with zipfile.ZipFile(book) as z:
            for name in z.namelist():
                if name.endswith('.xml'): ET.fromstring(z.read(name))
            ns={'m':'http://schemas.openxmlformats.org/spreadsheetml/2006/main'}
            styles=ET.fromstring(z.read('xl/styles.xml'))
            assert int(styles.find('m:fills',ns).get('count'))>=6
            data=ET.fromstring(z.read('xl/worksheets/sheet2.xml'))
            rows=data.findall('.//m:sheetData/m:row',ns)
            assert [rows[i][0].get('s') for i in (5,6,7)]==['6','7','8']

def test_templates_exposed_and_notice_deduplicated():
    server=(ROOT/'src/server.c').read_text()
    review=(ROOT/'src/account_review.inc').read_text()
    assert 'review_dispatch_payroll' in server and 'auto_review_employee' in server
    assert 'auto-review-summary:%d' in review
    assert 'INSERT OR IGNORE INTO email_queue' in review
    assert 'queued_count++' in review
    assert 'template_on' in review
