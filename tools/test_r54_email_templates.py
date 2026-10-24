from pathlib import Path
import re
ROOT = Path(__file__).resolve().parents[1]
server=(ROOT/'src/server.c').read_text()
timeoff=(ROOT/'src/time_off.inc').read_text()
mailer=(ROOT/'src/mailer.c').read_text()
review=(ROOT/'src/account_review.inc').read_text()

def test_new_templates_seeded_and_editable():
    for key in ['timeoff_received_employee','timeoff_received_manager','timeoff_decision_employee','timeoff_cancelled_employee','review_reminder_employee','email_failure_payroll']:
        assert f'{{"{key}",' in server
        assert f"'{key}'" in server
        assert f'"{key}"' in server

def test_notifications_use_enabled_templates():
    assert 'SELECT subject,body FROM email_templates WHERE id=?1 AND enabled=1' in timeoff
    assert 'timeoff_render_template("timeoff_received_employee"' in timeoff
    assert 'timeoff_render_template("timeoff_received_manager"' in timeoff
    assert 'timeoff_render_template(template_id' in timeoff
    assert 'timeoff_cancelled_employee' in timeoff
    assert 'timeoff_decision:%d:%s' in timeoff

def test_failure_notice_is_once_and_not_recursive():
    assert 'email_terminal_failure:%d' in mailer
    assert 'if(rc!=0&&attempts>=9&&!alert_is_failure)' in mailer
    assert 'if(updated&&attempts>=9&&!alert_is_failure)' in mailer
    assert 'Payroll workbook generation failed' in mailer
    assert "next_attempt=strftime('%Y-%m-%dT%H:%M:%fZ','now','+15 minutes')" in mailer

def test_manual_review_respects_template_toggle():
    assert "id='review_reminder_employee'" in review
    assert 'if(!reminder_enabled)' in review
