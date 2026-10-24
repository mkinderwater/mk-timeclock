"""Optional real-browser manager leave workflow and mobile layout."""
import json, os, socket, sqlite3, subprocess, tempfile, time, urllib.request
from pathlib import Path
root=Path(__file__).resolve().parents[1]
script=r'''
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const assert=require('node:assert/strict');
(async()=>{const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH,args:['--no-sandbox'],headless:true});try{
 const context=await browser.newContext();await context.addCookies([{name:'tc_session',value:'d'.repeat(64),url:process.env.TEST_URL}]);
 const page=await context.newPage(),errors=[];page.on('pageerror',e=>errors.push(e.message));
 for(const width of [320,390,1280]){
  await page.setViewportSize({width,height:900});await page.goto(process.env.TEST_URL+'/time-off-requests');
  await page.waitForFunction(()=>document.querySelector('#leaveEmployee').options.length===4);
  assert(await page.locator('#leaveEmployee').textContent().then(t=>t.includes('Former Employee (#3) - inactive')));
  assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1));if(width===390&&process.env.SCREENSHOT_PATH)await page.screenshot({path:process.env.SCREENSHOT_PATH,fullPage:true});
  assert.equal(await page.locator('#vacationDialog').evaluate(d=>d.open),false);assert.equal(await page.locator('main section h2').first().textContent(),'Waiting for approval');await page.locator('#openVacation').click();const f=page.locator('#recordTimeoff');
  for(const name of ['employee_id','kind','starts','ends','requested_days','return_date','note'])assert.equal(await f.locator('[name='+name+']').count(),1);
  assert.equal(await f.locator('select[name=kind]').count(),0);assert.equal(await f.locator('[name=requested_days]').getAttribute('type'),'hidden');
 }
 const f=page.locator('#recordTimeoff');
 await f.locator('[name=employee_id]').selectOption('2');await f.locator('[name=starts]').fill('2026-01-02');await page.waitForFunction(()=>document.querySelector('#recordTimeoff [name=ends]').value==='2026-01-02');assert.equal(await f.locator('[name=return_date]').getAttribute('required'),null);await f.locator('[name=starts]').fill('2026-01-01');await f.locator('[name=ends]').fill('2026-01-06');
 await page.waitForFunction(()=>document.querySelector('#recordTimeoff [name=requested_days]').value==='3');
 assert.equal(await f.locator('[name=return_date]').inputValue(),'2026-01-07');
 await f.locator('[name=note]').fill('Approved by email before clock installation');await f.getByRole('button',{name:'Record vacation',exact:true}).click();
 await page.waitForFunction(()=>document.getElementById('recordStatus').textContent.includes('Vacation recorded. No email sent.'));
 await page.waitForFunction(()=>document.getElementById('pastRequests').textContent.includes('Recorded by manager'));
 assert((await page.locator('#pastRequests').textContent()).includes('Approved by email before clock installation'));
 assert.equal(await f.locator('[name=employee_id]').inputValue(),'');
 // Duplicate save shows the API conflict, without replacing the existing record.
 await page.locator('#openVacation').click();
 await f.locator('[name=employee_id]').selectOption('2');await f.locator('[name=starts]').fill('2026-01-01');await f.locator('[name=ends]').fill('2026-01-06');
 await page.waitForFunction(()=>document.querySelector('#recordTimeoff [name=requested_days]').value==='3');await f.getByRole('button',{name:'Record vacation',exact:true}).click();
 await page.waitForFunction(()=>document.getElementById('recordError').textContent.includes('overlap'));
 assert.equal(await f.getByRole('button',{name:'Record vacation',exact:true}).isEnabled(),true);
 assert.deepEqual(errors,[]);console.log('PASS manager leave browser: 320/390/1280px; waiting first; closed dialog; shared fields; one-day default; optional suggested return; past dates; save/close/list; duplicate feedback; no script errors');
}finally{await browser.close()}})().catch(e=>{console.error(e);process.exitCode=1});
'''
with tempfile.TemporaryDirectory() as tmp:
    path=Path(tmp)/'clock.db'
    with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    proc=subprocess.Popen([str(root/'timeclock-server'),str(path),str(port)],cwd=root,env={**os.environ,'DEVICE_KEY':'test-device-key-12345','TZ':'America/Edmonton'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        base=f'http://127.0.0.1:{port}'
        for _ in range(100):
            try:urllib.request.urlopen(base+'/health',timeout=.2).close();break
            except OSError:time.sleep(.04)
        with sqlite3.connect(path) as db:
            db.execute("INSERT INTO employees(id,name,email,vacation_days) VALUES(1,'Manager','manager@example.test',NULL),(2,'Sarah Chen','sarah@example.test',0),(3,'Former Employee','',NULL)")
            db.execute('UPDATE employees SET active=0 WHERE id=3')
            db.execute("INSERT INTO employee_roles VALUES(1,'manager')")
            db.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
            db.execute("INSERT INTO manager_cards VALUES('A1B2C3D4',1,1)")
            db.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+600)",('d'*64,))
            db.execute('UPDATE review_send_settings SET enabled=0')
            db.execute("INSERT INTO statutory_holidays VALUES('2026-01-01','New Year')")
            db.execute("INSERT INTO schedule_profiles(employee_id,pay_type,schedule_mode,weekday_mask) VALUES(2,'salary','recurring_weekly',31)")
        js=Path(tmp)/'browser.cjs';js.write_text(script)
        subprocess.run(['node',str(js)],env={**os.environ,'TEST_URL':base},check=True)
        with sqlite3.connect(path) as db:
            assert db.execute("SELECT count(*) FROM timeoff_requests WHERE status='approved'").fetchone()[0]==1
            assert db.execute('SELECT count(*) FROM email_queue').fetchone()[0]==0
    finally:proc.terminate();proc.communicate(timeout=5)
