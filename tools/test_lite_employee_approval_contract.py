"""Source guardrail for employee approval and manager override, supplement live HTTP tests."""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
a=(r/'src/account_review.inc').read_text()
b=(r/'src/reconciliation.inc').read_text()
assert 'strcmp(suffix,"/approve")' in a
assert 'role=\'employee\'' in a
assert 'id=\\"approve\\"' in a
assert "'manager_override'" in b
assert 'employee_confirmed' in b
print('PASS employee approve route and manager override audit present')
