"""Execute shared page helper against success, error, and stalled fetch/body."""
import json,subprocess,sys,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(root/'tools'))
import gen_ui_inc
script=r'''
const vm=require('node:vm'),assert=require('node:assert/strict');
let mode='ok',calls=0,aborted=false,active=new Set(),last;
const context={window:{},document:{documentElement:{}},MutationObserver:class {observe(){}},AbortController,
 setTimeout(fn,ms){const t=setTimeout(()=>{active.delete(t);fn()},20);active.add(t);return t},
 clearTimeout(t){active.delete(t);clearTimeout(t)},
 fetch:async function(url,opts){calls++;last=opts;opts.signal.addEventListener('abort',()=>aborted=true);
  if(mode==='headers')return new Promise((resolve,reject)=>opts.signal.addEventListener('abort',()=>reject(Error('aborted'))));
  if(mode==='network')throw Error('Network unavailable');
  return {clone:()=>({arrayBuffer:()=>mode==='body'?new Promise(()=>{}):Promise.resolve(new ArrayBuffer(0))}),ok:mode!=='error',text:()=>mode==='body'?new Promise(()=>{}):Promise.resolve(mode==='bad'?'not JSON':JSON.stringify(mode==='error'?{error:'Manager access required'}:{value:42}))};
 }};
vm.runInNewContext(SCRIPT,context);
(async()=>{
 assert.equal((await context.window.tc.api('/ok',{method:'POST',body:'x=1',headers:{a:'b'}})).value,42);
 assert.equal(last.method,'POST');assert.equal(last.body,'x=1');assert.equal(last.headers.a,'b');assert.equal(last.cache,'no-store');assert.equal(active.size,0);
 for(const [state,message] of [['error','Manager access required'],['bad','The clock could not complete this'],['network','Network unavailable'],['headers','took too long'],['body','took too long']]){
  mode=state;calls=0;aborted=false;
  await assert.rejects(()=>context.window.tc.api('/test',{method:'POST'}),e=>e.message.includes(message));
  assert.equal(calls,1);assert.equal(active.size,0);
  if(state==='headers'||state==='body')assert.equal(aborted,true);
 }
 mode='ok';const controller=new AbortController();const cancelled=context.window.tc.request('/cancel',{signal:controller.signal});controller.abort();await assert.rejects(()=>cancelled);assert.equal(aborted,true);assert.equal(active.size,0);
 console.log('PASS: shared API preserves requests and server errors; headers/body time out; aborts and clears timers; no automatic save retries');
})().catch(e=>{console.error(e);process.exitCode=1});
'''.replace('SCRIPT',json.dumps(gen_ui_inc.UI_JS.removeprefix('<script>').removesuffix('</script>')))
with tempfile.TemporaryDirectory() as td:
    p=Path(td)/'test.cjs';p.write_text(script)
    subprocess.run(['node',str(p)],check=True)
