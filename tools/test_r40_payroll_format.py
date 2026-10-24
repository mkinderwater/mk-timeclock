from pathlib import Path
import re

S = (Path(__file__).resolve().parents[1] / 'src/payroll_timesheet.h').read_text()

def test_leave_line_matches_arguments():
    m = re.search(r'sqlite3_str_appendf\(txt,"(  %s \(Employee #%d, RFID: %s\): .*?\\n)",(.*?)\);', S)
    assert m, 'Expected approved-leave formatter not found'
    fmt, args = m.groups()
    conversions = re.findall(r'%(?:[-+ #0]*\d*(?:\.\d+)?[hljztL]*)?[sdiuoxXfFeEgGaAcCp]', fmt)
    # Count top-level comma-delimited C arguments without splitting ternaries.
    depth=0; quoted=False; escaped=False; count=1
    for c in args:
        if quoted:
            if escaped: escaped=False
            elif c=='\\': escaped=True
            elif c=='"': quoted=False
        elif c=='"': quoted=True
        elif c=='(': depth+=1
        elif c==')': depth-=1
        elif c==',' and depth==0: count+=1
    assert len(conversions)==count, (conversions,args)
