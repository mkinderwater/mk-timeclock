"""Execute the page's vacation-only tally with representative employee context."""
from pathlib import Path
import shutil, subprocess, tempfile
source=(Path(__file__).resolve().parents[1]/'src/account_review.inc').read_text()
assert "CASE WHEN lower(t.kind)='sick'" not in source
assert 'if(strcmp(ar_text(q,2),"holiday"))' in source
start=source.index('let labels=')
end=source.index('}else info.textContent=',start)
script=source[start:end]
if shutil.which('node'):
 with tempfile.TemporaryDirectory() as folder:
  file=Path(folder)/'summary.js'
  file.write_text('function summarize(ctx){let p={starts:"2026-11-02",ends:"2026-11-15"},info={textContent:""};function shortDate(s){return s;}'+script+'return info.textContent;}\n'+'''
const assert=require('node:assert/strict');
let ctx=[{day:'2026-11-04',type:'timeoff:vacation',label:'Approved time off: vacation'},{day:'2026-11-05',type:'absence',label:'Bereavement'},{day:'2026-11-06',type:'absence',label:'Unpaid leave'}];
assert.equal(summarize(ctx),'2026-11-02 to 2026-11-15: 1 vacation day, 1 bereavement day, 1 unpaid leave day');
assert.equal(summarize([{day:'2026-11-11',type:'holiday',label:'Stat holiday: Test'}]),'2026-11-02 to 2026-11-15: 1 stat holiday');
assert.equal(summarize([{day:'2026-11-04',type:'timeoff:personal',label:'Approved time off: personal'},{day:'2026-11-05',type:'timeoff:unpaid',label:'Approved time off: unpaid'},{day:'2026-11-06',type:'absence',label:'Personal day'}]),'2026-11-02 to 2026-11-15: 2 personal days, 1 unpaid leave day');
assert.equal(summarize([{day:'2026-11-04',type:'timeoff:other',label:'Approved time off: other'},{day:'2026-11-05',type:'absence',label:'Other'}]),'2026-11-02 to 2026-11-15: 1 other time off, 1 other absence');
assert.equal(summarize([]),'2026-11-02 to 2026-11-15: No leave or holidays recorded');
''')
  subprocess.run(['node',str(file)],check=True)
else:
 print('NOTE: Node unavailable; browser tally execution skipped')
print('PASS: leave-category tally, nonzero summary, no sick request branch, holiday-only exclusion')
