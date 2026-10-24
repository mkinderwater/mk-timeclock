from pathlib import Path
root = Path(__file__).resolve().parents[1]
css = (root / 'src/ui.css').read_text()
server = (root / 'src/server.c').read_text()
ui = (root / 'src/ui.inc').read_text()
assert '.section-head' in css and "<div class='section-head'>" in server
assert '.today-preview canvas' in css and '.today-preview-meta' in css
assert '.employee-timesheet' not in css
assert '.mobile-queue td,.mobile-uploads td' in css
assert 'form input:not([type=checkbox])' in css
assert "From email address<input id='sender'" in server
import re
make_version = re.search(r'^UI_VERSION \?= (\S+)$', (root / 'Makefile').read_text(), re.M).group(1)
assert f'#define UI_CSS_VERSION "{make_version}"' in ui, 'ui.inc version must match Makefile UI_VERSION'
print('PASS v0.9.130-r12 GUI cull protections')
