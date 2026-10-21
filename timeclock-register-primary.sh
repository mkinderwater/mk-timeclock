#!/usr/bin/env bash
set -Eeuo pipefail
[[ $EUID -eq 0 ]] || { echo 'Requires root' >&2; exit 1; }
source /opt/icubedev-timeclock/bin/tc-env.sh
ENV=/etc/timeclock/timeclock.env
[[ $(tc_val "$ENV" TIMECLOCK_ROLE) == primary ]] || { echo 'Not Primary' >&2; exit 1; }
export PRIMARY_ID="$(tc_val "$ENV" CLOCK_ID)"
export PRIMARY_KEY="$(tc_val "$ENV" DEVICE_KEY)"
python3 - <<'PYDB'
import os, sqlite3, sys, time
clock=os.environ['PRIMARY_ID']; key=os.environ['PRIMARY_KEY']
if not clock or len(clock)>48 or not all(c.isalnum() or c in '-_' for c in clock): sys.exit('Invalid local clock ID')
if len(key)<16 or len(key)>128 or not all(c.isalnum() or c in '-_' for c in key): sys.exit('Invalid local device key')
path='/var/lib/timeclock/server.db'
for attempt in range(40):
    try:
        con=sqlite3.connect('file:'+path+'?mode=rw',uri=True,timeout=3)
        try:
            con.execute('PRAGMA busy_timeout=3000')
            con.execute('BEGIN IMMEDIATE')
            row=con.execute('SELECT device_key,active FROM registered_clocks WHERE clock_id=?',(clock,)).fetchone()
            if row:
                if row != (key,1): sys.exit('Primary registration conflicts with existing credential or revoked clock')
            else:
                con.execute('INSERT INTO registered_clocks(clock_id,device_key,active) VALUES(?,?,1)',(clock,key))
            con.commit()
            print('Local Primary clock registered:',clock)
            break
        finally: con.close()
    except (sqlite3.OperationalError,sqlite3.DatabaseError) as e:
        if attempt==39: sys.exit('Primary database not ready: '+str(e))
        time.sleep(.25)
PYDB
unset PRIMARY_ID PRIMARY_KEY
