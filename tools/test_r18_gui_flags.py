#!/usr/bin/env python3
"""v0.9.130-r18: GUI copy rules and visual issue flags.

Copy rules: one wording for manager-card prompts and timeouts, page title = menu label = heading,
no internal jargon in page text, no em dash. Flag rules: caught errors use tc.err (red Error style),
the issue summary drives menu counts, and the APIs expose the fields the flags need."""
import json, os, re, sqlite3, subprocess, sys, tempfile, time, urllib.error, urllib.parse, urllib.request
from pathlib import Path

root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root / 'tools'))
import gen_ui_inc

PAGE_SOURCES = ['server.c', 'account_review.inc', 'admin_ops.inc', 'payroll.inc', 'reconciliation.inc', 'schedule.inc', 'note_link.inc']
src = {f: (root / 'src' / f).read_text() for f in PAGE_SOURCES}
everything = ''.join(src.values())
ui = (root / 'src/ui.inc').read_text()
css = (root / 'src/ui.css').read_text()

# 1. One wording for card prompts and timeouts (defined once in UI_JS).
assert "CARD:'Tap your manager card on the clock within 30 seconds.'" in ui
assert "TIMEOUT:'Time ran out. Try again.'" in ui
for old in ('Present your manager card', 'Present registered manager card', 'Present manager RFID card', 'Present a manager card',
            'present manager card within 30 seconds', "'Present manager card'", 'Present one card to the RC522', 'Scan expired',
            'Approval expired', 'Scan timed out', 'Authorization timeout', 'Manager card was not presented', "'Authorization expired'"):
    assert old not in everything, f'old prompt wording remains: {old}'

# 2. Internal or removed-feature wording never appears in page text.
for phrase in ('never stored in SQLite', 'systemd-timesyncd', 'the GUI may disconnect', 'removed role setup key', 'designation only',
               'Raw swipes', 'Effective punches', 'Unmeasured hardware', 'sync buffer', 'no event handler', 'Informational only',
               'Settings-only restoration', 'No backup synchronization', 'Swipe (ISO)', 'HTTP 400, 409, 422', 'RF read errors',
               'passwordless web login', 'chrony or another NTP daemon', 'never retrieved from storage'):
    assert phrase not in everything, f'jargon remains: {phrase}'
for f in list(PAGE_SOURCES) + ['ui.css']:
    text = (root / 'src' / f).read_text()
    assert chr(0x2014) not in text, f'{f}: em dash'

# 3. Caught errors are shown in the error style, never as plain status text.
assert not re.search(r"\.textContent=(e|err|x)\.message", everything), 'use tc.err(el,e) so errors are flagged'
assert not re.search(r"alert\((e|err|x)\.message\)", everything)
for rule in ('.is-err:not(:empty)', ".is-err::before{content:'Error'", '.flag-problem', '.flag-check', '.banner-problem',
             'tr.row-problem>td', 'print-color-adjust:exact', '.count{'):
    assert rule in css, rule

# 4. Every page: title, menu label and heading are the same words.
labels = {h: l for _, h, l, _ in gen_ui_inc.links()}
for title, key in re.findall(r'UI_HEAD\("([^"]+)","([\w-]+)"\)', everything):
    href = next(h for k, h, _, _ in gen_ui_inc.links() if k == key)
    assert title == labels[href], f'{key}: title {title!r} != menu {labels[href]!r}'

# 5. Live checks against a seeded database.
port = 19918
key = 'r18-device-key-0123456789'
with tempfile.TemporaryDirectory() as tmp:
    db = Path(tmp) / 'server.db'
    proc = subprocess.Popen([str(root / 'timeclock-server'), str(db), str(port)], env={**os.environ, 'DEVICE_KEY': key},
                            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    try:
        for _ in range(100):
            try:
                urllib.request.urlopen(f'http://127.0.0.1:{port}/health', timeout=.3); break
            except Exception: time.sleep(.05)
        else: raise AssertionError('server did not start')
        with sqlite3.connect(db) as c:
            c.create_function('approval_uid', 0, lambda: None)
            c.execute("INSERT INTO registered_clocks(clock_id,device_key,active) VALUES('CLOCK-001',?,1)", (key,))
            c.execute("INSERT INTO employees(id,name,email,first_name,last_name,position) VALUES(1,'Dana Reyes','dana@example.test','Dana','Reyes','Manager')")
            c.execute("INSERT INTO employees(id,name,email,first_name,last_name,position) VALUES(2,'Lee Park','','Lee','Park','Technician')")
            c.execute("INSERT INTO employee_roles VALUES(1,'manager')")
            c.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,?)", ('$6$rounds=5000$r18salt$' + 'x' * 86,))
            c.execute("INSERT INTO manager_cards(uid,manager_id,active) VALUES('A1B2C3D4',1,1)")
            c.execute("INSERT INTO cards(uid,employee_id,active) VALUES('55667788',2,1)")
            c.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+600)", ('f' * 64,))
            c.execute("INSERT INTO swipes(event_id,clock_id,card_uid,employee_id,swiped_at) VALUES('e1','CLOCK-001','55667788',2,strftime('%Y-%m-%dT%H:%M:%SZ','now','-3 hours'))")
            c.execute("INSERT INTO attendance_events(event_id,employee_id,kind,late_minutes) VALUES('e1',2,'IN',7)")
            c.execute("INSERT INTO email_queue(recipient,subject,body,status) VALUES('x@example.test','s','b','failed')")
            c.execute("INSERT INTO unknown_cards(uid,clock_id,first_seen,last_seen) VALUES('CAFEBABE','CLOCK-001',strftime('%Y-%m-%dT%H:%M:%fZ','now'),strftime('%Y-%m-%dT%H:%M:%fZ','now'))")
        def call(path, data=None, cookie=True):
            headers = {'Cookie': 'tc_session=' + 'f' * 64} if cookie else {}
            if data is not None:
                data = urllib.parse.urlencode(data).encode(); headers['Content-Type'] = 'application/x-www-form-urlencoded'
            req = urllib.request.Request(f'http://127.0.0.1:{port}' + path, data=data, headers=headers)
            try:
                with urllib.request.urlopen(req, timeout=4) as r: return r.status, r.read().decode()
            except urllib.error.HTTPError as e: return e.code, e.read().decode()
        # Page heading matches the menu label on every page.
        for _, href, label, _ in gen_ui_inc.links():
            code, html = call(href)
            assert code == 200, (href, code)
            assert f'<h1>{label}</h1>' in html, f'{href}: heading must be {label!r}'
            assert 'window.tc=' in html and html.index('window.tc=') < html.index('<main>'), f'{href}: tc helpers must load first'
        # Issue summary: counts for the menu, every key is a menu page.
        code, body = call('/api/v1/admin/issues'); assert code == 200, body
        items = {i['key']: i for i in json.loads(body)['items']}
        keys = {k for k, *_ in gen_ui_inc.links()}
        assert set(items) <= keys, set(items) - keys
        assert items['queue']['count'] == 1 and items['unknown']['count'] == 1
        assert items['employees']['count'] == 1, items  # Lee: late emails on, no address, card but no schedule
        assert 'smtp' in items and 'clocks' in items
        # Fields the flags need.
        emp = {e['id']: e for e in json.loads(call('/api/v1/admin/employees')[1])['employees']}
        assert emp[2]['has_schedule'] is False and emp[1]['has_schedule'] is False
        punches = json.loads(call('/api/v1/admin/punches')[1])['punches']
        assert punches and punches[0]['late'] == 7 and punches[0]['corrected'] is False
        assert 'synchronized' in json.loads(call('/api/v1/admin/ntp')[1])
        tls = json.loads(call('/api/v1/admin/tls/status')[1]); assert 'expires' in tls and 'temporary' in tls
        # A failed sign-in returns the form with a Problem banner, not a bare text page.
        code, html = call('/login', {'email': 'dana@example.test', 'password': 'wrong-password'}, cookie=False)
        assert code == 401 and "banner-problem" in html and 'Email or password is incorrect.' in html and "name='password'" in html
        # Unusable review links get a page, not plain text.
        code, html = call('/review/' + '0' * 64, cookie=False)
        assert code == 404 and '<h1>Link not valid</h1>' in html
        # Period close failures name their cause.
        code, body = call('/api/v1/admin/periods', {'action': 'close', 'starts': '2020-01-01', 'ends': '2020-01-15', 'employee_id': '0', 'reason': 'Test close'})
        assert code == 409 and 'not found' in json.loads(body)['error'], body
    finally:
        proc.terminate(); proc.communicate(timeout=5)
print('PASS r18 copy rules, error styling, issue counts, flag data and page headings')
