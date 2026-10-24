from pathlib import Path
s=(Path(__file__).resolve().parents[1]/'src/server.c').read_text()
for x in ('Clock management','Generate adoption code','Refresh clocks','Revoke Slave','AbortController','document.hidden','Restore is disabled.','timeclock-backup verify ARCHIVE'):
    assert x in s, x
assert 'role-setup-key' not in s
print('v0.9.88 GUI management checks passed')
