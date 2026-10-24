from pathlib import Path
s=Path("src/server.c").read_text()
assert "id='logAuto'> Auto-refresh" in s
assert "Log not loaded. Select Refresh log." in s
assert "timeout 1s /usr/bin/journalctl" in s
assert "long long deadline=now_ms()+REQUEST_DEADLINE_MS;" in s
assert "refreshLog();setInterval" not in s
