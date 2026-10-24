"""Execute the actual holiday page save handler for successful and rejected saves."""
import ast
import json
import re
import subprocess
from pathlib import Path

def test_holiday_save_handler():
    root = Path(__file__).resolve().parents[1]
    source = (root / 'src/server.c').read_text()
    start = source.index('"<script>const form=document.getElementById(\'f\')')
    end = source.index('UI_FOOT', start)
    html = ''.join(ast.literal_eval(m.group()) for m in re.finditer(r'"(?:[^"\\]|\\.)*"', source[start:end]))
    script = html.split('<script>', 1)[1].split('</script>', 1)[0]
    handler = script[script.index('form.onsubmit='):script.index(';load();')]
    js = r'''
const assert=require('node:assert/strict');
(async()=>{
 const form={},message={},skip={checked:true};
 const rows={children:[{dataset:{},querySelectorAll:()=>[{value:'2026-12-25'},{value:'Christmas Day'}]}]};
 let reject=false, calls=[];
 const tc={api:async(url,options)=>{calls.push([url,options]);if(reject)throw Error('Save rejected');return {status:'saved'}},ok:(el,text)=>el.success=text,err:(el,e)=>el.error=String(e.message||e)};
 HANDLER;
 await form.onsubmit({preventDefault(){}});
 assert.equal(calls.length,1);
 assert.equal(calls[0][0],'/api/v1/admin/holidays');
 assert.equal(calls[0][1].body.get('calendar'),'2026-12-25,Christmas Day');
 assert.equal(calls[0][1].body.get('skip'),'1');
 assert.equal(message.success,'Holidays saved.');assert.equal(message.error,undefined);
 reject=true;delete message.success;
 await form.onsubmit({preventDefault(){}});
 assert.equal(message.error,'Save rejected');assert.equal(message.success,undefined);
 console.log('PASS holiday save success and server rejection');
})().catch(e=>{console.error(e);process.exit(1)});
'''.replace('HANDLER', handler)
    subprocess.run(['node', '-e', js], check=True)

if __name__ == '__main__':
    test_holiday_save_handler()
