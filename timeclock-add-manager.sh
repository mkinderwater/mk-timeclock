#!/usr/bin/env python3
"""Root-only local first-manager provisioning and password recovery. No HTTP secret."""
import getpass
import os
import secrets
import sqlite3
import subprocess
import sys

DB = '/var/lib/timeclock/server.db'
if os.geteuid() != 0:
    sys.exit('Run as root.')
if len(sys.argv) not in (1, 3) or (len(sys.argv) == 3 and (sys.argv[1] != '--reset-password' or not sys.argv[2].isdigit())):
    sys.exit('Usage: timeclock-add-manager.sh [--reset-password MANAGER_ID]')

conn = sqlite3.connect(DB, timeout=5)
conn.execute('PRAGMA foreign_keys=ON')
existing = conn.execute("SELECT count(*) FROM employees e JOIN employee_roles r ON r.employee_id=e.id AND r.role='manager' JOIN user_accounts u ON u.employee_id=e.id WHERE e.active=1 AND u.disabled=0").fetchone()[0]
recover = len(sys.argv) == 3
if existing and not recover:
    sys.exit('Manager already exists. Use the GUI, or root-only --reset-password MANAGER_ID for recovery.')
if not existing and recover:
    sys.exit('No manager exists. Run without options to provision one.')
if recover:
    manager_id = int(sys.argv[2])
    check = conn.execute("SELECT 1 FROM employees e JOIN employee_roles r ON r.employee_id=e.id AND r.role='manager' WHERE e.id=?", (manager_id,)).fetchone()
    if not check:
        sys.exit('Specified employee is not a manager.')
else:
    first = input('Initial manager first name: ').strip()
    last = input('Initial manager last name: ').strip()
    position = input('Position (job title): ').strip()
    email = input('Manager login email: ').strip()
    if not first or not last or not position or '@' not in email:
        sys.exit('First name, last name, position and a valid email are required.')
    name = f'{first} {last}'
password = getpass.getpass('Password (12+ characters): ')
if len(password) < 12 or password != getpass.getpass('Repeat password: '):
    sys.exit('Password invalid or mismatch.')
salt = secrets.token_hex(12)
p = subprocess.run(['openssl', 'passwd', '-6', '-salt', salt, '-stdin'], input=(password+'\n').encode(), stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True)
hashed = p.stdout.decode().strip()
try:
    conn.execute('BEGIN IMMEDIATE')
    if not recover:
        cur = conn.execute('INSERT INTO employees(name,email,active,first_name,last_name,position) VALUES(?,?,1,?,?,?)', (name,email,first,last,position))
        manager_id = cur.lastrowid
        conn.execute("INSERT INTO employee_roles(employee_id,role) VALUES(?,'manager')", (manager_id,))
    conn.execute('INSERT INTO user_accounts(employee_id,password_hash,disabled) VALUES(?,?,0) ON CONFLICT(employee_id) DO UPDATE SET password_hash=excluded.password_hash,disabled=0', (manager_id,hashed))
    conn.execute('DELETE FROM login_sessions WHERE employee_id=?', (manager_id,))
    conn.commit()
except Exception:
    conn.rollback()
    raise
print(('Recovered' if recover else 'Created') + f' manager #{manager_id}. Sign in through the GUI.')
