#!/usr/bin/env python3
"""Prevent CSS regeneration and Overview formatting regressions."""
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
css=(ROOT/'src/ui.css').read_text()
assert '.periods-table td' in css and '.clocks-table td' in css
assert 'white-space:nowrap' in css
subprocess.run(['make','ui'],cwd=ROOT,check=True,stdout=subprocess.DEVNULL)
embedded=(ROOT/'src/ui.inc').read_text()
assert '.periods-table td' in embedded and '.clocks-table td' in embedded
server=(ROOT/'src/server.c').read_text()
period=(ROOT/'src/reconciliation.inc').read_text()
assert "<table class='clocks-table'><thead><tr>" in server
assert "<table class='periods-table'><thead><tr>" in period
assert '<thead<tr>' not in server and '<thead<tr>' not in period
assert "document.querySelectorAll('.overview-time')" in server
assert server.count("class='overview-time'") == 2
assert "strftime('%Y-%m-%d %H:%M:%S',u.last_seen,'localtime')" not in server
print('PASS: generated mobile CSS, valid table heads and both Overview timestamp helpers')
