#!/usr/bin/env python3
"""Source regression checks; on-device RC522 tests are still required."""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
a=(r/'src/agent.c').read_text(); install=(r/'install.sh').read_text(); ops=(r/'src/admin_ops.inc').read_text(); schedule=(r/'src/schedule.inc').read_text(); payroll=(r/'src/payroll.inc').read_text()
assert a.index('int fresh=first_presentation(uid,scan_now);') < a.index('if(scan_armed(now,uid))')
assert 'mark_enrolled(uid);' in a
assert 'if(consumed_by_enrollment(uid))continue;' in a
assert 'if(fresh){' in a
assert 'now-last_preview>=15' in a and 'preview_urgent' in a
assert 'if(!sql("RELEASE reject_event;")){sent=-1;break;}' in a
assert "value=''" in ops
# v0.9.48: notes are written by employees through src/note_link.inc; multiline input still required.
assert 'form_value_multiline(body+4,"note",note,sizeof note)' in (r/'src/note_link.inc').read_text()
assert 'if [[ $apt_updated -eq 0 ]]; then apt-get update' in install
assert 'SAVEPOINT validate_schedule' in schedule and 'ROLLBACK TO validate_schedule' in schedule
assert 'SAVEPOINT validate_payroll' in payroll and 'ROLLBACK TO validate_payroll' in payroll
print('PASS enrollment safety, export input, preview throttle, package install and pre-approval validation source checks')
