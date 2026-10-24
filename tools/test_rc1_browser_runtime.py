#!/usr/bin/env python3
"""Exercise rendered manager pages in Chromium; fail on caught visible JS errors."""
import os, socket, sqlite3, subprocess, tempfile, time
from pathlib import Path
try:
 from playwright.sync_api import sync_playwright
except ImportError:
 raise SystemExit("SKIP: Playwright not installed; browser runtime test requires Chromium and Playwright")
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='rc1-browser-') as tmp:
    db=Path(tmp)/'clock.sqlite'
    with socket.socket() as s:
        s.bind(('127.0.0.1',0)); port=s.getsockname()[1]
    server=subprocess.Popen([str(ROOT/'timeclock-server'),str(db),str(port)],cwd=ROOT,env={**os.environ,'DEVICE_KEY':'browser-test-key'},stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    try:
        import urllib.request
        for _ in range(100):
            try:urllib.request.urlopen(f'http://127.0.0.1:{port}/health',timeout=.3).close();break
            except OSError:time.sleep(.05)
        else:raise AssertionError('server did not start')
        with sqlite3.connect(db) as c:
            c.execute("INSERT INTO employees(id,name,email) VALUES(1,'Test Manager','manager@example.test')")
            c.execute("INSERT INTO employee_roles(employee_id,role) VALUES(1,'manager')")
            c.execute("INSERT INTO manager_cards(uid,manager_id,active) VALUES('A1B2C3D4',1,1)")
            c.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
            c.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+600)",('d'*64,))
        with sync_playwright() as pw:
            browser=pw.chromium.launch(headless=True,executable_path='/usr/bin/chromium',args=['--no-sandbox'])
            ctx=browser.new_context(viewport={'width':390,'height':844})
            ctx.add_cookies([{'name':'tc_session','value':'d'*64,'url':f'http://127.0.0.1:{port}/'}])
            for path,title in [('/presence','Who is in'),('/pay-periods','Pay periods')]:
                page=ctx.new_page(); faults=[]
                page.on('pageerror',lambda error:faults.append(str(error)))
                page.goto(f'http://127.0.0.1:{port}{path}',wait_until='networkidle',timeout=15000)
                assert page.locator('h1').inner_text()==title,(path,page.locator('h1').inner_text())
                assert page.evaluate("typeof tc.datetime === 'function'"),f'{path}: missing tc.datetime'
                sample=page.evaluate("tc.datetime('2026-10-16T13:58:00Z')")
                assert '2026' in sample and ':' in sample,sample
                assert not faults,(path,faults)
                # A caught exception is presented in the DOM, not in console.
                errors=page.locator('.is-err:visible').all_inner_texts()
                assert not errors,(path,errors)
                page.close()
            browser.close()
        print('PASS: mobile Chromium runtime, datetime and visible error checks')
    finally:
        server.terminate()
        try:server.wait(timeout=3)
        except subprocess.TimeoutExpired:server.kill();server.wait()
