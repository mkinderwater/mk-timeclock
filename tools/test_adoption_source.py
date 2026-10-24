from pathlib import Path
s=(Path(__file__).parent.parent/'src/server.c').read_text()
a=(Path(__file__).parent.parent/'clock-setup.py').read_text()
assert 'CREATE TABLE IF NOT EXISTS adoption_codes' in s
assert 'CREATE TABLE IF NOT EXISTS adoption_requests' in s
assert 'adoption_approve(fd,b)' in s
assert 'adoption_poll(fd,b+4)' in s
assert "('/request', '/check', '/role')" in a
assert "ssl.create_default_context()" in a
print('Adoption wiring checks passed')
