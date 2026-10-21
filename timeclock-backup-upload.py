#!/usr/bin/env python3
"""Privileged validation of an uploaded archive. Upload never restores data."""
import grp
import hashlib
import json
import os
from pathlib import Path
import re
import sqlite3
import tarfile
import tempfile

INBOX = Path('/var/lib/timeclock/backup-inbox')
BACKUPS = Path('/var/backups/timeclock')
MAX_COMPRESSED = 64 * 1024 * 1024
MAX_EXPANDED = 512 * 1024 * 1024
VALID = re.compile(r'timeclock-[A-Za-z0-9.-]+\.tar\.gz\Z')


def result(state, message, name=''):
    tmp = INBOX / '.status.tmp'
    tmp.write_text(json.dumps({'state': state, 'message': str(message)[:250], 'archive': name}))
    os.chown(tmp, 0, grp.getgrnam('timeclock').gr_gid)
    os.chmod(tmp, 0o640)
    os.replace(tmp, INBOX / 'status.json')


def validate(src):
    if src.is_symlink() or not src.is_file() or not 0 < src.stat().st_size <= MAX_COMPRESSED:
        raise ValueError('Invalid archive size or type')
    with tarfile.open(src, 'r:gz') as tf:
        names = set()
        expanded = 0
        database = None
        for member in tf:
            name = member.name
            if name in names or len(names) >= 100000:
                raise ValueError('Duplicate entries or too many files')
            names.add(name)
            components = Path(name).parts
            if not components or components[0] != 'data' or any(x in ('.', '..') for x in components):
                raise ValueError('Unsafe archive member')
            if not (member.isfile() or member.isdir()):
                raise ValueError('Links or special files are not allowed')
            expanded += max(0, member.size)
            if expanded > MAX_EXPANDED:
                raise ValueError('Expanded backup exceeds 512 MiB')
            if name == 'data/server.db' and member.isfile():
                database = member
        if 'data' not in names or database is None:
            raise ValueError('Not a complete Master clock backup')
        with tempfile.TemporaryDirectory(prefix='timeclock-verify-') as td:
            path = Path(td) / 'server.db'
            with tf.extractfile(database) as stream, path.open('wb') as out:
                while True:
                    block = stream.read(65536)
                    if not block:
                        break
                    out.write(block)
            con = sqlite3.connect(f'file:{path}?mode=ro', uri=True)
            try:
                if con.execute('PRAGMA integrity_check').fetchone()[0] != 'ok':
                    raise ValueError('Backup database failed integrity check')
                tables = {x[0] for x in con.execute("SELECT name FROM sqlite_master WHERE type='table'")}
                if not {'employees', 'swipes', 'registered_clocks'} <= tables:
                    raise ValueError('Backup database has missing tables')
            finally:
                con.close()


def main():
    namefile = INBOX / '.name'
    src = INBOX / '.ready'
    try:
        name = namefile.read_text().strip()
        if not VALID.fullmatch(name) or '/' in name or '\\' in name:
            raise ValueError('Invalid archive name')
        dest = BACKUPS / name
        if dest.exists() or dest.is_symlink() or Path(str(dest) + '.sha256').exists():
            raise ValueError('Archive name already exists')
        result('validating', 'Checking archive')
        validate(src)
        h = hashlib.sha256()
        with src.open('rb') as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b''):
                h.update(block)
        # Never overwrite an existing archive, even after a race.
        os.link(src, dest, follow_symlinks=False)
        try:
            os.chown(dest, 0, grp.getgrnam('timeclock').gr_gid)
            os.chmod(dest, 0o640)
            side = Path(str(dest) + '.sha256')
            with side.open('x') as out:
                out.write(h.hexdigest() + '  ' + name + '\n')
            os.chown(side, 0, grp.getgrnam('timeclock').gr_gid)
            os.chmod(side, 0o640)
        except Exception:
            dest.unlink(missing_ok=True)
            raise
        result('success', 'Archive verified and available for restoration', name)
    except Exception as exc:
        result('failed', str(exc))
    finally:
        src.unlink(missing_ok=True)
        namefile.unlink(missing_ok=True)


if __name__ == '__main__':
    main()
