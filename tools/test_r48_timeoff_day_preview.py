from pathlib import Path

SOURCE = (Path(__file__).resolve().parents[1] / 'src/time_off.inc').read_text()


def test_day_endpoint_consumes_entire_prefix():
    assert 'strncmp(suffix,"/days?starts=",13)' in SOURCE
    assert 'const char *s=suffix+13' in SOURCE


def test_zero_day_rejected_for_preview_and_submit():
    assert SOURCE.count('These dates have no scheduled working days.') == 2
    assert 'if(days==0)' in SOURCE
    assert 'if(calculated==0)' in SOURCE


def test_schedule_hint_uses_actual_shift_presence():
    assert 'scheduled_shifts' in SOURCE
    assert 'scheduled shifts and weekdays, excluding stat holidays' in SOURCE
    assert 'weekday estimate; no fixed weekly schedule' in SOURCE
    assert "')+'. You can adjust the total if needed.'" in SOURCE
    assert "schedule).':'" not in SOURCE
