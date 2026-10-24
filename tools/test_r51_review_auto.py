"""Compatibility regression: the old payday-triggered sender has been replaced in r58."""
from pathlib import Path

def test_post_period_sender_is_active():
    code=(Path(__file__).resolve().parents[1]/"src/account_review.inc").read_text()
    assert "auto-review:%d:%d" in code
    assert "auto-review-summary:%d" in code
    assert "AND ends>=date(?1,'-14 days')" in code
    assert "payroll_is_send_date(" not in code
