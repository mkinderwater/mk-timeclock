"""Card replacement is a manager-only, atomic history-preserving operation."""
from pathlib import Path
s = (Path(__file__).resolve().parents[1]/'src/server.c').read_text()
a = (Path(__file__).resolve().parents[1]/'src/admin_ops.inc').read_text()
assert 'CREATE TABLE IF NOT EXISTS card_replacement_audit' in s
assert '/api/v1/admin/card-replace' in s
assert 'card_replace(fd,session_user(buf),b+4)' in s
assert 'BEGIN IMMEDIATE' in a[a.index('static void card_replace'):a.index('static void card_list_json')]
assert 'UPDATE cards SET active=0' in a
assert 'INSERT INTO cards(uid,employee_id,active)' in a
assert 'INSERT INTO card_replacement_audit' in a
assert 'SELECT 1 FROM manager_cards' in a
assert 'SELECT 1 FROM cards' in a
assert "r.textContent='Replace'" in a and 'Scan new card (30 seconds)' in a
print('card replacement source checks passed')
