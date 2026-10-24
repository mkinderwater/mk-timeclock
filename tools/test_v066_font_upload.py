from pathlib import Path
s=Path("src/server.c").read_text()
a=Path("src/agent.c").read_text()
assert "static void font_upload(" not in s
assert 'font_upload(fd,buf,(size_t)n)' not in s
assert "id='fontUpload'" not in s
assert 'method=\'post\' action=\'/api/v1/admin/display-font\'' in s
assert 'custom-' not in a or 'strncmp(name,"custom-",7)!=0' in a
print("Stock-only TTF controls verified")
