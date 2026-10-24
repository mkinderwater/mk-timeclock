from pathlib import Path
SOURCE = Path(__file__).resolve().parents[1] / 'src' / 'time_off.inc'

def test_employee_uses_shared_forms_and_status_flags():
    text=SOURCE.read_text()
    assert "class='grid'" in text
    assert "class='actions'" in text
    assert "Previous requests" in text
    assert "flag flag-" in text
    assert "tc.ok(document.getElementById('status')" in text and "tc.err(document.getElementById('status')" in text

def test_manager_uses_shared_table_and_status_flags():
    text=SOURCE.read_text()
    assert "Employee requests" in text
    assert "['Employee','Dates','Days','Type','Status','Notes','Action']" in text
    assert "className='secondary'" in text
    assert "function statusFlag" in text

def test_expired_page_retains_existing_style():
    text=SOURCE.read_text()
    assert "UI_HEAD_LOGIN(\"Link expired\")" in text
    assert "class='banner banner-problem'" in text
