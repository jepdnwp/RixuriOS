import os,select,subprocess,time
from pathlib import Path
ROOT=Path('/home/ubuntu/work/RixuriOS');out=bytearray();cur=0
p=subprocess.Popen(['bash','./scripts/run-qemu.sh'],cwd=ROOT,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=os.environ.copy())
def until(m,t=30):
 global cur
 end=time.monotonic()+t
 while time.monotonic()<end:
  i=out.find(m,cur)
  if i>=0:cur=i+len(m);return True
  r,_,_=select.select([p.stdout],[],[],.2)
  if r:
   c=os.read(p.stdout.fileno(),4096)
   if not c:return False
   out.extend(c)
 return False
try:
 if not until(b'USER: init returned to kernel'):raise RuntimeError('boot')
 time.sleep(1);p.stdin.write(b'/usr/bin/credtest\n');p.stdin.flush()
 if not until(b'rixuri$ ',20):raise RuntimeError('prompt')
finally:
 p.terminate()
 try:p.wait(timeout=3)
 except subprocess.TimeoutExpired:p.kill();p.wait()
Path('/tmp/phase20-cred-qemu.log').write_bytes(out)
print(out.decode('utf-8','replace'))
for marker in (b'before uid=0 gid=0',b'after uid=1000 gid=1000'):
 if marker not in out:raise SystemExit('missing '+repr(marker))
if b'PAGE FAULT' in out or b'CPU exception' in out or b'PANIC' in out:raise SystemExit('fault')
print('qemu Phase 20 credential/permission test: PASS')
