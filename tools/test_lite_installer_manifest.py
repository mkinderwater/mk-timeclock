#!/usr/bin/env python3
"""All literal source dependencies installed by install.sh must exist."""
from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
s=(root/'install.sh').read_text()
for name in re.findall(r'\$HERE/([\w.-]+)',s):
    if name not in ('timeclock-server','timeclock-agent','timeclock-mailer'):
        assert (root/name).is_file(),f'installer source missing: {name}'
assert (root/'timeclock-backup').is_file() and (root/'timeclock-backup').stat().st_mode&0o111
assert s.index('Required installation source missing')<s.index('"$HERE/pre-apt.sh"')
print('PASS: install source dependency manifest complete; preflight before package/install changes')
