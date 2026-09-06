#!/usr/bin/env python3
import os, select, shutil, subprocess, sys, time
from pathlib import Path
ROOT=Path(__file__).resolve().parent.parent
IMAGE=ROOT/'build/rixfs-phase19.img'; ESP=ROOT/'build/uefi/esp-phase19'; LOG=ROOT/'build/qemu-phase19-utils.log'
shutil.copyfile(ROOT/'build/rixfs.img',IMAGE); shutil.copytree(ROOT/'build/uefi/esp',ESP)
env=os.environ.copy(); env['RIXURI_RIXFS_IMAGE']=str(IMAGE); env['RIXURI_ESP']=str(ESP)
proc=subprocess.Popen(['bash','./scripts/run-qemu.sh'],cwd=ROOT,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=env)
out=bytearray(); cursor=0
def until(marker,timeout=30):
 global cursor
 end=time.monotonic()+timeout
 while time.monotonic()<end:
  pos=out.find(marker,cursor)
  if pos>=0: cursor=pos+len(marker); return True
  ready,_,_=select.select([proc.stdout],[],[],.2)
  if ready:
   chunk=os.read(proc.stdout.fileno(),4096)
   if not chunk:return False
   out.extend(chunk)
 return False
def command(line):
 for b in line+b'\n': proc.stdin.write(bytes((b,))); proc.stdin.flush(); time.sleep(.005)
 if not until(b'rixuri$ '): raise RuntimeError('prompt missing after '+line.decode())
try:
 if not until(b'USER: init returned to kernel'): raise RuntimeError('init did not return')
 time.sleep(1)
 for line in [
  b'/bin/mkdir /tmp',
  b'/bin/echo alpha > /tmp/p19',
  b'/bin/echo beta >> /tmp/p19',
  b'/usr/bin/find /tmp p19',
  b'/bin/cat /tmp/p19 | /usr/bin/sed s/beta/gamma/',
  b'/bin/echo one two | /usr/bin/xargs /bin/echo',
  b'/bin/test alpha = alpha && /bin/echo test-ok',
  b'/bin/test alpha = beta || /bin/echo test-failed-recovered',
  b'/bin/test alpha = beta; /bin/echo status-path',
  b'/bin/cat /tmp/p19 | /usr/bin/grep gamma',
 ]: command(line)
finally:
 LOG.write_bytes(out); proc.terminate()
 try: proc.wait(timeout=3)
 except subprocess.TimeoutExpired: proc.kill(); proc.wait()
 IMAGE.unlink(missing_ok=True); shutil.rmtree(ESP,ignore_errors=True)
sys.stdout.buffer.write(out)
checks={b'/tmp/p19': 'find output', b'gamma': 'sed/pipeline output', b'one two': 'xargs output', b'test-ok': 'test true/&&', b'test-failed-recovered': 'test false/||', b'status-path': 'sequential status'}
for needle,desc in checks.items():
 if needle not in out: raise SystemExit(desc+' missing')
if b'CPU exception' in out or b'PANIC' in out: raise SystemExit('kernel fault marker observed')
print('qemu phase19 utilities test: PASS')
