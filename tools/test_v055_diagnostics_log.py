from pathlib import Path
p=Path(__file__).resolve().parents[1]
s=(p/'src/server.c').read_text()
css=(p/'src/ui.css').read_text()
svc=(p/'timeclock-server.service').read_text()
assert 'GET")&&!strcmp(path,"/api/v1/admin/diagnostics/log")' in s
assert 'static void diagnostics_log(int fd)' in s
assert '--lines=120' in s and 'timeout 1s' in s
assert 'timeclock-agent.service' in s and 'timeclock-mailer.service' in s
assert "id='diagLog'" in s and "id='logAuto'" in s
assert 'setInterval(()=>{if(document.getElementById(\'logAuto\').checked' in s
assert '.diag-log{' in css
assert 'SupplementaryGroups=systemd-journal' in svc
print('v0.9.55 log checks passed')
