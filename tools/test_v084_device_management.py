from pathlib import Path
r = Path(__file__).resolve().parents[1]
s=(r/'src/server.c').read_text()
nav=(r/'tools/gen_ui_inc.py').read_text()
helper=(r/'timeclock-system-action.sh').read_text()
assert '"/device-health")){health_page(fd);return;}' in s
assert '"/health")){send_response(fd,200,"application/json"' in s
assert '"/backups")){backups_page(fd);return;}' in s
assert "('health', '/device-health'" in nav
assert "('backups', '/backups'" in nav
assert "strcmp(action,\"backup\")" in s
assert 'backup) /opt/icubedev-timeclock/bin/timeclock-backup backup' in helper
assert 'role-setup-key' not in s
assert 'Restore is disabled.' in s
print('v0.9.84 device-management source checks passed')
