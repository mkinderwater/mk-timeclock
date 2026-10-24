"""Live manager-page spacing, phone layout and employee control checks with Playwright."""
import ctypes,ctypes.util,json,os,socket,sqlite3,subprocess,tempfile,time,urllib.request
from pathlib import Path
root=Path(__file__).resolve().parents[1]
script=r'''
const {chromium}=require(process.env.PLAYWRIGHT_MODULE||'playwright');
const assert=require('node:assert/strict');
(async()=>{
 const browser=await chromium.launch({executablePath:process.env.CHROMIUM_PATH,args:['--no-sandbox'],headless:true});
 try{
  const context=await browser.newContext();
  await context.addCookies([{name:'tc_session',value:'d'.repeat(64),url:process.env.TEST_URL}]);
  const page=await context.newPage();const errors=[];page.on('pageerror',e=>errors.push(e.message));
  const routes=['/card-status','/employees','/','/time-off-requests','/rejected-cards','/clock-grace','/holidays'];
  for(const width of [320,390]){
   await page.setViewportSize({width,height:844});
   for(const route of routes){
    await page.goto(process.env.TEST_URL+route);
    await page.waitForFunction(()=>document.querySelectorAll('table tr td').length>0,{},{timeout:5000}).catch(e=>{throw Error(route+': '+e.message+' '+errors.join('; '))});
    const result=await page.evaluate(()=>{
     const buttons=[...document.querySelectorAll('table button')].filter(b=>b.offsetWidth);
     const badWords=[];
     for(const td of document.querySelectorAll('table td')){
      const walk=document.createTreeWalker(td,NodeFilter.SHOW_TEXT);let node;
      while(node=walk.nextNode())for(const m of node.textContent.matchAll(/[A-Za-z0-9]+/g)){
       const range=document.createRange();range.setStart(node,m.index);range.setEnd(node,m.index+m[0].length);
       const rects=[...range.getClientRects()].filter(r=>r.width&&r.height);
       if(new Set(rects.map(r=>Math.round(r.y))).size>1)badWords.push(m[0]);
      }
     }
     return {badWords,buttons:buttons.map(b=>({text:b.textContent,whiteSpace:getComputedStyle(b).whiteSpace})),viewport:innerWidth,document:document.documentElement.scrollWidth};
    });
    assert.deepEqual(result.badWords,[],width+' '+route+' split words');
    if(route==='/time-off-requests'){
     const dates=await page.evaluate(()=>[...document.querySelectorAll('.timeoff-table-scroll td:nth-child(2)')].map(td=>getComputedStyle(td).whiteSpace));
     assert(dates.length>0);assert(dates.every(x=>x==='nowrap'));
    }

    assert(result.document<=result.viewport+1,width+' '+route+' viewport overflow '+JSON.stringify(result));
    for(const b of result.buttons)assert.equal(b.whiteSpace,'nowrap',route+' '+b.text);
   }
  }
  await page.goto(process.env.TEST_URL+'/review/'+'e'.repeat(64));
  await page.locator('#approve').waitFor({timeout:5000});
  const appearance=await page.evaluate(()=>({approve:getComputedStyle(document.getElementById('approve')).backgroundColor,wrong:getComputedStyle(document.getElementById('wrong')).backgroundColor}));
  assert.notEqual(appearance.approve,appearance.wrong);
  for(const route of ['/time-off','/time-off/'+'e'.repeat(64)]){
   await page.goto(process.env.TEST_URL+route);
   await page.waitForFunction(()=>document.querySelector('section form'));
   const form=await page.evaluate(()=>{const f=document.querySelector('section form');return {border:getComputedStyle(f).borderTopWidth,padding:getComputedStyle(f).paddingTop}});
   assert.equal(form.border,'0px');assert.equal(form.padding,'0px');
  }


  // Verify the current controls in a real browser, including mobile presentation.
  await page.goto(process.env.TEST_URL+'/device-health');
  await page.locator('#healthrows tr').last().waitFor();
  const bottom=await page.locator('#healthrows tr').last().evaluate(tr=>[...tr.children].map(c=>getComputedStyle(c).borderBottomWidth));
  assert.deepEqual(bottom,['1px','1px']);
  await page.goto(process.env.TEST_URL+'/time-off-requests');
  assert.equal(await page.locator('#vacation-download').evaluate(b=>b.tagName),'BUTTON');
  assert.equal(await page.locator('#vacation-download').evaluate(b=>b.parentElement.className),'actions');
  const controlStyles=await page.evaluate(()=>{const b=document.getElementById('vacation-download'),ref=document.createElement('button');ref.type='submit';ref.textContent=b.textContent;b.parentElement.append(ref);const keys=['fontFamily','fontSize','fontWeight','lineHeight','minHeight','paddingLeft','paddingRight','borderRadius','backgroundColor','color'];const values=el=>keys.map(k=>getComputedStyle(el)[k]);const result=[values(b),values(ref)];ref.remove();return result});
  assert.deepEqual(controlStyles[0],controlStyles[1]);
  await page.fill('#vacation-year','2026');
  const file=page.waitForEvent('download');await page.click('#vacation-download');
  const downloaded=await file;assert.equal(downloaded.suggestedFilename(),'Vacation_2026.csv');
  assert((await require('node:fs/promises').readFile(await downloaded.path(),'utf8')).includes('Vacation used / allowed'));
  await page.goto(process.env.TEST_URL+'/ntp');
  assert.equal(await page.locator('#timezone').evaluate(e=>e.tagName),'SELECT');
  assert.equal(await page.locator('#timezone option[value="America/Edmonton"]').textContent(),'Calgary / Edmonton (Mountain Time)');
  await page.selectOption('#timezone','America/Vancouver');
  assert.equal(await page.inputValue('#timezone'),'America/Vancouver');
  await page.goto(process.env.TEST_URL+'/smtp');
  await page.waitForFunction(()=>document.getElementById('stored').textContent.length>0);
  await page.fill('#host','relay.example.test');await page.selectOption('#tls_mode','none');
  assert.equal(await page.inputValue('#port'),'25');
  await page.fill('#sender','clock@example.test');await page.fill('#sender_name','iCube Development Timeclock');
  await page.getByRole('button',{name:'Save email settings',exact:true}).click();
  await page.waitForFunction(()=>document.getElementById('status').textContent.includes('Saved.'));
  await page.reload();await page.waitForFunction(()=>document.getElementById('sender_name').value==='iCube Development Timeclock');
  await page.route('**/api/v1/admin/manager-scan/status',route=>route.fulfill({json:{status:'scanned',uid:'CAFEBABE'}}));
  await page.goto(process.env.TEST_URL+'/manager-cards');
  await page.click('#arm');
  await page.waitForFunction(()=>document.getElementById('arm').textContent==='CAFEBABE');
  assert.equal(await page.locator('code#uid').count(),0);
  assert.equal(await page.locator('#save').isEnabled(),true);
  await page.goto(process.env.TEST_URL+'/employees');
  await page.fill('#employeeSearch','KINDERWATER');assert.equal(await page.locator('#rows tr:visible').count(),1);await page.fill('#employeeSearch','missing employee');assert.equal(await page.locator('#rows tr:visible').count(),0);assert.equal(await page.locator('#searchCount').textContent(),'No employees match this name.');await page.fill('#employeeSearch','');
  await page.click('#addEmployee');await page.click('#scan');
  await page.waitForFunction(()=>document.getElementById('scan').textContent==='CAFEBABE');
  assert.equal(await page.inputValue('#uid'),'CAFEBABE');
  await page.click('#closeEmployee');await page.click('#addEmployee');
  assert.equal(await page.locator('#scan').textContent(),'Scan card (30 seconds)');
  await page.goto(process.env.TEST_URL+'/card-status');
  await page.getByRole('button',{name:'Replace',exact:true}).first().click();
  await page.click('#scanReplace');
  await page.waitForFunction(()=>document.getElementById('scanReplace').textContent==='CAFEBABE');
  assert.equal(await page.inputValue('#newUid'),'CAFEBABE');
  await page.goto(process.env.TEST_URL+'/employees');
  await page.click('#addEmployee');
  await page.setViewportSize({width:1280,height:1000});
  const aligned=await page.evaluate(()=>{const ids=['phone','vacation_days'];return ids.map(id=>{const r=document.getElementById(id).getBoundingClientRect();return {y:r.y,height:r.height}})});
  assert.equal(aligned[0].y,aligned[1].y);assert.equal(aligned[0].height,aligned[1].height);
  assert.equal(await page.locator('#vacation_days').evaluate(e=>e.parentElement.querySelector('small')),null);
  await page.setViewportSize({width:390,height:844});
  await page.fill('#first_name','Zero');await page.fill('#last_name','Allowance');await page.fill('#position','Technician');await page.fill('#vacation_days','0');await page.fill('#employee_number','EMP-0009');
  await page.getByRole('button',{name:'Save employee',exact:true}).click();
  await page.waitForFunction(()=>!document.getElementById('employeeDialog').open);
  await page.getByRole('button',{name:'Edit employee Allowance, Zero',exact:true}).click();
  assert.equal(await page.inputValue('#vacation_days'),'0');assert.equal(await page.inputValue('#employee_number'),'EMP-0009');
  await page.fill('#vacation_days','15.5');await page.getByRole('button',{name:'Save employee',exact:true}).click();
  await page.waitForFunction(()=>!document.getElementById('employeeDialog').open);
  await page.getByRole('button',{name:'Edit employee Allowance, Zero',exact:true}).click();
  assert.equal(await page.inputValue('#vacation_days'),'15.5');
  await page.goto(process.env.TEST_URL+'/review-links');
  await page.waitForFunction(()=>document.getElementById('employee').options.length>0);
  assert.equal(await page.locator('#autoTime').getAttribute('type'),'time');
  await page.uncheck('#autoEnabled');await page.fill('#autoDays','0');await page.fill('#autoTime','08:00');await page.click('#autoSave');
  await page.waitForFunction(()=>document.getElementById('autoStatus').textContent.includes('saved'));
  await page.reload();await page.waitForFunction(()=>document.getElementById('autoTime').value==='08:00');
  assert.equal(await page.inputValue('#autoDays'),'0');
  await page.goto(process.env.TEST_URL+'/payroll');
  await page.waitForFunction(()=>document.getElementById('frequency').textContent.length>0);
  assert.equal(await page.locator('[name=send_time]').getAttribute('type'),'time');
  await page.fill('[name=anchor]','2026-10-15');await page.fill('[name=before]','0');await page.fill('[name=send_time]','15:00');await page.fill('[name=recipient]','payroll@example.test');await page.uncheck('[name=enabled]');
  const armed=page.waitForResponse(r=>r.url().endsWith('/api/v1/admin/payroll/arm')&&r.request().method()==='POST');
  await page.getByRole('button',{name:'Save (manager card required)',exact:true}).click();assert.equal((await armed).status(),200);
  const approved=await context.request.post(process.env.TEST_URL+'/api/v1/clock/scan',{headers:{'X-Clock-ID':'clock-test','X-Device-Key':'test-device-key-12345'},data:{clock_id:'clock-test',card_uid:'A1B2C3D4'}});
  assert.equal((await approved.json()).result,'APPROVED');
  await page.waitForFunction(()=>document.getElementById('status').textContent.includes('Payroll delivery saved'));
  await page.reload();await page.waitForFunction(()=>document.querySelector('[name=send_time]').value==='15:00');
  assert.equal(await page.inputValue('[name=before]'),'0');
  await page.goto(process.env.TEST_URL+'/time-off/'+'e'.repeat(64));
  await page.waitForFunction(()=>document.getElementById('vacationUsage').textContent.includes('0 of 0 days used'));
  await page.fill('[name=starts]','2026-11-02');await page.locator('[name=starts]').dispatchEvent('change');
  await page.fill('[name=ends]','2026-11-03');await page.locator('[name=ends]').dispatchEvent('change');
  await page.waitForFunction(()=>document.querySelector('[name=requested_days]').value==='2');
  await page.getByRole('button',{name:'Request vacation',exact:true}).click();
  await page.waitForFunction(()=>document.getElementById('status').textContent.includes('Vacation request submitted'));
  // Visit every manager page at phone and desktop widths, including empty states.
  await page.goto(process.env.TEST_URL+'/');
  const managerRoutes=await page.locator('.nav a').evaluateAll(links=>links.map(a=>a.getAttribute('href')));
  for(const width of [320,390,1280]){
   await page.setViewportSize({width,height:1000});
   for(const route of managerRoutes){
    await page.goto(process.env.TEST_URL+route);await page.waitForTimeout(100);
    const failures=await page.evaluate(()=>{
     const visible=e=>e.getClientRects().length&&getComputedStyle(e).visibility!=='hidden';const failures=[];
     if(document.documentElement.scrollWidth>innerWidth+1)failures.push('viewport overflow');
     for(const b of document.querySelectorAll('main button'))if(visible(b)&&!b.closest('.actions,td,th'))failures.push('button outside actions: '+b.textContent);
     for(const a of document.querySelectorAll('main .actions'))if(visible(a)&&a.children.length){
      const previous=a.previousElementSibling;
      if(previous&&visible(previous)&&!a.closest('td,th,.section-head')){
       const gap=a.getBoundingClientRect().top-previous.getBoundingClientRect().bottom;
       if(gap<8)failures.push('crowded action row: '+a.textContent.trim());
      }
     }
     return failures;
    });
    assert.deepEqual(failures,[],width+' '+route);
   }
  }
  assert.deepEqual(errors,[]);
  console.log('PASS: all 29 manager pages at 320/390/1280px meet action spacing and overflow checks; populated phone tables have no split words; employee Approve is primary; sender-name save/reload, timezone select, health borders, CSV button, all scan labels; vacation creation/edit, over-allowance request, both send-time save/reload; no browser script errors');
 }finally{await browser.close()}
})().catch(e=>{console.error(e);process.exitCode=1});
'''
with tempfile.TemporaryDirectory() as tmp:
    path=Path(tmp)/'clock.db'
    with socket.socket() as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
    proc=subprocess.Popen([str(root/'timeclock-server'),str(path),str(port)],cwd=root,env={**os.environ,'DEVICE_KEY':'test-device-key-12345'},stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
    try:
        base=f'http://127.0.0.1:{port}'
        for _ in range(100):
            try:urllib.request.urlopen(base+'/health',timeout=.2).close();break
            except OSError:time.sleep(.04)
        crypt=ctypes.CDLL(ctypes.util.find_library('crypt'));crypt.crypt.argtypes=[ctypes.c_char_p,ctypes.c_char_p];crypt.crypt.restype=ctypes.c_char_p
        hashed=crypt.crypt(b'e'*64,b'$6$rounds=5000$payreview2026$').decode()
        with sqlite3.connect(path) as db:
            db.execute("INSERT INTO employees(id,name,email) VALUES(1,'Manager','manager@example.test')")
            db.execute("INSERT INTO employees(id,name,email,vacation_days) VALUES(2,'Alexanderthegreat Kinderwater','alex@example.test',0)")
            db.execute("INSERT INTO employee_roles VALUES(1,'manager')")
            db.execute("INSERT INTO user_accounts(employee_id,password_hash) VALUES(1,'x')")
            db.execute("INSERT INTO manager_cards(uid,manager_id,active) VALUES('A1B2C3D4',1,1)")
            db.execute("INSERT INTO registered_clocks(clock_id,device_key,active) VALUES('clock-test','test-device-key-12345',1)")
            db.execute("INSERT INTO cards VALUES('E115305B',2,1)")
            db.execute("INSERT INTO login_sessions VALUES(?,1,CAST(strftime('%s','now') AS INTEGER)+600)",('d'*64,))
            db.execute("INSERT INTO unknown_cards VALUES('DEADBEEFABCD1234','clock-test',strftime('%Y-%m-%dT%H:%M:%fZ','now'),strftime('%Y-%m-%dT%H:%M:%fZ','now'),1)")
            db.execute("INSERT INTO timeoff_requests(employee_id,kind,starts,ends) VALUES(2,'vacation','2026-10-22','2026-10-23')")
            db.execute("INSERT INTO statutory_holidays VALUES('2026-12-25','Christmas Day')")
            db.execute("INSERT INTO pay_periods(starts,ends) VALUES('2026-09-01','2026-09-15')")
            pid=db.execute('SELECT last_insert_rowid()').fetchone()[0]
            db.execute("INSERT INTO review_links VALUES(2,?,?,CAST(strftime('%s','now') AS INTEGER)+600,1,CAST(strftime('%s','now') AS INTEGER),0)",(pid,hashed))
            db.execute("INSERT INTO timeoff_links VALUES(2,?,CAST(strftime('%s','now') AS INTEGER)+600,0)",(hashed,))
        js=Path(tmp)/'browser.cjs';js.write_text(script)
        subprocess.run(['node',str(js)],env={**os.environ,'TEST_URL':base},check=True)
    finally:proc.terminate();proc.communicate(timeout=5)
