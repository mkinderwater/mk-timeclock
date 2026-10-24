from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
def test_user_facing_corrections_and_manager_hours():
    a=(ROOT/'src/attendance.inc').read_text()
    p=(ROOT/'src/payroll_timesheet.h').read_text()
    r=(ROOT/'src/reconciliation.inc').read_text()
    v=(ROOT/'src/account_review.inc').read_text()
    assert 'if(at==old){exception=1;reason="duplicate_tap";}' in a
    assert 'else if(at<old){exception=1;reason="out_of_order";}' in a
    assert 'Manager added OUT:' in p
    assert "THEN '' ELSE coalesce(a.exception,'')" in p
    assert "(e.current_seconds/3600).toFixed(2)" in r
    assert "tc.flag('Hours incomplete','problem')" in v
    assert 'sign.hidden=true' in v
    assert 'Next business day after the notification' in v

