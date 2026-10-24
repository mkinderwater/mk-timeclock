from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / 'src' / 'time_off.inc').read_text()
CSS = (ROOT / 'src' / 'ui.css').read_text()

def test_request_history_mobile_and_labels():
    assert 'timeoff-history-item' in SOURCE
    assert 'top.append(flagStatus(x.status))' in SOURCE
    assert "dates.textContent=x.starts+' to '+x.ends" in SOURCE
    assert 'note.textContent=\'Manager note: \'+x.decision_note' in SOURCE
    assert 'body.login.timeoff-employee main{max-width:var(--page-w)}' in CSS
    assert "personal:'Personal day'" in SOURCE
