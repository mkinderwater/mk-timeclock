from pathlib import Path
root=Path(__file__).resolve().parents[1]
a=(root/'src/agent.c').read_text()
r=(root/'src/rc522.c').read_text()
assert 'uint8_t halt[4]={0x50,0x00,0,0}' in r
assert 'crc_a(halt,2,halt+2)' in r
assert 'while(!quitting && sqlite3_step(s)==SQLITE_ROW)' in a
assert a.count('while(!quitting && sqlite3_step(q)==SQLITE_ROW)')>=2
assert 'enqueue_scan(uid,scan_now)' in a
assert 'remote_scan(args->base,args->key,uid,&manager)' in a
assert 'read_failure=1;quitting=1;break;' in a
assert 'gpio_set(buzzer,0);buzzer_state.active=0;' in a
print('v0.9.101 source regression checks PASS')
