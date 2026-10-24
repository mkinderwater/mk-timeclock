from pathlib import Path
s=(Path(__file__).resolve().parents[1]/'src/server.c').read_text()
assert 'welcome_enqueue_ex(long employee_id,int manual)' in s
assert 'AND (?2=1 OR (welcomed_at IS NULL' in s
assert 'EXISTS(SELECT 1 FROM cards WHERE employee_id=?1 AND active=1)' not in s[s.index('static int welcome_enqueue_ex'):s.index('static int welcome_enqueue(long employee_id){return')]
assert 'welcome:%ld:resend:%s' in s
assert 'if(ok&&!manual)' in s
assert 'employees/welcome/resend' in s
assert "id='resendWelcome'" in s
assert "'Resent welcome email'" in s
print('PASS: welcome eligibility and resend contract')
