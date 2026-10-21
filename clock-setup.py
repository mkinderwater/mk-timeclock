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
    os.replace(temp, CONF)
    os.chown(CONF, 0, __import__('grp').getgrnam('timeclock').gr_gid)
    marker = Path('/etc/timeclock/adopted')
    marker.write_text(clock_id + '\n')
    os.chmod(marker, 0o600)
    subprocess.run(['systemctl', 'enable', '--now', 'timeclock-agent.service'], check=True, timeout=15)
    subprocess.run(['systemctl', 'restart', 'timeclock-agent.service'], check=True, timeout=15)

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
        if self.path == '/status':
            with LOCK: state = STATE.copy()
            self.respond(200, json.dumps({'adopted':state['adopted'], 'pending':bool(state['request_id']), 'error':state['error']}), 'application/json'); return
        if self.path != '/':
            self.respond(404, 'Not found', 'text/plain'); return
        with LOCK:
            state = STATE.copy()
        message = ('Adopted. This clock now records attendance.' if state['adopted'] else
                   'Waiting for approval on the Primary clock. Select Check approval after it is approved.' if state['request_id'] else 'Enter the Primary clock address and the code from its Clock management page.')
        p = html.escape(state['primary'], quote=True)
        cid = html.escape(state['clock_id'], quote=True)
        err = html.escape(state['error'])
        disabled = 'disabled' if state['adopted'] else ''
        page = f'''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Join a Primary clock</title>
<style>body{{font:16px system-ui;background:#f6f7f9;color:#18212b;margin:0}}main{{max-width:500px;margin:8vh auto;padding:24px;background:white;border:1px solid #ddd;border-radius:12px}}label{{display:block;margin:16px 0}}input{{display:block;box-sizing:border-box;width:100%;font:inherit;padding:10px;margin-top:6px}}button{{padding:10px 20px;font:inherit}}p{{line-height:1.6}}.error:not(:empty){{color:#b42318;background:#fdecea;border-left:4px solid #b42318;padding:8px 12px}}</style></head><body><main><h1>Join a Primary clock</h1><p>{message}</p><p>Clock ID: <strong>{cid or 'Unassigned'}</strong></p><form method="post" action="/request"><label>Primary HTTPS address<input required name="primary" type="url" value="{p}" placeholder="https://primary.local"></label><label>Code<input required name="code" autocomplete="off" maxlength="24" minlength="24"></label><button {disabled}>Send request</button></form><form method="post" action="/check"><button {'disabled' if not state['request_id'] or state['adopted'] else ''}>Check approval</button></form><p class="error" role="alert">{('<strong>Error</strong> ' + err) if err else ''}</p></main></body></html>'''
        self.respond(200, page)

    def do_POST(self):
        if self.path not in ('/request', '/check'):
            self.respond(404, 'Not found', 'text/plain'); return
        size = int(self.headers.get('Content-Length', '0'))
        if size <= 0 or size > 1024:
            self.respond(400, 'Invalid form', 'text/plain'); return
        fields = urllib.parse.parse_qs(self.rfile.read(size).decode('utf-8'), keep_blank_values=True)
        try:
            with LOCK:
                if STATE['adopted'] or Path('/etc/timeclock/adopted').exists():
                    raise ValueError('This clock has already joined a Primary.')
                conf = CONF.read_text() if CONF.exists() else ''
                if not re.search(r'(?m)^TIMECLOCK_ROLE=slave$', conf):
                    raise ValueError('This clock was not installed as a Slave.')
                if self.path == '/request':
                    primary = fields.get('primary', [''])[0].strip().rstrip('/')
                    code = fields.get('code', [''])[0].strip()
                    if not re.fullmatch(r'[0-9a-f]{24}', code):
                        raise ValueError('The code is 24 characters (0-9, a-f). Check it and try again.')
                    clock_id = 'CLOCK-' + secrets.token_hex(4).upper()
                    result = primary_api(primary, '/api/v1/adoption/request', {'code': code, 'clock_id': clock_id})
                    STATE.update(primary=primary, request_id=result['request_id'], clock_id=clock_id, error='')
                else:
                    if not STATE['request_id']:
                        raise ValueError('Send a request first.')
                    result = primary_api(STATE['primary'], '/api/v1/adoption/poll', {'request_id': STATE['request_id']})
                    if result.get('status') == 'approved':
                        if result.get('clock_id') != STATE['clock_id']:
                            raise ValueError('The Primary approved a different clock. Request a new code.')
                        configure(STATE['clock_id'], STATE['primary'], result['device_key'])
                        STATE['adopted'] = True
                        # Setup is no longer needed after adoption. Stop it shortly after responding.
                        threading.Timer(2.0, lambda: subprocess.run(['systemctl','disable','--now','mk-timeclock-setup.service'], timeout=12)).start()
                    elif result.get('status') != 'pending':
                        raise ValueError('The code expired. Generate a new one on the Primary.')
                    STATE['error'] = ''
        except (ValueError, KeyError, OSError, urllib.error.URLError, subprocess.SubprocessError) as exc:
            with LOCK: STATE['error'] = str(exc)[:200]
        self.send_response(303)
        self.send_header('Location', '/')
        self.send_header('Content-Length', '0')
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()

if __name__ == '__main__':
    ThreadingHTTPServer(('0.0.0.0', 8090), Handler).serve_forever()
