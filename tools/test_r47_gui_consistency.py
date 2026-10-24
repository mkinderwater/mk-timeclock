from pathlib import Path
import re
ROOT=Path(__file__).resolve().parents[1]

def test_page_fetches_use_shared_parser():
    for path in [ROOT/'src/server.c', *(ROOT/'src').glob('*.inc')]:
        assert not re.search(r'await\s+\w+\.json\(\)',path.read_text()),path

def test_holiday_and_timeoff_feedback():
    source=(ROOT/'src/server.c').read_text()
    leave=(ROOT/'src/time_off.inc').read_text()
    assert 'tc.json(r)' in source
    assert 'tc.json(r)' in leave
    assert "document.getElementById('status')" in leave

def test_no_visible_unused_template_option():
    source=(ROOT/'src/server.c').read_text()
    assert 'rows.filter(x=>x.implemented)' in source
    assert 'Not used yet.' not in source

def test_responsive_forms_and_generated_asset():
    css=(ROOT/'src/ui.css').read_text()
    assert 'form .actions{flex-wrap:wrap}' in css
    version=re.search(r'^UI_VERSION\s*\?=\s*(\S+)',(ROOT/'Makefile').read_text(),re.M).group(1)
    assert f'UI_CSS_VERSION \"{version}\"' in (ROOT/'src/ui.inc').read_text()
