"""Optional browser: waiting-first manager page, history filters/paging and simple entry dialog."""
import datetime,os,socket,sqlite3,subprocess,tempfile,time,urllib.request
from pathlib import Path
root=Path(__file__).resolve().parents[1]
script=r'''
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright'),assert=require('node:assert/strict');
(async()=>{const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH,args:['--no-sandbox'],headless:true});try{
 const context=await browser.newContext();await context.addCookies([{name:'tc_session',value:'d'.repeat(64),url:process.env.TEST_URL}]);const page=await context.newPage(),errors=[];page.on('pageerror',e=>errors.push(e.message));page.on('dialog',d=>d.accept('Reviewed'));
 for(const width of [320,390,1280]){
  await page.setViewportSize({width,height:900});await page.goto(process.env.TEST_URL+'/time-off-requests');await page.waitForFunction(()=>document.querySelectorAll('#waitingRequests tbody tr').length===2&&document.querySelectorAll('#pastRequests tbody tr').length===50&&document.getElementById('filterYear').options.length>=4);
  assert.equal(await page.locator('main section').first().getAttribute('id'),'waitingSection');assert.equal(await page.locator('#vacationDialog').evaluate(d=>d.open),false);assert.equal(await page.locator('#recordTimeoff').isVisible(),false);assert(await page.evaluate(()=>document.documentElement.scrollWidth<=innerWidth+1));
  assert.equal(await page.locator('#waitingRequests tbody tr').filter({hasText:'Elena'}).getByText('Dates have passed',{exact:true}).count(),1);assert.equal(await page.locator('#waitingRequests tbody tr').filter({hasText:'Sarah'}).getByText('Dates have passed',{exact:true}).count(),0);
  if(width===390&&process.env.SCREENSHOT_PATH)await page.screenshot({path:process.env.SCREENSHOT_PATH,fullPage:true});
 }
 assert.equal(await page.locator('#pastRequests thead th').count(),5);assert.equal(await page.getByRole('button',{name:'Cancel approval',exact:true}).count(),0);assert(await page.getByRole('button',{name:'Cancel',exact:true}).count()>0);
 const detail=page.locator('#pastRequests .vacation-details').first();assert.equal(await detail.evaluate(d=>d.open),false);await detail.locator('summary').click();assert.equal(await detail.evaluate(d=>d.open),true);await detail.locator('summary').click();
 await page.locator('#filterEmployee').selectOption('2');await page.locator('#filterYear').selectOption('2026');await page.waitForFunction(()=>document.querySelectorAll('#pastRequests tbody tr').length===3);
 const dates=await page.locator('#pastRequests tbody tr td:nth-child(2)').allTextContents();assert(dates[0].startsWith('2026-10-15'));assert(dates[1].startsWith('2026-10-05'));assert(dates[2].startsWith('2026-09-21'));assert.equal(await page.locator('#waitingRequests tbody tr').count(),2);
 await page.locator('#filterYear').selectOption('');await page.waitForFunction(()=>document.getElementById('pastPager').textContent.includes('of 63'));await page.getByRole('button',{name:'Next past page',exact:true}).click();await page.waitForFunction(()=>document.getElementById('pastPager').textContent.includes('51 to 63 of 63'));assert.equal(await page.locator('#pastRequests tbody tr').count(),13);
 await page.locator('#filterYear').selectOption('2026');await page.waitForFunction(()=>document.querySelectorAll('#pastRequests tbody tr').length===3);assert.equal(await page.locator('#pastPager').textContent(),'');
 await page.locator('#filterEmployee').selectOption('3');await page.locator('#filterYear').selectOption('2090');await page.waitForFunction(()=>document.querySelectorAll('#upcomingRequests tbody tr').length===1&&document.getElementById('pastRequests').textContent.includes('No vacation records'));
 assert.equal(await page.locator('#waitingRequests tbody tr').count(),2);
 // Approving an old waiting request removes it from the queue even when history filters exclude it.
 const waiting=page.locator('#waitingRequests tbody tr').filter({hasText:'Elena'});await waiting.getByRole('button',{name:'Approve',exact:true}).click();await page.waitForFunction(()=>document.querySelectorAll('#waitingRequests tbody tr').length===1);assert((await page.locator('#status').textContent()).includes('Decision saved.'));
 await page.locator('#openVacation').click();assert.equal(await page.locator('#vacationDialog').getByRole('button',{name:'Cancel',exact:true}).count(),0);await page.locator('#closeVacation').click();assert.equal(await page.locator('#vacationDialog').evaluate(d=>d.open),false);await page.locator('#openVacation').click();assert.equal(await page.locator('#leaveEmployee').inputValue(),'3');const form=page.locator('#recordTimeoff');await form.locator('[name=starts]').fill('2090-01-03');await page.waitForFunction(()=>document.querySelector('#recordTimeoff [name=ends]').value==='2090-01-03'&&document.querySelector('#recordTimeoff [name=requested_days]').value==='1');assert.equal(await form.locator('[name=return_date]').inputValue(),'2090-01-04');assert.equal(await form.locator('[name=return_date]').getAttribute('required'),null);
 // Leave the suggested return blank deliberately: server still accepts its own suggestion.
 await form.locator('[name=return_date]').fill('');await form.getByRole('button',{name:'Record vacation',exact:true}).click();await page.waitForFunction(()=>!document.getElementById('vacationDialog').open&&document.getElementById('recordStatus').textContent.includes('Vacation recorded.'));await page.waitForFunction(()=>document.querySelectorAll('#upcomingRequests tbody tr').length===2);
 await page.locator('#openVacation').click();assert.equal(await form.locator('[name=starts]').inputValue(),'');await page.keyboard.press('Escape');assert.equal(await page.locator('#vacationDialog').evaluate(d=>d.open),false);assert.deepEqual(errors,[]);
 console.log('PASS manager page browser: queue before history; closed dialog; 320/390/1280px; date ordering; filters retain waiting requests; paging and reset; approval leaves queue; one-day entry with suggested return; save/close/reset/Escape; no script errors');
}finally{await browser.close()}})().catch(e=>{console.error(e);process.exitCode=1});
'''
with tempfile.TemporaryDirectory() as tmp:
    path=Path(tmp)/'clock.db'
    with socket.socket() as sock:sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
    proc=subprocess.Popen([str(root/'timeclock-server'),str(path),str(port)],cwd=root,env={**os.environ,'DEVICE_KEY':'test-device-key-12345','TZ':'America/Edmonton'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        base=f'http://127.0.0.1:{port}'
        for _ in range(100):
            try:urllib.request.urlopen(base+'/health',timeout=.2).close();break
            except OSError:time.sleep(.04)
        with sqlite3.connect(path) as db:
            db.execute("INSERT INTO employees(id,name,email) VALUES(1,'Manager','manager@example.test'),(2,'Elena',''),(3,'Sarah','')")
            db.execute("INSERT INTO employee_roles VALUES(1,'manager')")
            db.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
            db.execute("INSERT INTO manager_cards VALUES('A1B2C3D4',1,1)")
            db.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+600)",('d'*64,))
            db.execute('UPDATE review_send_settings SET enabled=0')
            for day in ['2026-09-21','2026-10-15','2026-10-05']:
                db.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status,requested_days) VALUES(2,'vacation',?,?,'approved',1)",(day,day))
            for i in range(60):
                day=(datetime.date(2025,1,1)+datetime.timedelta(days=i)).isoformat()
                db.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status,requested_days) VALUES(2,'vacation',?,?,'approved',1)",(day,day))
            for eid,day,status in [(3,'2090-01-02','approved'),(3,'2090-02-01','pending'),(2,'2026-10-01','pending')]:db.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends,status,requested_days) VALUES(?,'vacation',?,?,?,1)",(eid,day,day,status))
        js=Path(tmp)/'browser.cjs';js.write_text(script);subprocess.run(['node',str(js)],env={**os.environ,'TEST_URL':base},check=True)
        with sqlite3.connect(path) as db:
            assert db.execute("SELECT return_date FROM timeoff_requests WHERE employee_id=3 AND starts='2090-01-03'").fetchone()[0]=='2090-01-04'
            assert db.execute('SELECT count(*) FROM email_queue').fetchone()[0]==0
    finally:proc.terminate();proc.communicate(timeout=5)
