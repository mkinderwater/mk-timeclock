"""Decode actual mailer-generated MIME, including Unicode and XLSX attachments."""
from email import policy
from email.parser import BytesParser
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def test_plain_and_attachment_mime():
    with tempfile.TemporaryDirectory() as temp:
        exe = Path(temp) / 'mime-harness'
        subprocess.run(['cc', '-O2', '-Wall', '-Wextra', '-Werror', '-std=c11',
                        '-D_POSIX_C_SOURCE=200809L', '-o', str(exe),
                        str(ROOT / 'tests/mime_harness.c'), '-lsqlite3', '-lcurl'],
                       check=True)
        for attachment in (False, True):
            raw = subprocess.check_output([str(exe)] + (['attached'] if attachment else []))
            msg = BytesParser(policy=policy.default).parsebytes(raw)
            assert not msg.defects
            assert str(msg['Subject']) == 'Attendance: résumé'
            assert msg.is_multipart() == attachment
            if attachment:
                parts = list(msg.iter_parts())
                assert len(parts) == 2
                assert parts[0].get_content_type() == 'text/plain'
                assert parts[1].get_content_type() == 'application/vnd.openxmlformats-officedocument.spreadsheetml.sheet'
                assert parts[1].get_filename() == 'timesheet.xlsx'
                assert parts[1].get_payload(decode=True) == bytes([0x50,0x4b,0x03,0x04,0x00,0xff,0x01])
                body = parts[0].get_content()
            else:
                assert msg['Content-Transfer-Encoding'] == 'base64'
                body = msg.get_content()
            assert 'Missed punches' in body
            assert 'Résumé: vérifié' in body
            assert 'Content-Transfer-Encoding:' not in body
            assert b'\r\n\r\nContent-Transfer-Encoding:' not in raw


def test_legacy_queue_attachment_null_safe():
    src = (ROOT / 'src/mailer.c').read_text()
    assert 'attachment?strdup((const char*)attachment):NULL' in src
    assert 'csv=strdup((const char*)sqlite3_column_text(q,5))' not in src
