#!/usr/bin/env python3
"""Exercise the actual shared UI helper in Node, not just a string match."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'src/ui.inc').read_text()
code=s.split('#define UI_JS "<script>',1)[1].split('</script>"',1)[0]
js="global.window={};global.MutationObserver=class{observe(){}};global.document={documentElement:{}};"+code+''';
if(typeof window.tc.datetime!=='function')throw Error('datetime helper missing');
let d=window.tc.datetime('2026-10-16T13:58:00Z');
if(!d.includes('2026') || !/\\b(AM|PM)\\b/.test(d) || /:\\d{2}:\\d{2}/.test(d))throw Error('Wrong format: '+d);
if(window.tc.datetime('invalid-date')!=='invalid-date')throw Error('Invalid date fallback');
console.log('PASS:',d);
'''
with tempfile.TemporaryDirectory() as tmp:
 p=Path(tmp)/'helper.js';p.write_text(js)
 subprocess.run(['node',str(p)],check=True)
