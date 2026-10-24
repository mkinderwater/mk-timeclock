"""Regression: SIGTERM and SIGINT interrupt the main ppoll wait promptly."""
import os, signal, socket, subprocess, tempfile, time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
 for i in range(25):
  with socket.socket() as sock:
   sock.bind(('127.0.0.1',0));port=sock.getsockname()[1]
  p=subprocess.Popen([str(ROOT/'timeclock-server'),str(Path(tmp)/f'case{i}.db'),str(port)],cwd=ROOT,env={**os.environ,'DEVICE_KEY':'0123456789abcdef0123456789abcdef'},stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
  try:
   ready=False
   for _ in range(300):
    if p.poll() is not None:break
    try:
     with socket.create_connection(('127.0.0.1',port),.1):ready=True;break
    except OSError:time.sleep(.01)
   assert ready, f'server failed to start: {p.poll()}'
   time.sleep(.015 if i%2 else .065)
   start=time.monotonic()
   p.send_signal(signal.SIGTERM if i%2 else signal.SIGINT)
   p.wait(timeout=3)
   assert time.monotonic()-start<3, 'slow shutdown'
  finally:
   if p.poll() is None:p.kill();p.wait()
print('PASS 25 prompt server shutdowns with SIGTERM/SIGINT')
