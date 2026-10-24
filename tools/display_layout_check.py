from pathlib import Path
root=Path(__file__).resolve().parent.parent
s=(root/'src/display_ui.c').read_text()
t=(root/'src/oled_ttf.c').read_text()
o=(root/'src/oled.c').read_text()
b=(root/'src/oled_bitmap.c').read_text()
a=(root/'src/agent.c').read_text()
# idle: blinking colon in stable slots, seconds bar, burn-in shift actually applied
assert 'tm.tm_sec%2' in s
assert 'i!=2 || colon_visible' in t
assert 'const int left=2,right=125;' in s
assert 'shift_frame(d,d->shift);' in s
# monochrome hinting and cached clock fit
assert 'FT_LOAD_MONOCHROME' in t and 'FT_PIXEL_MODE_MONO' in t
assert 'if(t->clk_max==max_size&&t->clk_size)return 0;' in t
# partial-column flush
assert 'while(first<128&&cur[first]==old[first])first++;' in o
# punctuation needed by IP/version lines
assert '{ 46, {' in b and '{ 47, {' in b
# explicit result kinds, no substring classification
assert 'strstr(detail' not in s
for k in ('DISPLAY_IN','DISPLAY_OUT','DISPLAY_LATE','DISPLAY_PENDING','DISPLAY_DUPLICATE','DISPLAY_REVIEW','DISPLAY_REJECT','DISPLAY_MANAGER','DISPLAY_ENROLL','DISPLAY_TEST'):
    assert 'display_result(&disp,'+k in a, k
assert 'display_tick(&disp);' in a and '!display_busy(&disp)' in a
assert 'message_until' not in a
print('PASS idle layout / burn-in shift / mono glyphs / partial flush / explicit result kinds')
