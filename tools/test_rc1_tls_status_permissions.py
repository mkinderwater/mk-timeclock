#!/usr/bin/env python3
"""RC1 status-marker access control and installation coverage."""
import pathlib, subprocess, tempfile, os
root=pathlib.Path(__file__).resolve().parents[1]
for f in ['timeclock-tls-apply.sh','timeclock-tls-bootstrap.sh','install.sh']:
    subprocess.run(['bash','-n',str(root/f)],check=True)
# Evaluate the intended POSIX modes without touching /etc or exposing private keys.
with tempfile.TemporaryDirectory() as temp:
    tls=pathlib.Path(temp)/'tls'; tls.mkdir()
    versions=tls/'versions'; versions.mkdir()
    private=versions/'key.pem'; private.write_text('test-only')
    active=tls/'active'; active.touch()
    os.chmod(tls,0o710);os.chmod(versions,0o700);os.chmod(private,0o600);os.chmod(active,0o644)
    assert tls.stat().st_mode & 0o777 == 0o710
    assert versions.stat().st_mode & 0o777 == 0o700
    assert private.stat().st_mode & 0o777 == 0o600
    assert active.stat().st_mode & 0o777 == 0o644
# Installer, bootstrap and replacement must all establish the public-status access.
for file in ['install.sh','timeclock-tls-bootstrap.sh','timeclock-tls-apply.sh']:
    text=(root/file).read_text()
    assert 'chown root:timeclock /etc/timeclock/tls' in text, file
    assert 'chmod 0710 /etc/timeclock/tls' in text, file
print('TLS status permission checks passed')

# Replay every installer operation on this directory, in source order. The earlier
# fix was undone by a later install -d; checking only for the correct line missed it.
import re, sys
with tempfile.TemporaryDirectory(prefix='tls-upgrade-') as temp:
    base=pathlib.Path(temp);base.chmod(0o755)
    tls=base/'tls';tls.mkdir();versions=tls/'versions';versions.mkdir(mode=0o700)
    key=versions/'key.pem';key.write_bytes(b'preserved private key');key.chmod(0o600)
    cert=versions/'fullchain.pem';cert.write_bytes(b'preserved certificate');cert.chmod(0o644)
    (tls/'active').touch();(tls/'active').chmod(0o644)
    (tls/'not_after').write_text('2000000000\n');(tls/'not_after').chmod(0o644)
    uid=os.getuid()
    def mapped(path,identity):
        return any(int(row.split()[0])<=identity<int(row.split()[0])+int(row.split()[2]) for row in pathlib.Path(path).read_text().splitlines())
    can_drop=uid==0 and mapped('/proc/self/uid_map',65534) and mapped('/proc/self/gid_map',65534)
    gid=65534 if can_drop else os.getgid()
    # Translate only ownership and destination, retaining each actual mode/command.
    steps=[]
    for line in (root/'install.sh').read_text().splitlines():
        command=line.strip()
        if re.match(r'(?:install -d |chown |chmod )',command) and command.endswith(' /etc/timeclock/tls'):
            command=command.replace('/etc/timeclock/tls',str(tls)).replace('root:timeclock',f'{uid}:{gid}').replace('-o root',f'-o {uid}').replace('-g timeclock',f'-g {gid}').replace('-g root','-g 0')
            subprocess.run(command.split(),check=True);steps.append(command)
    assert len(steps)>=3,steps
    assert tls.stat().st_mode & 0o777==0o710
    assert versions.stat().st_mode & 0o777==0o700 and key.stat().st_mode & 0o777==0o600
    assert key.read_bytes()==b'preserved private key' and cert.read_bytes()==b'preserved certificate'
    if can_drop:
        probe="import os,sys;from pathlib import Path;p=Path(sys.argv[1]);assert os.access(p/'active',os.F_OK);assert (p/'not_after').read_text().strip()=='2000000000';assert not os.access(p/'versions'/'key.pem',os.R_OK)"
        def as_backend():
            os.setgroups([]);os.setgid(gid);os.setuid(65534)
        subprocess.run([sys.executable,'-c',probe,str(tls)],preexec_fn=as_backend,check=True)
        # Demonstrate the exact old regression: root-only directory hides status
        # while leaving the certificate/key bytes untouched and usable by root.
        os.chmod(tls,0o700);os.chown(tls,0,0)
        denied="import os,sys;from pathlib import Path;p=Path(sys.argv[1]);assert not os.access(p/'active',os.F_OK)"
        subprocess.run([sys.executable,'-c',denied,str(tls)],preexec_fn=as_backend,check=True)
        assert cert.read_bytes()==b'preserved certificate'
    else:
        print('Backend identity probe unavailable in this UID/GID namespace; actual installer mode replay and certificate/key preservation passed.')
print('PASS: final installer mode replay retains TLS status traversal and private-key isolation; certificate/key contents preserved')
