from pathlib import Path
s=(Path(__file__).resolve().parents[1]/'src/server.c').read_text()
a=s[s.index('static void system_page(int fd)'):s.index('static void system_arm(',s.index('static void system_page(int fd)'))]
b=s[s.index('static void backups_page(int fd)'):s.index('static void system_page(',s.index('static void backups_page(int fd)'))]
assert "timer=setTimeout(poll,700)" in a
assert "pollActive=false;arm.disabled=false;state.textContent=j.status" in a
assert "setInterval(async()=>" not in a
assert "waitForCard(j.token" in b
assert "setTimeout(poll,700)" in b
assert "setInterval(async()=>" not in b
assert s.startswith('#define _GNU_SOURCE')
assert '#define TIMECLOCK_VERSION "0.9.114"' in (Path(__file__).resolve().parents[1]/'src/agent.c').read_text()
assert 'Restore** remains CLI-only' not in (Path(__file__).resolve().parents[1]/'docs/BACKUP_RESTORE.md').read_text()
print('PASS v0.9.107 sequential backup/restore authorization polling')
