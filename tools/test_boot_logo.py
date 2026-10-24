from pathlib import Path
import re
root=Path(__file__).resolve().parents[1]
src=(root/'src/boot_logo.h').read_text()
assert len(re.findall(r'0x[0-9a-fA-F]{2}',src))==1024
ui=(root/'src/display_ui.c').read_text()
a=(root/'src/agent.c').read_text()
assert 'void display_boot_logo' in ui
assert 'display_boot_logo(d);' in ui
assert 'j<20' in a
assert 'display_idle_status(&disp,0,0)' in a
