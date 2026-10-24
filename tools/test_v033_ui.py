#!/usr/bin/env python3
"""v0.9.33 shared GUI: stylesheet route, caching, every page on the shared layout, generated CSS in sync."""
import os,re,sqlite3,subprocess,sys,tempfile,time,urllib.request,urllib.error
from pathlib import Path
base=Path(__file__).resolve().parents[1]
src=lambda f:(base/'src'/f).read_text()
# Static checks: no page keeps its own <style>, and src/ui.inc matches src/ui.css.
for f in ['server.c','admin_ops.inc','schedule.inc','payroll.inc']:
    assert '<style>' not in src(f),f'inline <style> remains in {f}'
ver=re.search(r'#define UI_CSS_VERSION "([^"]+)"',src('ui.inc')).group(1)
with tempfile.TemporaryDirectory() as t:
    out=Path(t)/'ui.inc'
    subprocess.run([sys.executable,'-I',str(base/'tools/gen_ui_inc.py'),str(base/'src/ui.css'),str(out),ver],check=True,stdout=subprocess.DEVNULL)
    assert out.read_text()==src('ui.inc'),'src/ui.inc is stale: run make ui'
# Live checks against the built server.
work=Path(tempfile.mkdtemp(prefix='tc0933-'));db=work/'server.db';env=dict(os.environ,DEVICE_KEY='localtestingkey0123456789');port=18788
server=subprocess.Popen([str(base/'timeclock-server'),str(db),str(port)],env=env,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
try:
    time.sleep(.3);assert server.poll() is None,server.stderr.read().decode()
    con=sqlite3.connect(db);con.execute("INSERT INTO employees(id,name,email) VALUES(1,'Sample Person','sample@example.test')");con.execute("INSERT INTO employee_roles VALUES(1,'manager')");con.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'fakehash')")
    token='b'*64;con.execute("INSERT INTO login_sessions(token,employee_id,expires_at) VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+600)",(token,));con.commit();con.close()
    def get(path,auth=True):
        req=urllib.request.Request(f'http://127.0.0.1:{port}{path}',headers={'Cookie':'tc_session='+token} if auth else {})
        return urllib.request.urlopen(req)
    r=get(f'/ui.css?v={ver}',auth=False)
    assert r.status==200 and r.headers['Content-Type'].startswith('text/css'),r.headers
    assert 'max-age=604800' in r.headers['Cache-Control'],r.headers['Cache-Control']
    css=r.read().decode();assert '.topbar' in css and 'prefers-color-scheme:dark' in css
    login=get('/login',auth=False).read().decode()
    assert f"/ui.css?v={ver}" in login and "class='login'" in login and 'class=\'topbar\'' not in login
    pages={'/':'dashboard','/employees':'employees','/schedules':'schedules','/payroll':'payroll','/email-templates':'templates','/smtp':'smtp',
           '/manager-cards':'manager-cards','/attendance-export':'export','/email-queue':'queue','/rejected-cards':'unknown','/card-status':'cards','/punch-corrections':'corrections'}
    for path,key in pages.items():
        html=get(path).read().decode()
        assert f"/ui.css?v={ver}" in html,path
        assert f"data-page='{key}'" in html,path
        assert html.count("<header class='topbar'>")==1 and html.rstrip().endswith('</main></body></html>'),path
        assert '<style>' not in html,path
        # each data-page key has a matching active-link rule in the stylesheet
        assert f'body[data-page={key}]' in css,key
    anon=get('/employees',auth=False);assert anon.url.endswith('/login'),'pages must still require login'
    try:get('/api/v1/admin/employees',auth=False);raise RuntimeError('admin API must require login')
    except urllib.error.HTTPError as e:assert e.code==401,e.code
    print('PASS shared stylesheet route and caching, 12 manager pages and login on shared layout, generated CSS in sync, auth unchanged')
finally:
    server.terminate();server.wait(timeout=5)
