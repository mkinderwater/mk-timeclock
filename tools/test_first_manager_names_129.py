from pathlib import Path
s=(Path(__file__).parent.parent/'src/server.c').read_text()
assert "name='first_name'" in s and "name='last_name'" in s
assert 'form_value(body,"first_name"' in s and 'form_value(body,"last_name"' in s
assert 'INSERT INTO employees(name,email,position,active,first_name,last_name)' in s
assert 'sqlite3_bind_text(q,4,name,-1,SQLITE_TRANSIENT)' in s
assert 'sqlite3_bind_text(q,5,sep,-1,SQLITE_TRANSIENT)' in s
print('PASS first manager names')
