from pathlib import Path
root=Path(__file__).resolve().parents[1]
s=(root/'src/server.c').read_text()
nav=(root/'tools/gen_ui_inc.py').read_text()
assert "'/diagnostics'" in nav
assert '"/diagnostics"' in s and '"/api/v1/admin/diagnostics/report"' in s
assert '!strcmp(path,"/diagnostics")||' in s.split('&&!session_user(buf)')[0]
for key in ('raw_swipes','classified_swipes','queued_email','failed_email','rejected_uploads','uptime_seconds','storage_total_bytes'):
    assert key in s
assert 'diag_escape' in s and 'diagnostics_json(fd)' in s
print('PASS manager-only diagnostics routing, report fields and navigation')
