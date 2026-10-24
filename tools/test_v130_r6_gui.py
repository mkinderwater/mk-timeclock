from pathlib import Path
s=Path('src/server.c').read_text()
assert "id='addEmployee'" in s
assert "<dialog id='employeeDialog'" in s
assert "document.getElementById('employeeDialog').showModal()" in s
assert "id='new'" not in s[s.index('static void management_page'):s.index('static void employee_card_revoke')]
assert "<h2>Live OLED preview</h2>" not in s
part=s[s.index('static void recent_page'):s.index('static void manager_cards_get')]
assert part.index("class='today-preview'") < part.index('Recent swipes')
assert 'refreshPreview' in part
print('PASS employee dialog and Today OLED placement')
