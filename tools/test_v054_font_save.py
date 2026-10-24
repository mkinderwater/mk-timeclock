from pathlib import Path
s=Path(__file__).resolve().parents[1].joinpath("src/server.c").read_text()
a=s.index("static void display_font_page(")
b=s.index("static void display_font_save(",a)
f=s[a:b]
assert "char html[18000]" in f
assert "n>=sizeof html" in f
assert "addEventListener('submit'" in f
assert "e.preventDefault()" in f
assert "id='fontUpload'" not in f
assert "Font saved." in f
assert "save.disabled=false" in f
assert "id='activeFont'" in f
assert "static void font_upload(" not in s
assert "FONT_MAX" in s
print("v0.9.66 font UI/upload source checks passed")
