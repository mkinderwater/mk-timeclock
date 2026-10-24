from pathlib import Path
s=(Path(__file__).resolve().parents[1]/'src/server.c').read_text()
active=['swipe_employee','late_arrival_employee','late_arrival_manager','attendance_report','missing_in_employee','missing_out_employee']
assert all(('{"'+x+'"') in s for x in active)
assert '"implemented"' in s or '\\"implemented\\"' in s
assert "CASE WHEN ?1 IN ('late_arrival_employee','late_arrival_manager','attendance_report') THEN 1 ELSE 0 END" in s
assert 'UPDATE email_templates SET subject=?1,body=?2 WHERE id=?3' in s
print('PASS active email catalog, new reminders default-off, restore preserves enabled')
