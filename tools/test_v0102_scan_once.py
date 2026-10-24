#!/usr/bin/env python3
from pathlib import Path
s=(Path(__file__).resolve().parents[1]/'src/agent.c').read_text()
assert 'scans[i].state==1 || scans[i].state==2' in s
assert 'scans[i].state && seconds_since' not in s
assert 'scans[i].state==2?4:0' in s
assert 'else if(scans[slot].state==4)scans[slot].state=0' in s
assert '"%.23s",scans[i].uid' in s
assert 'display_message(&disp,"CARD READ","VERIFYING...")' in s
print('PASS scan state ownership and release assertions')
