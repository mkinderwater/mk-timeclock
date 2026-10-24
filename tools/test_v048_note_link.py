#!/usr/bin/env python3
"""Current late arrival explanation links: scoped, expiring, and passwordless."""
import datetime, json, os, re, sqlite3, subprocess, tempfile, time, urllib.error, urllib.parse, urllib.request
from pathlib import Path
from zoneinfo import ZoneInfo

base = Path(__file__).resolve().parents[1]
KEY = 'note-link-test-device-key'
PORT = 19348
URL = f'http://127.0.0.1:{PORT}'
SESSION = 'a' * 64
V047_BODY = ("Hello {{employee_name}},\n\nScheduled start: {{scheduled_start}}\nRecorded IN: {{actual_start}}\nLate: {{late_minutes}} minutes\n\n"
             "Your attendance has been recorded. Please contact your manager about any approved exception.")


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *a, **k):
        return None


opener = urllib.request.build_opener(NoRedirect)


def call(path, data=None, cookie=False, origin=True, headers=None):
    h = dict(headers or {})
    if cookie:
        h['Cookie'] = 'tc_session=' + SESSION
    if data is not None and origin:
        h['Origin'] = URL
    body = None if data is None else (data if isinstance(data, bytes) else urllib.parse.urlencode(data).encode())
    req = urllib.request.Request(URL + path, data=body, headers=h)
    try:
        r = opener.open(req, timeout=5)
        return r.status, dict(r.headers), r.read().decode()
    except urllib.error.HTTPError as e:
        return e.code, dict(e.headers), e.read().decode()


def swipe(event, local):
    t = datetime.datetime.fromisoformat(local).replace(tzinfo=ZoneInfo('America/Edmonton')).astimezone(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
    s, _, b = call('/api/v1/swipes', json.dumps({'event_id': event, 'clock_id': 'test', 'card_uid': '01020304', 'swiped_at': t}).encode(),
                   origin=False, headers={'X-Clock-ID': 'test', 'X-Device-Key': KEY, 'Content-Type': 'application/json'})
    assert s == 200, (s, b)


def start(db):
    p = subprocess.Popen([str(base / 'timeclock-server'), str(db), str(PORT)], env={**os.environ, 'DEVICE_KEY': KEY, 'TZ': 'America/Edmonton', 'TIMECLOCK_NGINX_TLS_CONF': str(db.parent / 'timeclock-tls.conf')},
                         stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    for _ in range(40):
        try:
            urllib.request.urlopen(URL + '/health', timeout=1); return p
        except Exception:
            time.sleep(.1)
    raise SystemExit(p.stderr.read().decode())


def stop(p):
    p.terminate()
    try: p.wait(timeout=3)
    except subprocess.TimeoutExpired: p.kill()


with tempfile.TemporaryDirectory(prefix='tc048-') as td:
    db = Path(td) / 'server.db'
    srv = start(db)
    try:
        c = sqlite3.connect(db, isolation_level=None)
        c.executescript(f"""
          INSERT INTO registered_clocks(clock_id,device_key) VALUES('test','note-link-test-device-key');
          INSERT INTO employees(id,name,first_name,last_name,email) VALUES(1,'Jane Doe','Jane','Doe','jane@example.test');
          INSERT INTO cards(uid,employee_id) VALUES('01020304',1);
          INSERT INTO schedule_profiles(employee_id,pay_type,start_time,end_time,weekday_mask) VALUES(1,'salary','08:00','16:00',127);
          INSERT INTO employees(id,name,email) VALUES(2,'Boss','boss@example.test');
          INSERT INTO employee_roles(employee_id,role) VALUES(2,'manager');
          INSERT INTO user_accounts(employee_id,password_hash) VALUES(2,'x');
          INSERT INTO manager_cards(uid,manager_id) VALUES('D00DBEEF',2);
          INSERT INTO login_sessions VALUES('{SESSION}',2,CAST(strftime('%s','now') AS INTEGER)+3600);""")

        # Fresh database: employee template carries the link placeholder.
        assert '{{note_link}}' in c.execute("SELECT body FROM email_templates WHERE id='late_arrival_employee'").fetchone()[0]

        # 1. Without a clock address the email still goes out, with no token created.
        swipe('e0', '2026-10-05T08:10:00')
        body0 = c.execute("SELECT body FROM email_queue WHERE event_id='late:e0:employee'").fetchone()[0]
        assert 'note link not configured' in body0 and '/n/' not in body0, body0
        assert c.execute("SELECT count(*) FROM swipe_note_links").fetchone()[0] == 0
        swipe('e0out', '2026-10-05T16:00:00')

        # 1b. The address follows the installed certificate: bootstrap (server_name _) gives none,
        #     a validated certificate gives https://<hostname>, an override wins, blank falls back.
        conf = db.parent / 'timeclock-tls.conf'
        link = lambda: json.loads(call('/api/v1/admin/note-link', cookie=True)[2])
        assert link()['source'] == 'none'
        conf.write_text('server {\n    listen 443 ssl;\n    server_name _;\n}\n')
        assert link()['source'] == 'none'
        conf.write_text('server {\n    listen 443 ssl;\n    server_name clock.icube.test;\n    ssl_certificate /x;\n}\n')
        j = link(); assert (j['source'], j['effective'], j['certificate_host']) == ('certificate', 'https://clock.icube.test', 'clock.icube.test'), j
        swipe('c1', '2026-10-06T08:20:00')
        assert re.search(r'https://clock\.icube\.test/n/[0-9a-f]{64}', c.execute("SELECT body FROM email_queue WHERE event_id='late:c1:employee'").fetchone()[0])
        swipe('c1out', '2026-10-06T16:00:00')
        for bad in ('server_name -bad;', 'server_name a.test b.test;', 'server_name evil"quote;', 'server_name;'):
            conf.write_text('server {\n    ' + bad + '\n}\n')
            assert link()['source'] == 'none', bad
        conf.write_text('server {\n    listen 443 ssl;\n    server_name clock.icube.test;\n}\n')
        assert 'linkform' in call('/tls', cookie=True)[2] and 'linkform' not in call('/email-templates', cookie=True)[2]

        # 2. Override validation (manager only).
        assert call('/api/v1/admin/note-link', {'base_url': 'https://clock.example.test'})[0] == 401
        for bad in ('http://clock.example.test', 'https://clock.example.test/path', 'https://', 'https://bad host', 'https://a:b:c'):
            assert call('/api/v1/admin/note-link', {'base_url': bad}, cookie=True)[0] == 400, bad
        s, _, b = call('/api/v1/admin/note-link', {'base_url': 'https://clock.example.test:8443/'}, cookie=True)
        assert s == 200, b
        j = link(); assert (j['base_url'], j['effective'], j['source']) == ('https://clock.example.test:8443', 'https://clock.example.test:8443', 'override'), j

        # 3. Late IN: link in the employee email only; only a hash is stored.
        swipe('e1', '2026-10-12T08:07:00')
        emp = c.execute("SELECT body FROM email_queue WHERE event_id='late:e1:employee'").fetchone()[0]
        mgr = c.execute("SELECT body FROM email_queue WHERE event_id LIKE 'late:e1:manager:%'").fetchone()[0]
        m = re.search(r'https://clock\.example\.test:8443/n/([0-9a-f]{64})', emp)
        assert m, emp
        token = m.group(1)
        assert '/n/' not in mgr and '{{note_link}}' not in mgr, mgr
        stored = c.execute("SELECT token_hash FROM swipe_note_links WHERE event_id='e1'").fetchone()[0]
        assert token not in stored and stored.startswith('$5$'), stored

        # 4. Public page: no password, shows the swipe, GET changes nothing.
        s, h, page = call('/n/' + token)
        assert s == 200 and 'Jane' in page and 'Late' in page and "name='note'" in page, page
        assert h.get('Referrer-Policy') == 'same-origin' and 'noindex' in h.get('X-Robots-Tag', '')
        assert "class='topbar'" not in page  # no manager navigation
        assert c.execute("SELECT count(*) FROM swipe_notes").fetchone()[0] == 0

        # 5. Unknown and malformed tokens look the same.
        for bad in ('/n/' + 'f' * 64, '/n/' + token[:-1], '/n/' + token.upper(), '/n/x'):
            assert call(bad)[0] == 404, bad
        assert call('/n/' + 'f' * 64, {'note': 'x'})[0] == 404

        # 6. Save, then change, the note. History keeps both; no manager card recorded.
        s, h, _ = call('/n/' + token, {'note': 'Bus broke down on Macleod Trail.'})
        assert s == 303 and h['Location'] == f'/n/{token}?saved=1', (s, h)
        assert 'Reason saved' in call(h['Location'])[2]
        s, _, _ = call('/n/' + token, {'note': 'Bus broke down; arrived <8:07> & signed in.'})
        assert s == 303
        assert c.execute("SELECT note,updated_by FROM swipe_notes WHERE event_id='e1'").fetchone() == ('Bus broke down; arrived <8:07> & signed in.', 1)
        assert c.execute("SELECT count(*),count(approved_card_uid) FROM swipe_note_history WHERE event_id='e1'").fetchone() == (2, 0)
        page = call('/n/' + token)[2]
        assert '&lt;8:07&gt; &amp; signed' in page and '<8:07>' not in page  # escaped
        assert call('/n/' + token, {'note': 'x' * 2001})[0] == 400
        assert call('/n/' + token, {'note': 'cross-site'}, headers={'Origin': 'https://evil.example'}, origin=False)[0] == 403
        assert call('/n/' + token, {'note': 'opaque'}, headers={'Origin': 'null'}, origin=False)[0] == 403

        # 7. Managers see the note on punches; export column renamed to note.
        punches = json.loads(call('/api/v1/admin/punches', cookie=True)[2])['punches']
        assert any(p['id'] == 'e1' and p['note'].startswith('Bus broke down') for p in punches), punches
        csv = call('/api/v1/admin/export?scope=all&employee=0&from=2026-10-12&to=2026-10-12&format=csv', cookie=True)[2]
        assert ',paired_in_event,note,clock_id,' in csv.splitlines()[0], csv.splitlines()[0]

        # 8. Later punches do not prevent a late explanation; expiry and payroll closure do.
        swipe('e1dup', '2026-10-12T08:07:05')
        assert c.execute("SELECT exception FROM attendance_events WHERE event_id='e1dup'").fetchone()[0] == 'duplicate_tap'
        assert "name='note'" in call('/n/' + token)[2]
        swipe('e2', '2026-10-12T12:00:00')
        s, _, page = call('/n/' + token)
        assert s == 200 and "name='note'" in page, page
        s, _, page = call('/n/' + token, {'note': 'Updated after OUT'})
        assert s == 303, (s,page)
        c.execute("UPDATE swipe_note_links SET created_at='2020-01-01T00:00:00Z' WHERE event_id='e1'")
        assert "name='note'" not in call('/n/' + token)[2]
        s, _, page = call('/n/' + token, {'note': 'too late'})
        assert s == 409 and 'closed' in page
        assert c.execute("SELECT note FROM swipe_notes WHERE event_id='e1'").fetchone()[0] == 'Updated after OUT'

        # 8b. Clearing the override goes back to the certificate hostname.
        assert call('/api/v1/admin/note-link', {'base_url': ''}, cookie=True)[0] == 200
        assert link()['effective'] == 'https://clock.icube.test'

        # 9. Old manager notes page is gone; the old URL redirects to punches.
        s, h, _ = call('/swipe-notes', cookie=True)
        assert s == 303 and h['Location'] == '/punch-corrections'
        assert call('/api/v1/admin/employee-change/arm', {'op': 'swipe_note', 'event_id': 'e1', 'note': 'm'}, cookie=True)[0] != 200

        # 10. Baseline: customized templates are not overwritten on restart.
        c.execute("UPDATE email_templates SET body='Custom wording' WHERE id='late_arrival_employee'")
        stop(srv); srv = start(db)
        assert c.execute("SELECT body FROM email_templates WHERE id='late_arrival_employee'").fetchone()[0] == 'Custom wording'
        print('PASS note link: email-only link, hashed token, public page, save/change/escape, double tap, expires at 7 days, survives next swipe, manager view, template preservation')
    finally:
        stop(srv)
