#!/usr/bin/env bash
set -Eeuo pipefail
python3 - <<'PYDB'
import sqlite3,sys
try:
    con=sqlite3.connect('file:/var/lib/timeclock/server.db?mode=ro',uri=True)
    n=con.execute("SELECT count(*) FROM employees e JOIN employee_roles r ON r.employee_id=e.id AND r.role='manager' JOIN user_accounts u ON u.employee_id=e.id WHERE e.active=1 AND u.disabled=0").fetchone()[0]
except sqlite3.Error:
    sys.exit(1)
sys.exit(0 if n else 1)
PYDB
