from pathlib import Path
s=Path('src/time_off.inc').read_text()
assert "tc.ok(document.getElementById('status'),'Request submitted. Your manager will review it.')" in s
assert "tc.err(document.getElementById('status'),e)" in s
assert "tc.ok(document.getElementById('status'),'Decision saved.')" in s
assert 'tc.ok(status,' not in s
