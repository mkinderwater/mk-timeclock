from pathlib import Path
import sqlite3

src = Path(__file__).resolve().parents[1] / "src" / "server.c"
s = src.read_text()
assert "CREATE TABLE IF NOT EXISTS clock_names" in s
assert "LEFT JOIN clock_names n ON n.clock_id=r.clock_id" in s
assert "json_quote(COALESCE(n.name,r.clock_id))" in s
assert '"/api/v1/admin/clock-name"' in s
assert "name=excluded.name" in s
assert "o.textContent=(x.name||x.clock_id)" in s
cx = sqlite3.connect(":memory:")
cx.executescript("CREATE TABLE registered_clocks(clock_id TEXT PRIMARY KEY,device_key TEXT NOT NULL,active INTEGER NOT NULL);CREATE TABLE clock_names(clock_id TEXT PRIMARY KEY REFERENCES registered_clocks(clock_id),name TEXT NOT NULL CHECK(length(name) BETWEEN 1 AND 60));INSERT INTO registered_clocks VALUES('CLOCK-001','test',1);")
cx.execute("INSERT INTO clock_names(clock_id,name) SELECT clock_id,? FROM registered_clocks WHERE clock_id=? AND active=1 ON CONFLICT(clock_id) DO UPDATE SET name=excluded.name", ('Reception','CLOCK-001'))
assert cx.execute("SELECT name FROM clock_names").fetchone()[0] == 'Reception'
assert cx.execute("SELECT device_key FROM registered_clocks").fetchone()[0] == 'test'
print('clock friendly names: PASS')
