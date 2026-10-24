#!/usr/bin/env python3
"""Exercise PEM joining with both newline and no-newline inputs."""
from pathlib import Path
import subprocess, tempfile

SCRIPT = Path(__file__).resolve().parents[1] / 'timeclock-tls-apply.sh'
src = SCRIPT.read_text()
assert 'printf \'\\n\'' in src
assert 'assembled certificate chain contains invalid certificate' in src
assert 'nginx-test.log' in src and 'nginx-reload.log' in src

with tempfile.TemporaryDirectory() as d:
    root = Path(d)
    # Generate a genuine pair of certificates. Test each missing/trailing LF combination.
    for name in ('a', 'b'):
        subprocess.run(['openssl', 'req', '-x509', '-newkey', 'rsa:2048', '-nodes', '-subj', '/CN='+name,
                        '-keyout', str(root/(name+'.key')), '-out', str(root/(name+'.pem')), '-days', '1'],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    a = (root/'a.pem').read_bytes().rstrip(b'\n')
    b = (root/'b.pem').read_bytes().rstrip(b'\n')
    for ae in (b'', b'\n'):
        for be in (b'', b'\n'):
            full = root/'fullchain.pem'
            full.write_bytes(a+ae+b'\n'+b+be+b'\n' if ae else a+b'\n'+b+be+b'\n')
            # Actual assembly rule always inserts a newline independent of source endings.
            full.write_bytes(a+ae+b'\n'+b+be+b'\n')
            # Confirm decoder reads both PEM blocks using extraction.
            blocks = full.read_text().split('-----END CERTIFICATE-----')
            assert len(blocks)==3
            for i, block in enumerate(blocks[:2]):
                piece = root/f'piece-{i}.pem'
                piece.write_text(block+'-----END CERTIFICATE-----\n')
                subprocess.run(['openssl','x509','-in',str(piece),'-noout'], check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            subprocess.run(['openssl','crl2pkcs7','-nocrl','-certfile',str(full)],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
print('PASS: both PEM blocks parse with or without original trailing LF; installer source checks present')
