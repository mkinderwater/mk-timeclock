#!/usr/bin/env python3
"""v0.9.47 menu regression: every NAV page renders the menu with its behaviour script, the
current page is marked, and every inline page script parses (node --check, when Node.js exists)."""
import os, re, shutil, sqlite3, subprocess, sys, tempfile, time, urllib.request
from pathlib import Path

base = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(base / 'tools'))
import gen_ui_inc  # noqa: E402

pages = [(k, h) for k, h, _, _ in gen_ui_inc.links()]
assert all(len(item) == 2 and isinstance(item[1], list) for item in gen_ui_inc.NAV), 'NAV entries must all be dropdown groups'

css = (base / 'src/ui.css').read_text(encoding='utf-8')
assert '[hidden]{display:none}' in css, 'hidden attribute must win over element display rules'

# A bare `status` in page scripts resolves to the browser's window.status string, not the
# <p id='status'> element, so messages silently disappear. Scripts must declare their own reference.
for src in sorted((base / 'src').glob('*.inc')) + sorted((base / 'src').glob('*.c')):
    if src.name == 'ui.inc':
        continue
    for m in re.finditer(r'<script>(.*?)</script>', src.read_text(encoding='utf-8'), re.S):
        js = m.group(1)
        if re.search(r'(?<![\w.])status\.textContent', js):
            assert re.search(r'[,(]\s*status\s*[,)=]|\b(?:const|let|var)\s+status\s*=', js), \
                f'{src.name}: script writes to bare `status` (window.status), not the status element'

node = shutil.which('node')
with tempfile.TemporaryDirectory(prefix='tc047-') as td:
    db = Path(td) / 'tc.db'; port = 19347
    env = dict(os.environ, DEVICE_KEY='test-device-key-123456789', TZ='America/Edmonton')
    proc = subprocess.Popen([str(base / 'timeclock-server'), str(db), str(port)], env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        time.sleep(.3); assert proc.poll() is None, proc.stderr.read().decode()
        c = sqlite3.connect(db)
        c.execute("INSERT INTO employees(id,name) VALUES(1,'Manager')")
        c.execute("INSERT INTO employee_roles VALUES(1,'manager')")
        c.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
        c.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+3600)", ('f' * 64,))
        c.commit(); c.close()
        checked = 0
        for key, href in pages:
            req = urllib.request.Request(f'http://127.0.0.1:{port}{href}', headers={'Cookie': 'tc_session=' + 'f' * 64})
            html = urllib.request.urlopen(req, timeout=5).read().decode()
            assert f"data-page='{key}'" in html, f'{href}: wrong or missing data-page'
            assert html.count("<details class='g-") == len(gen_ui_inc.NAV), f'{href}: menu groups missing'
            assert f"data-key='{key}'" in html, f'{href}: current page link missing data-key'
            assert html.count(gen_ui_inc.NAV_JS) == 1, f'{href}: menu script missing or duplicated'
            if node:
                for i, script in enumerate(re.findall(r'<script>(.*?)</script>', html, re.S)):
                    f = Path(td) / f'{key}-{i}.js'; f.write_text(script, encoding='utf-8')
                    r = subprocess.run([node, '--check', str(f)], capture_output=True, text=True)
                    assert r.returncode == 0, f'{href}: inline script {i} does not parse:\n{r.stderr[-400:]}'
                    checked += 1
        print(f'test_v047_menu: PASS ({len(pages)} pages' + (f', {checked} inline scripts parsed)' if node else ', node not found: script syntax check skipped)'))
    finally:
        proc.terminate(); proc.wait(timeout=5)
