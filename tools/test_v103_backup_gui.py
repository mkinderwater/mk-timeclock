from pathlib import Path
root=Path(__file__).resolve().parents[1]
server=(root/'src/server.c').read_text()
backup=(root/'timeclock-backup').read_text()
service=(root/'timeclock-backup.service').read_text()
assert 'backup_download_file(fd,session_user(buf),path+37)' in server
assert 'backup_download_arm(fd,session_user(buf),b+4)' in server
assert 'owner!=who||at<=0||(long long)time(NULL)-at>60' in server
assert 'DELETE FROM backup_download WHERE id=1 AND token=?1 AND requested_by=?2' in server
assert 'mc.manager_id=?2' in server
assert 'backup_download_audit' in server
assert 'O_NOFOLLOW' in server
assert 'system/arm' in server and "<a href='/system?backup=1'>Back up now</a>" in server
assert "ap.add_argument('--email-scheduled'" in backup
assert 'email_scheduled(target, data / "server.db")' in backup
assert "if not row or not row[0]:" in backup
assert "tls not in ('none','starttls','implicit')" in backup
assert '15 * 1024 * 1024' in backup
assert 'backup --email-scheduled' in service
print('PASS v0.9.103 backup GUI security invariants')
