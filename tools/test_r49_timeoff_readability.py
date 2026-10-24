from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
S = (ROOT / 'src/time_off.inc').read_text()
CSS = (ROOT / 'src/ui.css').read_text()

def test_status_first_and_history_no_sideways_table():
    employee = S.split('static void timeoff_employee_page',1)[1].split('static void timeoff_manager_page',1)[0]
    assert 'top.append(flagStatus(x.status))' in employee
    assert 'timeoff-history-item' in employee
    assert 'timeoff-table-scroll' not in employee
    assert 'timeoff-employee' in employee
    assert 'body.login.timeoff-employee main{max-width:var(--page-w)}' in CSS

def test_manager_email_has_confirmed_days_and_return_date():
    assert '(%d working %s, returns %s)' in S
    assert 'calculated==1?"day":"days"' in S
    assert 'name,kind,starts,ends,calculated,calculated==1?"day":"days",return_date' in S
