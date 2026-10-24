from pathlib import Path
a=(Path(__file__).resolve().parents[1]/'src/attendance.inc').read_text()
s=(Path(__file__).resolve().parents[1]/'src/server.c').read_text()
g=(Path(__file__).resolve().parents[1]/'src/agent.c').read_text()
assert 'difftime(at,old)<duplicate_seconds' in a
assert 'difftime(at,previous)<duplicate_seconds' in a
assert 'duplicate_seconds=10' in a
assert 'clock_duplicate_settings' in s
assert '"DUPLICATE"' in s
assert 'feedback=5' in g
assert 'else if(result==5)' in g
print('duplicate settings and feedback checks pass')
