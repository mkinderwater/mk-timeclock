from pathlib import Path
s=(Path(__file__).resolve().parents[1]/'src/server.c').read_text()
for name,stop in [('accept_swipe','accept_unknown'),('accept_unknown','/* Whole-request')]:
    part=s[s.index('static void '+name+'('):s.index('static void '+stop+'(') if stop!='/* Whole-request' else s.index('/* Whole-request')]
    assert 'clock_durability(1)' in part
    assert 'clock_durability(0)' in part
    assert part.index('clock_durability(1)')<part.index('sqlite3_prepare_v2')
part=s[s.index('static void rejected_upload_receive('):s.index('static void rejected_upload_list(')]
assert 'clock_durability(1)' in part and 'clock_durability(0)' in part
assert s.index('static int clock_durability') < s.index('static void accept_swipe')
print('PASS durable clock acknowledgement boundaries')
