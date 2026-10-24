from pathlib import Path
r=Path(__file__).resolve().parent.parent
setup=(r/'clock-setup.py').read_text()
install=(r/'install.sh').read_text()
assert 'SETUP_KEY' not in setup
assert 'setup_key' not in setup
assert 'ROLE_SELECTED.exists()' in setup
assert 'ROLE_SELECTED = Path' in setup
assert 'rm -f /etc/timeclock/role-setup-key' in install
assert 'role-selected' in install
assert 'Adoption code' in setup
assert 'apply-role.sh' in setup
print('v0.9.82 role-key cull checks passed')
