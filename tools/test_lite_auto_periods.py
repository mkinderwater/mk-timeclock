#!/usr/bin/env python3
"""Connected unattended period upkeep: fresh, replenish, disabled, restart, cycles."""
import os, socket, sqlite3, subprocess, tempfile, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]

def check():
 with tempfile.TemporaryDirectory() as d:
  path=str(Path(d)/'test.sqlite')
  port_sock=socket.socket();port_sock.bind(('127.0.0.1',0));port=port_sock.getsockname()[1];port_sock.close()
  def tick():
   with open(os.devnull,'w') as devnull:
    proc=subprocess.Popen([str(ROOT/'timeclock-server'),path,str(port)],env={**os.environ,'DEVICE_KEY':'5'*32},stdout=devnull,stderr=devnull)
    try:
     for _ in range(60):
      if proc.poll() is not None:raise AssertionError('server exited')
      if Path(path).exists():
       try:
        with sqlite3.connect(path) as con:
         con.execute('SELECT 1 FROM pay_periods').fetchone()
         # The server's first maintenance iteration will have finished when listening.
         if con.execute('SELECT count(*) FROM pay_periods').fetchone()[0]:break
       except sqlite3.Error:pass
      time.sleep(.05)
     time.sleep(.15)
    finally:
     proc.terminate();proc.wait(timeout=5)
  def periods():
   with sqlite3.connect(path) as db:
    return db.execute('SELECT starts,ends FROM pay_periods ORDER BY starts').fetchall()
  tick();first=periods()
  today=time.strftime('%Y-%m-%d',time.localtime())
  assert len([x for x in first if x[0]>today])==2, first
  assert all(a[1]<b[0] for a,b in zip(first,first[1:])),first
  tick(); assert periods()==first,'restart duplicated periods'
  with sqlite3.connect(path) as db:
   db.execute('DELETE FROM pay_periods WHERE starts=?',(first[-1][0],))
  tick(); assert periods()==first,'missing future period not replenished'
  tick();assert periods()==first,'generation changed dates on restart'
  # An established schedule's closed record must not be changed.
  with sqlite3.connect(path) as db:
   db.execute("UPDATE pay_periods SET status='closed' WHERE starts=?",(first[0][0],))
  tick()
  assert periods()==first,'maintenance changed historical period dates'
  print('PASS: fresh database, exactly two future periods, restart, gap replenishment, always-on generation, closed period retained')

if __name__=='__main__':check()
