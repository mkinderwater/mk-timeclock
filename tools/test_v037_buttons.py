#!/usr/bin/env python3
"""v0.9.37: every page button sits in an .actions row; spacing contract present in the served stylesheet."""
import os,sqlite3,subprocess,sys,tempfile,time,urllib.request
from html.parser import HTMLParser
from pathlib import Path
base=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(base/'tools'))
import css_lint,gen_ui_inc
problems=css_lint.run(base);assert not problems,'\n'.join(problems)
# The checker must flag a loose button.
with tempfile.TemporaryDirectory() as t:
    r=Path(t);(r/'src').mkdir();(r/'tools').mkdir()
    for f in css_lint.PAGE_SOURCES:(r/'src'/f).write_text('')
    (r/'src'/'server.c').write_text("\"<form><label>X<input></label><button>Go</button></form>\"")
    (r/'tools'/'gen_ui_inc.py').write_text((base/'tools/gen_ui_inc.py').read_text())
    assert any('outside' in p for p in css_lint.lint_pages(r)),'lint missed a loose button'
class Buttons(HTMLParser):
    def __init__(s):super().__init__();s.stack=[];s.loose=[];s.text=None
    def handle_starttag(s,tag,a):
        a=dict(a)
        if tag in('input','meta','link','br','img'):return
        s.stack.append((tag,a.get('class','')))
        if tag=='button':s.text=''
    def handle_endtag(s,tag):
        if tag=='button':
            ok=any(t=='div' and 'actions' in c.split() for t,c in s.stack[:-1]) or any(t in('header','td') for t,_ in s.stack[:-1])
            if not ok:s.loose.append(s.text.strip())
        while s.stack:
            t,_=s.stack.pop()
            if t==tag:break
    def handle_data(s,d):
        if s.text is not None:s.text+=d
work=Path(tempfile.mkdtemp(prefix='tc0937-'));db=work/'server.db';port=18790
server=subprocess.Popen([str(base/'timeclock-server'),str(db),str(port)],env=dict(os.environ,DEVICE_KEY='localtestingkey0123456789'),stdout=subprocess.PIPE,stderr=subprocess.PIPE)
try:
    time.sleep(.3);assert server.poll() is None,server.stderr.read().decode()
    con=sqlite3.connect(db);con.execute("INSERT INTO employees(id,name,email) VALUES(1,'Sample Person','sample@example.test')");con.execute("INSERT INTO employee_roles VALUES(1,'manager')");con.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
    token='e'*64;con.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+600)",(token,));con.commit();con.close()
    get=lambda p,auth=True:urllib.request.urlopen(urllib.request.Request(f'http://127.0.0.1:{port}{p}',headers={'Cookie':'tc_session='+token} if auth else {})).read().decode()
    css=get('/ui.css',auth=False)
    for rule in ('.actions{display:flex','.actions+*,form+*{margin-top:var(--s4)}','td>button{min-height:var(--control-h-sm)',':where(#status,#message,#result,#state,p[role=status]):empty','.is-err:not(:empty)'):
        assert rule in css,rule
    hrefs=['/login']+[h for _,h,_,_ in gen_ui_inc.links()];count=0
    for h in hrefs:
        p=Buttons();p.feed(get(h,auth=h!='/login'));assert not p.loose,f'{h}: loose buttons {p.loose}';count+=1
    print(f'PASS buttons in .actions rows on {count} pages, lint catches loose buttons, spacing rules served')
finally:
    server.terminate();server.wait(timeout=5)
