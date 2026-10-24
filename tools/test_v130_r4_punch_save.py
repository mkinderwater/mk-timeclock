from pathlib import Path
p=Path(__file__).resolve().parents[1]/'src/reconciliation.inc'
s=p.read_text()
assert "d.toISOString().slice(0,19)+'Z'" in s, 'Manual punch must use second-resolution UTC expected by rec_stamp'
assert "<p id='editstatus' role='status' aria-live='polite'></p>" in s, 'Save errors must be visible in active dialog'
assert "tc.err(editstatus,e)" in s, 'Correction failures should not display behind modal'
assert "save.disabled=true;editstatus.textContent='Saving correction...'" in s, 'Guard duplicate submits and show progress'
assert 'if(strlen(s)!=20' in s, 'Test assumes existing 20-byte backend timestamp validator'
print('PASS manual punch timestamp, visible save errors and request guard')
