from pathlib import Path
s=(Path(__file__).resolve().parents[1]/"src/rc522.c").read_text()
a=(Path(__file__).resolve().parents[1]/"src/agent.c").read_text()
for token in ("verify_reader(fd)","TRELOADH","TRELOADL","TPRESCALER","TXASK","RC522_MAX_RECOVERY_FAILURES","return -2;","recover_reader(fd)"):
 assert token in s,token
assert 'if(read==-2)' in a
print("RC522 reliability source checks pass")
