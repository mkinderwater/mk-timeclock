#!/usr/bin/env python3
"""RC1 packaging preflight: no installation or file changes."""
from pathlib import Path
import re, sys, subprocess
p=Path(__file__).resolve().parent
s=(p/'install.sh').read_text()
refs=set(re.findall(r'\$HERE/([A-Za-z0-9_.-]+)',s))
refs |= set(re.findall(r'\$HERE/([A-Za-z0-9_.-]+)',(p/'install-master.sh').read_text()))
refs |= set(re.findall(r'\$HERE/([A-Za-z0-9_.-]+)',(p/'install-slave.sh').read_text()))
produced={'timeclock-server','timeclock-agent','timeclock-mailer'}
missing=sorted(x for x in refs-produced if not (p/x).is_file())
for name in ('timeclock-backup','install-master.sh','install-slave.sh','Makefile','src/agent.c','src/server.c','src/mailer.c'):
    if not (p/name).is_file(): missing.append(name)
if missing:
    print('FAILED: missing installer inputs:',', '.join(sorted(set(missing))));sys.exit(1)
print('PASS: source and referenced installer files present')
print('NOTE: BPI agent must be compiled on device with FreeType, libcurl and I2C dependencies')
print('NOTE: preflight does not test hardware, TLS, SMTP or systemd')
