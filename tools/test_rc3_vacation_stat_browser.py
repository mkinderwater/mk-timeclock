"""Optional browser checks for vacation/stat breakdown, history and employee timesheets."""
import ctypes,ctypes.util,os,socket,sqlite3,subprocess,tempfile,time,urllib.request
from pathlib import Path
root=Path(__file__).resolve().parents[1]
script=r'''
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright'),assert=require('node:assert/strict');
(async()=>{const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH,args:['--no-sandbox'],headless:true});try{
 const context=await browser.newContext();await context.addCookies([{name:'tc_session',value:'d'.repeat(64),url:process.env.TEST_URL}]);const page=await context.newPage(),errors=[];page.on('pageerror',e=>errors.push(e.message));
 for(const width of [390,1280]){
  await page.setViewportSize({width,height:900});await page.goto(process.env.TEST_URL+'/time-off-requests');await page.waitForFunction(()=>document.getElementById('pastRequests').textContent.includes('4 vacation days + 1 stat holiday'));
  const record=page.locator('#pastRequests tbody tr').first();assert.equal((await record.locator('td').nth(2).textContent()).trim(),'4 + 1 stat');const details=record.locator('details');assert.equal(await details.evaluate(d=>d.open),false);await details.locator('summary').click();assert(await details.getByText('Stat holiday: Thanksgiving (2026-10-12). Not counted as vacation.',{exact:true}).isVisible());
  await page.locator('#openVacation').click();const form=page.locator('#recordTimeoff');await form.locator('[name=employee_id]').selectOption('2');await form.locator('[name=starts]').fill('2026-10-12');await form.locator('[name=ends]').fill('2026-10-16');
  await page.waitForFunction(()=>document.getElementById('dayshint').textContent.includes('4 vacation days + 1 stat holiday'));
  assert((await page.locator('#dayshint').textContent()).includes('Stat holiday: Thanksgiving (2026-10-12). Not counted as vacation.'));
  assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1));
  if(width===390&&process.env.SCREENSHOT_PATH)await page.screenshot({path:process.env.SCREENSHOT_PATH,fullPage:true});
  await form.locator('[name=ends]').fill('2026-10-12');await page.waitForFunction(()=>document.getElementById('dayshint').textContent.includes('2026-10-12 is Thanksgiving'));
  await page.goto(process.env.TEST_URL+'/time-off/'+'e'.repeat(64));await page.waitForFunction(()=>document.getElementById('history').textContent.includes('4 vacation days + 1 stat holiday'));
  assert((await page.locator('#history').textContent()).includes('Not counted as vacation'));
  const employee=page.locator('#newrequest');await employee.locator('[name=starts]').fill('2026-10-12');await employee.locator('[name=ends]').fill('2026-10-16');await page.waitForFunction(()=>document.getElementById('dayshint').textContent.includes('4 vacation days + 1 stat holiday'));
  await employee.locator('[name=ends]').fill('2026-10-12');await page.waitForFunction(()=>document.getElementById('dayshint').textContent.includes('2026-10-12 is Thanksgiving'));
  await page.goto(process.env.TEST_URL+'/review/'+'e'.repeat(64));await page.waitForFunction(()=>document.querySelectorAll('#entries tr').length===5);
  const rows=await page.locator('#entries tr').allTextContents();assert.equal(rows.filter(x=>x.includes('Thanksgiving')).length,1);assert.equal(rows.filter(x=>x.includes('Approved time off: vacation')).length,4);assert(rows.find(x=>x.includes('Thanksgiving')).includes('Not counted as vacation'));
 }
 await page.goto(process.env.TEST_URL+'/review/'+'e'.repeat(64));await page.waitForFunction(()=>document.getElementById('reviewstate').textContent.includes('No timesheet approval required'));assert.equal(await page.locator('#approve').isVisible(),false);
 await page.goto(process.env.TEST_URL+'/pay-periods');await page.locator('#rows tr').filter({hasText:'2026-10-12'}).getByRole('button',{name:'Review',exact:true}).click();await page.waitForFunction(()=>document.getElementById('people').textContent.includes('Not required'));const leaveRow=page.locator('#people tr').filter({hasText:'Sarah Chen'});assert.equal(await leaveRow.locator('button').count(),0);assert.equal(await leaveRow.evaluate(r=>r.classList.contains('row-check')),false);assert.equal(await page.locator('#closeReason').textContent(),'');
 for(const width of [390,1280]){await page.setViewportSize({width,height:900});await page.goto(process.env.TEST_URL+'/review/'+'c'.repeat(64));await page.waitForFunction(()=>document.getElementById('summary').textContent.includes('17.00 hours'));assert.equal(await page.locator('#approve').isVisible(),true);await page.goto(process.env.TEST_URL+'/pay-periods');await page.locator('#rows tr').filter({hasText:'2026-09-01'}).getByRole('button',{name:'Review',exact:true}).click();await page.waitForFunction(()=>document.getElementById('people').textContent.includes('Historical Worker'));assert.equal(await page.locator('#people').getByRole('button',{name:'Manager override',exact:true}).count(),1);assert.equal(await page.locator('#closePeriod').isEnabled(),false);}
 assert.deepEqual(errors,[]);console.log('PASS stat browser: previews and approved history show 4 vacation days + 1 stat holiday; named stat-only messages; employee timesheet has one holiday row and subtext; 390/1280px; no script errors');
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
        crypt=ctypes.CDLL(ctypes.util.find_library('crypt')).crypt;crypt.argtypes=[ctypes.c_char_p,ctypes.c_char_p];crypt.restype=ctypes.c_char_p
        hashed=crypt(b'e'*64,b'$6$rounds=5000$payreview2026$').decode()
        with sqlite3.connect(path) as db:
            db.execute("INSERT INTO employees(id,name,email,vacation_days,attendance_mode) VALUES(1,'Manager','manager@example.test',NULL,'punch_required'),(2,'Sarah Chen','sarah@example.test',15,'vacation_only')")
            db.execute("INSERT INTO employee_roles VALUES(1,'manager')")
            db.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
            db.execute("INSERT INTO manager_cards VALUES('A1B2C3D4',1,1)")
            db.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+600)",('d'*64,))
            db.execute('UPDATE review_send_settings SET enabled=0')
            db.execute("INSERT INTO statutory_holidays VALUES('2026-10-12','Thanksgiving')")
            db.execute("INSERT INTO schedule_profiles(employee_id,pay_type,schedule_mode,weekday_mask) VALUES(2,'salary','recurring_weekly',31)")
            db.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status,requested_days,return_date) VALUES(2,'vacation','2026-10-12','2026-10-16','approved',4,'2026-10-19')")
            pid=db.execute("INSERT INTO pay_periods(starts,ends,status) VALUES('2026-10-12','2026-10-16','closed') RETURNING id").fetchone()[0]
            db.execute("INSERT INTO timeoff_links VALUES(2,?,CAST(strftime('%s','now') AS INTEGER)+600,0)",(hashed,))
            db.execute("INSERT INTO review_links(employee_id,period_id,token_hash,expires_at,created_by,created_at) VALUES(2,?,?,CAST(strftime('%s','now') AS INTEGER)+600,1,CAST(strftime('%s','now') AS INTEGER))",(pid,hashed))
            db.execute("INSERT INTO employees(id,name,attendance_mode,created_at) VALUES(3,'Historical Worker','punch_required','2026-08-01T00:00:00Z')")
            db.execute("UPDATE employees SET attendance_mode='vacation_only' WHERE id=3")
            historical=db.execute("INSERT INTO pay_periods(starts,ends) VALUES('2026-09-01','2026-09-15') RETURNING id").fetchone()[0]
            history_hash=crypt(b'c'*64,b'$6$rounds=5000$payreview2026$').decode()
            db.execute("INSERT INTO review_links(employee_id,period_id,token_hash,expires_at,created_by,created_at) VALUES(3,?,?,CAST(strftime('%s','now') AS INTEGER)+600,1,CAST(strftime('%s','now') AS INTEGER))",(historical,history_hash))
            for index,(at,kind) in enumerate([('2026-09-03T15:00:00Z','IN'),('2026-09-03T23:00:00Z','OUT'),('2026-09-04T15:00:00Z','IN'),('2026-09-05T00:00:00Z','OUT')]):
                db.execute("INSERT INTO punch_corrections(employee_id,manual_id,action,effective_at,kind,reason,manager_id) VALUES(3,?,'add',?,?,'Verified historical shift',1)",('history-'+str(index),at,kind))
        js=Path(tmp)/'browser.cjs';js.write_text(script);subprocess.run(['node',str(js)],env={**os.environ,'TEST_URL':base},check=True)
    finally:proc.terminate();proc.communicate(timeout=5)
