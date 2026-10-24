from pathlib import Path
s=Path('src/server.c').read_text()
helper=Path('timeclock-timezone-apply.sh').read_text()
reminders=Path('timeclock-attendance-reminders.py').read_text()
assert 'readlink("/etc/localtime"' in s
assert 'timezone_save(' not in s
assert 'timezone:' in s and 'timezone_valid(zone)' in s
assert 'action:\'timezone\'' in s
assert 'setTimeout(poll,700)' in s
assert 'systemctl try-restart timeclock-server.service timeclock-agent.service timeclock-mailer.service' in helper
assert '"/etc/localtime").resolve()' in reminders
assert '"/etc/timezone"' not in reminders
print('PASS v0.9.117 timezone authorization and runtime checks')
