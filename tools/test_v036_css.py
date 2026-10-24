#!/usr/bin/env python3
"""v0.9.36: stylesheet consistency lint, nav generated from one table, NTP page reachable with valid JSON."""
import json,os,sqlite3,subprocess,sys,tempfile,time,urllib.request
from pathlib import Path
base=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(base/'tools'))
import css_lint,gen_ui_inc
problems=css_lint.run(base)
assert not problems,'css_lint:\n'+'\n'.join(problems)
# The lint must actually catch the classes of problem it claims to (guards against a silently broken checker).
bad=('a{font-size:13px;color:#123456;padding:7px;border-radius:5px}'
     'b{margin:0}b{margin:0}'
     'c,d{gap:var(--s1)}d{gap:var(--s2)}')
found='\n'.join(css_lint.lint_css(bad))
for needle in ('font-size 13px','raw color','padding value 7px','border-radius 5px','appears 2 times','gap for "d" is set in 2 rules'):
    assert needle in found,f'lint missed: {needle}\n{found}'
with tempfile.TemporaryDirectory() as t:
    crlf=Path(t)/'x';(crlf/'src').mkdir(parents=True);(crlf/'src'/'a.c').write_bytes(b'int a;\r\n');(crlf/'.DS_Store').write_bytes(b'x')
    f='\n'.join(css_lint.lint_files(crlf));assert 'CRLF' in f and 'OS metadata' in f,f
keys={k:h for k,h,_,_ in gen_ui_inc.links()}
assert keys.get('ntp')=='/ntp'
work=Path(tempfile.mkdtemp(prefix='tc0936-'));db=work/'server.db';port=18789
server=subprocess.Popen([str(base/'timeclock-server'),str(db),str(port)],env=dict(os.environ,DEVICE_KEY='localtestingkey0123456789'),stdout=subprocess.PIPE,stderr=subprocess.PIPE)
try:
    time.sleep(.3);assert server.poll() is None,server.stderr.read().decode()
    con=sqlite3.connect(db);con.execute("INSERT INTO employees(id,name,email) VALUES(1,'Sample Person','sample@example.test')");con.execute("INSERT INTO employee_roles VALUES(1,'manager')");con.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
    token='d'*64;con.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+600)",(token,));con.commit();con.close()
    get=lambda p:urllib.request.urlopen(urllib.request.Request(f'http://127.0.0.1:{port}{p}',headers={'Cookie':'tc_session='+token})).read().decode()
    ntp=json.loads(get('/api/v1/admin/ntp'));assert set(ntp)=={'server','status'},ntp
    css=get('/ui.css')
    for key,href in keys.items():
        html=get(href)
        assert f"data-page='{key}'" in html,href
        assert f"body[data-page={key}] .nav a[href='{href}']" in css,key
        assert "style='" not in html,href
    print(f'PASS css_lint clean and self-checked, {len(keys)} nav pages generated and highlighted, NTP page in nav with valid JSON')
finally:
    server.terminate();server.wait(timeout=5)
