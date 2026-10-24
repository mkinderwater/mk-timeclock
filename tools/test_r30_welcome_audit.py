"""r30 regression guards for welcome lifecycle and approval audit."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SERVER = (ROOT/'src/server.c').read_text()
OPS = (ROOT/'src/admin_ops.inc').read_text()
REC = (ROOT/'src/reconciliation.inc').read_text()
PAY = (ROOT/'src/payroll_timesheet.h').read_text()


def test_welcome_requires_card_and_is_not_repeated():
    assert "welcomed_at IS NULL AND created_at IS NOT NULL AND julianday('now')-julianday(created_at) BETWEEN 0 AND 7 AND EXISTS(SELECT 1 FROM cards WHERE employee_id=?1 AND active=1)" in SERVER
    assert "id='welcome_employee' AND enabled=1" in SERVER
    assert 'UPDATE employees SET welcomed_at=' in SERVER
    assert 'if(ok)ok=welcome_enqueue(id);' in SERVER
    assert 'if(ok)ok=welcome_enqueue(owner);' in OPS
    assert 'if(owner)ok=welcome_enqueue(owner);' in OPS




def test_override_status_uses_current_approval():
    assert 'a.recorded_at>=s.signed_at' in PAY
    assert "a.action='employee_not_notified'" in PAY
    assert 'notice turned off in Email templates' in REC
    assert 'notice_disabled' in REC
