from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

def test_named_holiday_editor_uses_existing_api():
    source=(ROOT/'src/server.c').read_text()
    section=source.split('static void holidays_page(int fd){',1)[1].split('/* A system action',1)[0]
    assert "id='holidayrows'" in section
    assert "d.type='date'" in section
    assert "n.type='text'" in section
    assert "Add holiday" in section
    assert "seen.has(date)" in section
    assert "'/api/v1/admin/holidays'" in section
    assert "calendar:days.join" in section
    assert 'YYYY-MM-DD,Name<textarea' not in section

def test_manager_uses_readable_names():
    source=(ROOT/'src/time_off.inc').read_text()
    assert "personal:'Personal day'" in source
    assert "timeoff-table-scroll" in source
