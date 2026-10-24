#!/usr/bin/env python3
"""Check scripts actually served by the running application, not C source literals."""
import html.parser
import re
import json
import urllib.parse
import urllib.error
import os
import socket
import sqlite3
import subprocess
import tempfile
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PAGES = ['/', '/employees', '/email-templates', '/smtp', '/tls', '/ntp',
         '/system', '/device-health', '/backups', '/diagnostics', '/audit',
         '/display', '/holidays', '/manager-cards', '/schedules', '/clock-grace',
         '/payroll', '/attendance-export', '/email-queue', '/rejected-cards',
         '/rejected-uploads', '/card-status', '/swipe-notes',
         '/punch-corrections', '/presence', '/clocks', '/clock-dim',
         '/pay-periods', '/time-off-requests', '/accounts', '/review-links',
         '/login', '/setup', '/review/invalid-token']

EXPECTED_TITLES = {
 '/':'Overview','/employees':'Employees','/email-templates':'Email templates',
 '/smtp':'Outgoing email','/tls':'HTTPS certificate','/ntp':'Time sync',
 '/system':'System operations','/device-health':'System health','/backups':'Backup and restore',
 '/diagnostics':'Diagnostics','/audit':'Audit log','/display':'Clock display',
 '/holidays':'Stat holidays','/manager-cards':'Manager cards','/schedules':'Schedules',
 '/clock-grace':'Late grace and repeat taps','/payroll':'Payroll report',
 '/attendance-export':'Attendance export','/email-queue':'Email queue',
 '/rejected-cards':'Unknown cards','/rejected-uploads':'Rejected uploads',
 '/card-status':'Card status','/swipe-notes':'Punches and corrections',
 '/punch-corrections':'Punches and corrections','/presence':'Who is in',
 '/clocks':'Clock management','/clock-dim':'Clock display',
 '/pay-periods':'Pay periods','/time-off-requests':'Time off requests',
 '/accounts':'Manager accounts','/review-links':'Timesheet links',
 '/login':'Sign in','/setup':'Sign in','/review/invalid-token':'Link not valid',
}

class Titles(html.parser.HTMLParser):
 def __init__(self):
  super().__init__(); self.inside=False; self.titles=[]
 def handle_starttag(self,tag,attrs):
  if tag=='h1':self.inside=True;self.titles.append('')
 def handle_data(self,data):
  if self.inside:self.titles[-1]+=data
 def handle_endtag(self,tag):
  if tag=='h1':self.inside=False

class Scripts(html.parser.HTMLParser):
    def __init__(self):
        super().__init__(); self.active=False; self.scripts=[]
    def handle_starttag(self, tag, attrs):
        if tag == 'script':
            a=dict(attrs)
            self.active = not a.get('src') and a.get('type','text/javascript') in ('text/javascript','module','')
            if self.active:self.scripts.append('')
    def handle_data(self, data):
        if self.active:self.scripts[-1]+=data
    def handle_endtag(self, tag):
        if tag=='script': self.active=False

with tempfile.TemporaryDirectory(prefix='rc1-rendered-js-') as tmp:
    db=Path(tmp)/'test.sqlite'
    with socket.socket() as s:
        s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    proc=subprocess.Popen([str(ROOT/'timeclock-server'),str(db),str(port)],cwd=ROOT,
        env={**os.environ,'DEVICE_KEY':'rc1-test-device-key'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        base=f'http://127.0.0.1:{port}'
        for _ in range(100):
            try:urllib.request.urlopen(base+'/health',timeout=.3).close();break
            except OSError:time.sleep(.05)
        else:raise AssertionError('HTTP server failed to start')
        with sqlite3.connect(db) as conn:
            conn.execute("INSERT INTO employees(id,name,email) VALUES(1,'Test Manager','manager@example.test')")
            conn.execute("INSERT INTO employee_roles(employee_id,role) VALUES(1,'manager')")
            conn.execute("INSERT INTO manager_cards(uid,manager_id,active) VALUES('A1B2C3D4',1,1)")
            conn.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
            conn.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+600)",('d'*64,))
            conn.execute("INSERT INTO pay_periods(starts,ends,status) VALUES('2026-10-01','2026-10-15','closed')")
        # Seed a valid employee bearer link with the application's SHA-512 crypt format.
        import ctypes
        crypt=ctypes.CDLL('libcrypt.so.1').crypt
        crypt.argtypes=[ctypes.c_char_p,ctypes.c_char_p]
        crypt.restype=ctypes.c_char_p
        token='a'*64
        token_hash=crypt(token.encode(),b'$6$rounds=5000$payreview2026$').decode()
        with sqlite3.connect(db) as conn:
            conn.execute("INSERT INTO review_links(employee_id,period_id,token_hash,expires_at,created_by,created_at,revoked) VALUES(1,1,?,CAST(strftime('%s','now') AS INTEGER)+600,1,CAST(strftime('%s','now') AS INTEGER),0)",(token_hash,))
        routes=['/schedules']
        checked=0
        for route in routes:
            request=urllib.request.Request(base+route,headers={'Cookie':'tc_session='+'d'*64})
            try:
                with urllib.request.urlopen(request,timeout=7) as resp:
                    body=resp.read().decode('utf-8','replace');content_type=resp.headers.get('Content-Type','')
                    assert 'text/html' in content_type,(route,content_type)
            except urllib.error.HTTPError as e:
                if route == '/review/invalid-token' and e.code == 404:
                    body=e.read().decode('utf-8','replace'); content_type=e.headers.get('Content-Type','')
                else:raise AssertionError(f'Could not fetch {route}: HTTP {e.code}') from e
            except Exception as e:
                raise AssertionError(f'Could not fetch {route}: {e}') from e
            titles=Titles();titles.feed(body)
            expected = 'Your recorded timesheet' if route == '/review/'+token else EXPECTED_TITLES[route]
            assert titles.titles and titles.titles[0].strip()==expected, (route,expected,titles.titles)
            parser=Scripts();parser.feed(body)
            for i,script in enumerate(parser.scripts,1):
                path=Path(tmp)/'script.js';path.write_text(script)
                result=subprocess.run(['node','--check',str(path)],capture_output=True,text=True)
                assert result.returncode==0,f'{route} script {i}: {result.stderr}'
                checked+=1
        assert checked>=1,f'Only {checked} served scripts checked'
        print(f'PASS: {checked} scripts parsed from {len(routes)} HTTP-served pages')
    finally:
        proc.terminate()
        try:proc.wait(timeout=3)
        except subprocess.TimeoutExpired:proc.kill();proc.wait()
