#!/usr/bin/env python3
"""Local, temporary Slave adoption GUI. No dependency outside Python stdlib."""
import html
import json
import os
import re
import secrets
import ssl
import subprocess
import threading
import urllib.error
import urllib.parse
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

CONF = Path('/etc/timeclock/timeclock.env')
STATE = {'primary': '', 'request_id': '', 'clock_id': '', 'error': '', 'adopted': False}
LOCK = threading.Lock()
OPERATION = threading.Lock()
MARKER = CONF.parent / 'adopted'
APPROVAL = None

def saved_connection():
    """Resume credentials already received, without redeeming a one-time token again."""
    values = dict(line.split('=', 1) for line in CONF.read_text().splitlines() if '=' in line) if CONF.exists() else {}
    clock, primary, key = (values.get(name, '') for name in ('CLOCK_ID', 'SERVER_URL', 'DEVICE_KEY'))
    if (values.get('TIMECLOCK_ROLE') == 'slave' and values.get('ATTENDANCE_ENABLED') == 'yes'
            and re.fullmatch(r'[A-Za-z0-9_.:-]{1,48}', clock)
            and re.fullmatch(r'https://[a-zA-Z0-9.\[\]:-]+(?::[0-9]{1,5})?/?', primary)
            and re.fullmatch(r'[0-9a-f]{64}', key)):
        return clock, primary, key
    return None


def primary_api(url, route, data):
    if not re.fullmatch(r'https://[a-zA-Z0-9.\[\]:-]+(?::[0-9]{1,5})?/?', url):
        raise ValueError('Enter the Primary address as https://name, with no path.')
    body = urllib.parse.urlencode(data).encode('ascii')
    req = urllib.request.Request(url.rstrip('/') + route, body,
                                 headers={'Content-Type': 'application/x-www-form-urlencoded'})
    with urllib.request.urlopen(req, timeout=6, context=ssl.create_default_context()) as res:
        if res.status != 200:
            raise ValueError('The Primary refused the request.')
        return json.load(res)

def validate_connection(clock_id, primary, key):
    """Require authenticated contact before committing adoption."""
    body = urllib.parse.urlencode({'clock_id': clock_id, 'pending': 0, 'oldest': '',
                                  'rejected': 0, 'healthy': 0, 'synced': 0}).encode('ascii')
    req = urllib.request.Request(primary.rstrip('/') + '/api/v1/clock/heartbeat', body,
        headers={'Content-Type': 'application/x-www-form-urlencoded',
                 'X-Clock-ID': clock_id, 'X-Device-Key': key})
    try:
        with urllib.request.urlopen(req, timeout=6, context=ssl.create_default_context()) as res:
            if res.status != 200 or json.load(res).get('status') != 'received':
                raise ValueError('The Primary did not acknowledge this clock. Try Check approval again.')
    except urllib.error.HTTPError as exc:
        if exc.code not in (401, 403):
            raise
        # Explicit rejection permits a fresh request, including after setup restarts.
        original = CONF.read_text()
        temp = CONF.with_suffix('.tmp')
        with open(temp, 'w') as f:
            f.write('\n'.join(line for line in original.splitlines()
                              if not line.startswith('DEVICE_KEY=')) + '\n')
        os.chmod(temp, CONF.stat().st_mode & 0o777)
        os.chown(temp, CONF.stat().st_uid, CONF.stat().st_gid)
        os.replace(temp, CONF)
        global APPROVAL
        APPROVAL = None
        with LOCK:
            STATE.update(request_id='', clock_id='')
        raise ValueError('The Primary refused this clock key. Generate a new code on the Primary and Send request.') from None


def configure(clock_id, primary, key):
    if not re.fullmatch(r'[A-Za-z0-9_.:-]{1,48}', clock_id) or not re.fullmatch(r'[0-9a-f]{64}', key):
        raise ValueError('The approval from the Primary is not valid. Request a new code.')
    original = CONF.read_text() if CONF.exists() else ''
    lines = [l for l in original.splitlines() if not l.startswith(('CLOCK_ID=', 'SERVER_URL=', 'DEVICE_KEY=', 'ATTENDANCE_ENABLED=', 'TIMECLOCK_ROLE='))]
    lines += ['TIMECLOCK_ROLE=slave', 'ATTENDANCE_ENABLED=yes', 'CLOCK_ID='+clock_id,
              'SERVER_URL='+primary.rstrip('/'), 'DEVICE_KEY='+key]
    temp = CONF.with_suffix('.tmp')
    with open(temp, 'w') as f:
        f.write('\n'.join(lines)+'\n')
    os.chmod(temp, 0o640)
    os.chown(temp, 0, __import__('grp').getgrnam('timeclock').gr_gid)
    os.replace(temp, CONF)
    subprocess.run(['systemctl', 'enable', '--now', 'timeclock-agent.service'], check=True, timeout=15)
    subprocess.run(['systemctl', 'restart', 'timeclock-agent.service'], check=True, timeout=15)
    subprocess.run(['systemctl', 'enable', '--now', 'timeclock-backup.timer'], check=True, timeout=15)
    # Completion marker is the last commit, after every required service succeeds.
    temp_marker = MARKER.with_suffix('.tmp')
    with open(temp_marker, 'w') as f:
        f.write(clock_id + '\n')
    os.chmod(temp_marker, 0o600)
    os.replace(temp_marker, MARKER)


class Handler(BaseHTTPRequestHandler):
    def respond(self, status, body, content_type='text/html; charset=utf-8'):
        payload = body.encode()
        self.send_response(status)
        self.send_header('Content-Type', content_type)
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('Content-Length', str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):
        if self.path == '/ui.css':
            folder = Path(__file__).resolve().parent
            css = folder / 'ui.css'
            if not css.is_file():
                css = folder / 'src' / 'ui.css'
            try:
                self.respond(200, css.read_text(), 'text/css; charset=utf-8')
            except OSError:
                self.respond(503, 'Stylesheet unavailable.', 'text/plain; charset=utf-8')
            return
        if self.path == '/status':
            with LOCK: state = STATE.copy()
            self.respond(200, json.dumps({'adopted':state['adopted'], 'pending':bool(state['request_id']), 'error':state['error']}), 'application/json'); return
        if self.path != '/':
            self.respond(404, 'Not found', 'text/plain'); return
        with LOCK:
            state = STATE.copy()
        connection = saved_connection()
        if connection:
            state.update(primary=connection[1], clock_id=connection[0])
        message = ('A setup request is running. Please wait.' if state.get('busy') else
                   'Saved approval. Select Check approval to validate it and finish setup.' if connection and not state['adopted'] else
                   'Adopted. This clock now records attendance.' if state['adopted'] else
                   'Waiting for approval on the Primary clock. Select Check approval after it is approved.' if state['request_id'] else 'Enter the Primary clock address and the code from its Clock management page.')
        p = html.escape(state['primary'], quote=True)
        cid = html.escape(state['clock_id'], quote=True)
        err = html.escape(state['error'])
        disabled = 'disabled' if state['adopted'] or state.get('busy') or connection else ''
        page = f'''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Join a Primary clock</title>
<link rel="stylesheet" href="/ui.css"></head><body>
<header class="topbar"><div class="topbar-in"><a class="brand" href="/">mk-timeclock</a><span class="muted">Slave setup</span></div></header>
<main><h1>Join a Primary clock</h1><p>Connect this clock to your attendance system.</p>
<section><h2>Clock connection</h2><p role="status">{message}</p>
<p>Clock ID: <strong>{cid or 'Unassigned'}</strong></p>
<p class="is-err" role="alert">{err}</p>
<form method="post" action="/request">
<label>Primary HTTPS address<input required name="primary" type="url" value="{p}" placeholder="https://primary.local" {disabled}></label>
<label>Code<input required name="code" autocomplete="off" maxlength="24" minlength="24" {disabled}></label>
<button {disabled}>Send request</button></form></section>
<section><h2>Approval</h2><p>Approve this clock on the Primary, then check its approval here.</p>
<form method="post" action="/check"><button class="secondary" {'disabled' if (not state['request_id'] and not connection) or state['adopted'] or state.get('busy') else ''}>Check approval</button></form>
</section></main></body></html>'''
        self.respond(200, page)

    def do_POST(self):
        if self.path not in ('/request', '/check'):
            self.respond(404, 'Not found', 'text/plain'); return
        try:
            size = int(self.headers.get('Content-Length', '0'))
        except ValueError:
            self.respond(400, 'Invalid form', 'text/plain'); return
        if size < 0 or size > 1024 or (self.path == '/request' and size == 0):
            self.respond(400, 'Invalid form', 'text/plain'); return
        global APPROVAL
        if not OPERATION.acquire(blocking=False):
            self.respond(409, 'A setup request is already running. Please wait.', 'text/plain; charset=utf-8')
            return
        try:
            fields = urllib.parse.parse_qs(self.rfile.read(size).decode('utf-8'), keep_blank_values=True)
            with LOCK:
                state = STATE.copy()
                STATE['busy'] = True
            if state['adopted'] or MARKER.exists():
                raise ValueError('This clock has already joined a Primary.')
            conf = CONF.read_text() if CONF.exists() else ''
            if not re.search(r'(?m)^TIMECLOCK_ROLE=slave$', conf):
                raise ValueError('This clock was not installed as a Slave.')
            connection = APPROVAL or saved_connection()
            if self.path == '/request':
                if connection:
                    raise ValueError('Saved approval. Select Check approval to validate it and finish setup.')
                primary = fields.get('primary', [''])[0].strip().rstrip('/')
                code = fields.get('code', [''])[0].strip()
                if not re.fullmatch(r'[0-9a-f]{24}', code):
                    raise ValueError('The code is 24 characters (0-9, a-f). Check it and try again.')
                clock_id = 'CLOCK-' + secrets.token_hex(4).upper()
                result = primary_api(primary, '/api/v1/adoption/request', {'code': code, 'clock_id': clock_id})
                with LOCK:
                    STATE.update(primary=primary, request_id=result['request_id'], clock_id=clock_id, error='')
            else:
                if not connection:
                    if not state['request_id']:
                        raise ValueError('Send a request first.')
                    result = primary_api(state['primary'], '/api/v1/adoption/poll', {'request_id': state['request_id']})
                    if result.get('status') == 'approved':
                        if result.get('clock_id') != state['clock_id']:
                            raise ValueError('The Primary approved a different clock. Request a new code.')
                        connection = (state['clock_id'], state['primary'], result['device_key'])
                        APPROVAL = connection
                    elif result.get('status') != 'pending':
                        raise ValueError('The code expired. Generate a new one on the Primary.')
                if connection:
                    validate_connection(*connection)
                    configure(*connection)
                    with LOCK:
                        STATE.update(adopted=True, primary=connection[1], clock_id=connection[0])
                    APPROVAL = None
                    threading.Timer(2.0, lambda: subprocess.run(['systemctl','disable','--now','mk-timeclock-setup.service'], timeout=12)).start()
                with LOCK:
                    STATE['error'] = ''
        except (ValueError, KeyError, OSError, urllib.error.URLError, subprocess.SubprocessError) as exc:
            with LOCK:
                STATE['error'] = str(exc)[:200]
        finally:
            with LOCK:
                STATE['busy'] = False
            OPERATION.release()
        self.send_response(303)
        self.send_header('Location', '/')
        self.send_header('Content-Length', '0')
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()

if __name__ == '__main__':
    ThreadingHTTPServer(('0.0.0.0', 8090), Handler).serve_forever()
