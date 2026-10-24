from pathlib import Path
import re
ROOT = Path(__file__).resolve().parents[1]
inst=(ROOT/'install.sh').read_text()
patterns=(ROOT/'src/buzzer_patterns.h').read_text()
assert 'BUZZER_OFFSET=6\nATTENDANCE_ENABLED=yes\n' in inst
assert "printf '\\nATTENDANCE_ENABLED=no\\n'" in inst
assert 'Preserved /etc/timeclock/timeclock.env' in inst
assert 'physical pin 9 is GND' in inst
expected={
 'ok':[55,55,85], 'out':[130,55,55,55,130],
 'late':[65,50,65,135,160], 'fail':[115,75,115,75,115],
 'manager':[65,45,65,45,65,45,170]
}
for name,durations in expected.items():
 m=re.search(rf'buzz_{name}_durations\[\]\s*=\s*\{{([^}}]+)\}}',patterns)
 assert m, name
 actual=[int(x.strip()) for x in m.group(1).split(',')]
 assert actual==durations,(name,actual)
 assert len(actual)%2==1 and all(20<=n<=200 for n in actual)
assert len({tuple(x) for x in expected.values()})==5
agent=(ROOT/'src/agent.c').read_text()
assert 'else if(result==5)' in agent or 'result==5' in agent
print('v0.9.63 attendance default and buzzer patterns: PASS')
